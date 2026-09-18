// audio/tt_store.cpp — see tt_store.h. Flow-YAML parser for config + presets.
#include "audio/tt_store.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>   // strcasecmp
#include <stdlib.h>

extern "C" {
#include "ff.h"
}

#define MAX_SD_PRESETS 12
#define NAME_MAX       40
#define IR_MAX         64

// Parsed results (static storage — dsp_chain_install_presets keeps the pointer).
static Preset s_sd[MAX_SD_PRESETS];
static char   s_name[MAX_SD_PRESETS][NAME_MAX];
static char   s_ir[MAX_SD_PRESETS][IR_MAX];
static int    s_n = 0;

// Songs (songs.txt). Same flow-YAML shape as presets: a "name:" starts a song, "---" optional.
static char   s_song_name[TT_MAX_SONGS][NAME_MAX];
static char   s_song_preset[TT_MAX_SONGS][NAME_MAX];
static char   s_song_backing[TT_MAX_SONGS][IR_MAX];
static uint8_t s_song_type[TT_MAX_SONGS];
static float  s_song_bpm[TT_MAX_SONGS];
static int    s_song_bars[TT_MAX_SONGS];
static int    s_songs = 0;

static char   s_boot[NAME_MAX] = "";
static bool   s_gr = false, s_gr_set = false;
static bool   s_have_config = false;

// --- small text helpers ----------------------------------------------------
// Trim leading/trailing ASCII whitespace in place; return the trimmed start.
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

// Strip surrounding single/double quotes in place.
static char *unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n-1] == '"') || (s[0] == '\'' && s[n-1] == '\''))) {
        s[n-1] = 0;
        return s + 1;
    }
    return s;
}

static bool parse_bool(const char *v) {
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' ||
           strcmp(v, "on") == 0 || strcmp(v, "ON") == 0 || strcmp(v, "On") == 0;
}

// Parse an inline flow list "[a, b, c]" (brackets optional) into out[]; returns count.
static int parse_list(const char *v, float *out, int maxn) {
    const char *s = strchr(v, '[');
    s = s ? s + 1 : v;
    int n = 0;
    while (n < maxn) {
        while (*s == ' ' || *s == ',' || *s == '\t') s++;
        if (*s == ']' || *s == 0) break;
        char *end;
        float f = strtof(s, &end);
        if (end == s) break;
        out[n++] = f;
        s = end;
    }
    return n;
}

// Read a whole small file into buf (NUL-terminated). Returns length, or -1 if absent.
// ⚠ A file longer than the buffer is TRUNCATED, and the loss is invisible: the last preset simply
// inherits `default` for every key past the cut. That is exactly what happened on 2026-09-18 -- a
// commented-up presets.txt reached 9133 bytes against an 8192 buffer, and slg110s loaded with the
// default EQ and compressor while its IR (an early key) was fine. So: say so, loudly.
static int read_file(const char *path, char *buf, int cap) {
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    UINT total = 0, br;
    while (total < (UINT)(cap - 1)) {
        if (f_read(&f, buf + total, (UINT)(cap - 1) - total, &br) != FR_OK) { f_close(&f); return -1; }
        if (br == 0) break;
        total += br;
    }
    buf[total] = 0;
    if (total >= (UINT)(cap - 1) && f_size(&f) > (FSIZE_t)total)
        printf("!! %s is %lu bytes, buffer %d: TRUNCATED -- later presets are missing keys\n",
               path, (unsigned long)f_size(&f), cap);
    f_close(&f);
    return (int)total;
}

// Split "key: value" in place. Returns false if there's no ':'. Both sides trimmed.
static bool split_kv(char *line, char **key, char **val) {
    char *colon = strchr(line, ':');
    if (!colon) return false;
    *colon = 0;
    *key = trim(line);
    *val = trim(colon + 1);
    return true;
}

