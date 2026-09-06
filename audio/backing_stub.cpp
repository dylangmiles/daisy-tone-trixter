// audio/backing_stub.cpp — backing tracks, NOT YET PORTED.
//
// ⚠ STUB. The real backing.cpp depends on the Pico's i2s/i2s.h and pico/time.h and needs its
// streaming layer rewritten against the Daisy audio path. Until then this satisfies the menu's
// link-time dependency and reports "no tracks", so the backing picker shows only its "off" entry.
//
// The REAL header is used verbatim (audio/backing.h), so the finished port is a drop-in replacement
// for this file with no call-site changes anywhere.

#include "audio/backing.h"

#include <stdint.h>
#include <stddef.h>

void        backing_scan(void) {}
int         backing_count(void) { return 0; }
const char *backing_name(int i) { (void)i; return ""; }
int         backing_play(int i) { (void)i; return -1; }
void        backing_stop(void) {}
bool        backing_playing(void) { return false; }
int         backing_current(void) { return -1; }
void        backing_service(void) {}
void        backing_mix(float *dst, int n) { (void)dst; (void)n; }
void        backing_set_level(float g) { (void)g; }
float       backing_level(void) { return 1.0f; }
void        backing_stats(uint32_t *underruns, int *ring_pct, uint32_t *max_service_us)
{
    if(underruns)       *underruns = 0;
    if(ring_pct)        *ring_pct = 0;
    if(max_service_us)  *max_service_us = 0;
}
