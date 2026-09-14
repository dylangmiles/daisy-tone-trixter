#include "audio/looper.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cassert>
#define BLK 48
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)
// Feed n samples of constant value v through the looper; return the output of the last block.
static float run(int n, float v, float *last_out = nullptr){
    float buf[BLK]; float o = 0;
    for(int i=0;i<n;i+=BLK){ for(int k=0;k<BLK;k++) buf[k]=v; looper_process(buf, BLK); o = buf[BLK-1]; }
    if(last_out) *last_out = o; return o;
}
int main(){
    looper_reset(); run(BLK,0);
    CHECK(looper_state()==LOOPER_EMPTY);

    // press ARMS; silence keeps it armed; signal starts the pass
    looper_press(); run(BLK*10, 0.f);
    CHECK(looper_state()==LOOPER_ARMED);
    run(4*48000, 0.25f);
    CHECK(looper_state()==LOOPER_RECORDING);
    looper_press(); run(BLK, 0.f);
    CHECK(looper_state()==LOOPER_PLAYING); CHECK(looper_layers()==1);
    CHECK(looper_length() >= 4*48000 - BLK && looper_length() <= 4*48000 + BLK);
    printf("len=%u bpm=%.1f\n", looper_length(), looper_bpm());
    // playing: silent input, output should be ~0.25 (the layer)
    float o = run(BLK*10, 0.f); CHECK(fabsf(o-0.25f) < 0.002f);

    // overdub: 0.5 for one full loop -> auto-commit, 2 layers, output 0.75
    looper_press(); run(BLK, 0.5f); CHECK(looper_state()==LOOPER_OVERDUB);
    run(looper_length(), 0.5f);
    CHECK(looper_state()==LOOPER_PLAYING); CHECK(looper_layers()==2);
    o = run(BLK*10, 0.f); CHECK(fabsf(o-0.75f) < 0.003f);

    // undo -> 1 layer, output 0.25
    looper_undo(); run(BLK, 0.f); CHECK(looper_layers()==1);
    o = run(BLK*10, 0.f); CHECK(fabsf(o-0.25f) < 0.002f);

    // overdub cut short after 1/4 loop by a press: extent = prefix if started at 0, else head zeroed
    // get position to ~0 first: play until wrap
    while(looper_position() > BLK) run(BLK,0.f);
    uint32_t st = looper_position();
    looper_press(); run(BLK, 0.5f);
    run(looper_length()/4, 0.5f);
    looper_press(); run(BLK, 0.f); CHECK(looper_layers()==2); CHECK(looper_state()==LOOPER_PLAYING);
    // now the new layer is 0.5 for the first quarter, 0 after. Read at 1/8 and at 3/4.
    while(looper_position() > BLK) run(BLK,0.f);
    run(looper_length()/8, 0.f); o = run(BLK, 0.f); CHECK(fabsf(o-0.75f) < 0.01f);
    run(looper_length()/2, 0.f); o = run(BLK, 0.f); CHECK(fabsf(o-0.25f) < 0.01f);
    (void)st;

    // stop, press -> plays from the top
    looper_stop(); run(BLK,0.f); CHECK(looper_state()==LOOPER_STOPPED); CHECK(looper_position()==0);
    o = run(BLK,0.f); CHECK(fabsf(o) < 1e-6f);
    looper_press(); run(BLK,0.f); CHECK(looper_state()==LOOPER_PLAYING);

    // undo twice -> EMPTY
    looper_undo(); run(BLK,0.f); looper_undo(); run(BLK,0.f);
    CHECK(looper_state()==LOOPER_EMPTY); CHECK(looper_layers()==0); CHECK(looper_length()==0);

    // tempo lock: 120 bpm, bars 2 -> length = 2*4*24000 = 192000 regardless of pass length
    looper_set_tempo(120.f, 2); looper_press(); run(30000, 0.25f); looper_press(); run(BLK,0.f);
    CHECK(looper_length()==192000); CHECK(fabsf(looper_bpm()-120.f)<0.01f);
    // beyond the written extent reads zero
    run(60000, 0.f); o = run(BLK, 0.f); CHECK(fabsf(o) < 1e-6f);
    looper_reset(); run(BLK,0.f);

    // bpm only: 100 bpm, pass of 1.3 bars -> snaps to 1 bar = 115200
    looper_set_tempo(100.f, 0); looper_press(); run((int)(115200*1.3), 0.25f); looper_press(); run(BLK,0.f);
    CHECK(looper_length()==115200);
    looper_reset(); run(BLK,0.f); looper_set_tempo(0,0);

    // press-then-hold: press starts an overdub, the hold reverts it and undoes a layer
    looper_reset(); run(BLK,0.f); looper_set_tempo(0,0);
    looper_press(); run(2*48000, 0.25f); looper_press(); run(BLK,0.f);      // 1 layer
    looper_press(); run(looper_length(), 0.5f);                              // overdub, commits -> 2
    CHECK(looper_layers()==2);
    looper_press(); run(BLK, 0.5f); CHECK(looper_state()==LOOPER_OVERDUB);  // press starts a 3rd
    looper_unpress(); looper_undo(); run(BLK,0.f);                           // hold: revert + undo
    CHECK(looper_state()==LOOPER_PLAYING); CHECK(looper_layers()==1);

    // free-loop late press: silence, note at 2.0 s, press 100 ms after -> loop ends at the note
    looper_reset(); run(BLK,0.f);
    looper_press(); run(48000, 0.25f);          // start on signal (t=0)
    run(48000, 0.f);                            // silence to 2.0 s (env decays below threshold)
    run(4800, 0.25f);                           // onset at 2.0 s, 100 ms of note
    looper_press(); run(BLK,0.f);
    printf("late-press len=%u (want ~96000)\n", looper_length());
    CHECK(looper_length() >= 96000 - 2*BLK && looper_length() <= 96000 + 2*BLK);

    // free-loop press well after the last onset (400 ms): no snap
    looper_reset(); run(BLK,0.f);
    looper_press(); run(48000, 0.25f); run(48000, 0.f); run(19200, 0.25f); looper_press(); run(BLK,0.f);
    CHECK(looper_length() >= 115200 - 2*BLK && looper_length() <= 115200 + 2*BLK);
    looper_reset(); run(BLK,0.f);

    // cap: record past 60 s closes automatically
    looper_press(); run(60*48000 + 4800, 0.1f);
    CHECK(looper_state()==LOOPER_PLAYING); CHECK(looper_length()==60u*48000u);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails;
}