// --- config.txt ------------------------------------------------------------
static void parse_config(char *buf) {
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *hash = strchr(p, '#');
        if (hash) *hash = 0;
        char *line = trim(p);
        char *k, *v;
        if (*line && split_kv(line, &k, &v)) {
            if (strcmp(k, "boot_preset") == 0) {
                strncpy(s_boot, unquote(v), NAME_MAX - 1); s_boot[NAME_MAX - 1] = 0;
                s_have_config = true;
            } else if (strcmp(k, "gr_meter") == 0) {
                s_gr = parse_bool(v); s_gr_set = true; s_have_config = true;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

// --- presets.txt -----------------------------------------------------------
static void commit(const Preset *cur, const char *nm, const char *ir, bool *active) {
    if (!*active) return;
    *active = false;
    if (nm[0] == 0 || s_n >= MAX_SD_PRESETS) return;
    strncpy(s_name[s_n], nm, NAME_MAX - 1); s_name[s_n][NAME_MAX - 1] = 0;
    strncpy(s_ir[s_n],   ir, IR_MAX - 1);   s_ir[s_n][IR_MAX - 1]     = 0;
    s_sd[s_n]      = *cur;
    s_sd[s_n].name = s_name[s_n];
    s_sd[s_n].ir   = s_ir[s_n][0] ? s_ir[s_n] : NULL;
    s_n++;
}

static void parse_presets(char *buf) {
    const Preset *def = dsp_chain_default_preset();
    Preset cur = *def;
    char nm[NAME_MAX] = "";
    char ir[IR_MAX];
    strncpy(ir, def->ir ? def->ir : "", IR_MAX - 1); ir[IR_MAX - 1] = 0;
    bool active = false;

    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *hash = strchr(p, '#');
        if (hash) *hash = 0;
        char *line = trim(p);
        if (!*line) { if (!nl) break; p = nl + 1; continue; }

        if (strcmp(line, "---") == 0) {
            commit(&cur, nm, ir, &active);
            cur = *def; nm[0] = 0;
            strncpy(ir, def->ir ? def->ir : "", IR_MAX - 1); ir[IR_MAX - 1] = 0;
            if (!nl) break; p = nl + 1; continue;
        }

        char *k, *v;
        if (split_kv(line, &k, &v)) {
            if (strcmp(k, "name") == 0) {
                commit(&cur, nm, ir, &active);            // implicit separator if --- omitted
                cur = *def;
                strncpy(ir, def->ir ? def->ir : "", IR_MAX - 1); ir[IR_MAX - 1] = 0;
                strncpy(nm, unquote(v), NAME_MAX - 1); nm[NAME_MAX - 1] = 0;
                active = true;
            } else if (active) {
                float a[5];
                if (strcmp(k, "ir") == 0) {          // "none"/"off"/empty → no IR (convolution off)
                    const char *uv = unquote(v);
                    if (uv[0] == 0 || strcasecmp(uv, "none") == 0 || strcasecmp(uv, "off") == 0)
                        ir[0] = 0;                   // committed as ir=NULL ⇒ resolves to the "none" IR
                    else { strncpy(ir, uv, IR_MAX - 1); ir[IR_MAX - 1] = 0; }
                }
                else if (strcmp(k, "in.on") == 0)     cur.in_on   = parse_bool(v);
                else if (strcmp(k, "eq.on") == 0)     cur.eq_on   = parse_bool(v);
                else if (strcmp(k, "comp.on") == 0)   cur.comp_on = parse_bool(v);
                else if (strcmp(k, "out.on") == 0)    cur.out_on  = parse_bool(v);
                else if (strcmp(k, "in.level") == 0)  cur.in_level  = strtof(v, NULL);
                else if (strcmp(k, "out.level") == 0) cur.out_level = strtof(v, NULL);
                else if (strcmp(k, "byp.level") == 0) cur.byp_level = strtof(v, NULL); // ⚠ Daisy addition
                else if (strcmp(k, "bk.level") == 0) cur.bk_level = strtof(v, NULL);   // ⚠ Daisy addition
                else if (strcmp(k, "pga") == 0)       cur.pga = (int)strtol(v, NULL, 10);   // ES8388 PGA dB (K&K 12, Garrison 6)
                else if (strcmp(k, "eq.lo") == 0)  { int c = parse_list(v, a, 2); if (c>=1) cur.lo_f=a[0]; if (c>=2) cur.lo_g=a[1]; }
                else if (strcmp(k, "eq.mid") == 0) { int c = parse_list(v, a, 3); if (c>=1) cur.mid_f=a[0]; if (c>=2) cur.mid_g=a[1]; if (c>=3) cur.mid_q=a[2]; }
                else if (strcmp(k, "eq.hi") == 0)  { int c = parse_list(v, a, 2); if (c>=1) cur.hi_f=a[0]; if (c>=2) cur.hi_g=a[1]; }
                else if (strcmp(k, "comp") == 0)   { int c = parse_list(v, a, 5);
                    if (c>=1) cur.thr=a[0]; if (c>=2) cur.ratio=a[1]; if (c>=3) cur.att=a[2];
                    if (c>=4) cur.rel=a[3]; if (c>=5) cur.mkup=a[4]; }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    commit(&cur, nm, ir, &active);
}

// --- songs.txt -------------------------------------------------------------
static void song_commit(const char *nm, const char *pr, const char *bk, uint8_t ty, float bpm, int bars, bool *active) {
    if (!*active) return;
    *active = false;
    if (nm[0] == 0 || s_songs >= TT_MAX_SONGS) return;
    strncpy(s_song_name[s_songs],    nm, NAME_MAX - 1); s_song_name[s_songs][NAME_MAX - 1]  = 0;
    strncpy(s_song_preset[s_songs],  pr, NAME_MAX - 1); s_song_preset[s_songs][NAME_MAX - 1] = 0;
    strncpy(s_song_backing[s_songs], bk, IR_MAX - 1);   s_song_backing[s_songs][IR_MAX - 1]  = 0;
    s_song_type[s_songs] = ty; s_song_bpm[s_songs] = bpm; s_song_bars[s_songs] = bars;
    s_songs++;
}

static void parse_songs(char *buf) {
    char nm[NAME_MAX] = "", pr[NAME_MAX] = "", bk[IR_MAX] = "";
    uint8_t ty = TT_SONG_PLAIN; float bpm = 0.f; int bars = 0;
    bool active = false;
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *hash = strchr(p, '#');
        if (hash) *hash = 0;
        char *line = trim(p);
        if (!*line) { if (!nl) break; p = nl + 1; continue; }

        if (strcmp(line, "---") == 0) {
            song_commit(nm, pr, bk, ty, bpm, bars, &active);
            nm[0] = pr[0] = bk[0] = 0; ty = TT_SONG_PLAIN; bpm = 0.f; bars = 0;
            if (!nl) break; p = nl + 1; continue;
        }
        char *k, *v;
        if (split_kv(line, &k, &v)) {
            if (strcmp(k, "name") == 0) {
                song_commit(nm, pr, bk, ty, bpm, bars, &active);
                pr[0] = bk[0] = 0; ty = TT_SONG_PLAIN; bpm = 0.f; bars = 0;
                strncpy(nm, unquote(v), NAME_MAX - 1); nm[NAME_MAX - 1] = 0;
                active = true;
            } else if (active) {
                if (strcmp(k, "preset") == 0) {
                    strncpy(pr, unquote(v), NAME_MAX - 1); pr[NAME_MAX - 1] = 0;
                } else if (strcmp(k, "backing") == 0) {
                    const char *uv = unquote(v);
                    if (uv[0] == 0 || strcasecmp(uv, "none") == 0 || strcasecmp(uv, "off") == 0) bk[0] = 0;
                    else { strncpy(bk, uv, IR_MAX - 1); bk[IR_MAX - 1] = 0; }
                } else if (strcmp(k, "type") == 0) {
                    const char *uv = unquote(v);
                    ty = (strcasecmp(uv, "looping") == 0 || strcasecmp(uv, "loop") == 0 || strcasecmp(uv, "looper") == 0)
                         ? TT_SONG_LOOPING : TT_SONG_PLAIN;
                } else if (strcmp(k, "bpm") == 0) {
                    bpm = strtof(v, NULL);
                } else if (strcmp(k, "bars") == 0) {
                    bars = (int)strtol(v, NULL, 10);
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    song_commit(nm, pr, bk, ty, bpm, bars, &active);
}

tt_song_type_t tt_store_song_type(int i) { return (i >= 0 && i < s_songs) ? (tt_song_type_t)s_song_type[i] : TT_SONG_PLAIN; }
float          tt_store_song_bpm(int i)  { return (i >= 0 && i < s_songs) ? s_song_bpm[i]  : 0.f; }
int            tt_store_song_bars(int i) { return (i >= 0 && i < s_songs) ? s_song_bars[i] : 0; }

int         tt_store_song_count(void)      { return s_songs; }
const char *tt_store_song_name(int i)      { return (i >= 0 && i < s_songs) ? s_song_name[i]    : ""; }
const char *tt_store_song_preset(int i)    { return (i >= 0 && i < s_songs) ? s_song_preset[i]  : ""; }
const char *tt_store_song_backing(int i)   { return (i >= 0 && i < s_songs) ? s_song_backing[i] : ""; }

// --- public API ------------------------------------------------------------
bool tt_store_load(void) {
    s_n = 0; s_songs = 0; s_have_config = false; s_gr = false; s_gr_set = false; s_boot[0] = 0;

    static char buf[32768];   // ⚠ was 8192: presets.txt outgrew it on 2026-09-18 (comments count)
    if (read_file("/tonetrix/config.txt",  buf, sizeof buf) >= 0) parse_config(buf);
    if (read_file("/tonetrix/presets.txt", buf, sizeof buf) >= 0) parse_presets(buf);
    if (read_file("/tonetrix/songs.txt",   buf, sizeof buf) >= 0) parse_songs(buf);

    return s_have_config || s_n > 0;
}

const Preset *tt_store_presets(int *n_out) {
    if (n_out) *n_out = s_n;
    return s_n > 0 ? s_sd : NULL;
}

const char *tt_store_boot_preset(void) { return s_boot; }

bool tt_store_gr_meter(bool *was_set) {
    if (was_set) *was_set = s_gr_set;
    return s_gr;
}

void tt_store_dump(void) {
    printf("sdcfg: config=%s", s_have_config ? "yes" : "no");
    if (s_boot[0])  printf(" boot_preset=%s", s_boot);
    if (s_gr_set)   printf(" gr_meter=%s", s_gr ? "on" : "off");
    printf("\nsdcfg: presets=%d%s\n", s_n, s_n ? "" : " (using built-ins)");
    for (int i = 0; i < s_n; i++)
        printf("  %-16s ir=%s\n", s_sd[i].name, s_sd[i].ir ? s_sd[i].ir : "(keep)");
    printf("sdcfg: songs=%d\n", s_songs);
    for (int i = 0; i < s_songs; i++)
        printf("  %-16s %s preset=%s backing=%s bpm=%.0f bars=%d\n", s_song_name[i],
               s_song_type[i] == TT_SONG_LOOPING ? "looping" : "song",
               s_song_preset[i][0] ? s_song_preset[i] : "(keep)",
               s_song_backing[i][0] ? s_song_backing[i] : "(none)",
               (double)s_song_bpm[i], s_song_bars[i]);
}
