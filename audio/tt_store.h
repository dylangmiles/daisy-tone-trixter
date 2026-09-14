// audio/tt_store.h — Tone Trixter on-card settings store.
//
// Reads /tonetrix/config.txt and /tonetrix/presets.txt off the mounted SD card and
// turns them into a runtime Preset table + global config. The format is flow-style
// YAML (flat "key: value" lines, inline [a, b, c] lists, --- between presets,
// # comments) — valid YAML, but with no significant indentation so a card edited in
// any text editor can't silently orphan a key.
//
// Fallback model: if the /tonetrix/ folder / files are absent, nothing is parsed and
// the caller keeps the firmware's built-in presets + defaults (see main.cpp boot).
#ifndef TT_STORE_H
#define TT_STORE_H

#include "audio/dsp_chain.h"   // Preset (kept OUTSIDE the extern "C" block below)
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parse /tonetrix/config.txt + /tonetrix/presets.txt from the already-mounted card.
// Safe to call with no card / no folder. Returns true if a config value or >=1 valid
// preset was parsed (i.e. the card overrides the built-ins).
bool tt_store_load(void);

// Parsed preset table to hand to dsp_chain_install_presets(). Returns NULL / *n_out=0
// when no presets were parsed (caller keeps the built-in table). The storage is
// static and valid for the program's life.
const Preset *tt_store_presets(int *n_out);

// config.txt: boot preset name ("" if unspecified). gr_meter default, with *was_set
// telling the caller whether config.txt actually specified it.
const char *tt_store_boot_preset(void);
bool        tt_store_gr_meter(bool *was_set);

// /tonetrix/songs.txt -- SONG MODE. A song is a name, a preset (by name) and a backing track (a
// file in /tonetrix/backing, or "none"). Parsed by tt_store_load(); count 0 when the file is absent.
// Later a song may be a looping session with a bpm and triggerable sounds; for now it is these three.
#define TT_MAX_SONGS 16
int         tt_store_song_count(void);
const char *tt_store_song_name(int i);      // "" out of range
const char *tt_store_song_preset(int i);    // "" = keep whatever preset is loaded
const char *tt_store_song_backing(int i);   // "" = no backing track
// type: "song" (default) or "looping"; more later. bpm 0 = unspecified; bars 0 = unspecified.
typedef enum { TT_SONG_PLAIN = 0, TT_SONG_LOOPING = 1 } tt_song_type_t;
tt_song_type_t tt_store_song_type(int i);
float          tt_store_song_bpm(int i);
int            tt_store_song_bars(int i);

// Print a one-shot summary of what was loaded (for the `sdcfg` UART command).
void        tt_store_dump(void);

#ifdef __cplusplus
}
#endif

#endif // TT_STORE_H
