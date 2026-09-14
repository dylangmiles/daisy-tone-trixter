// audio/looper.h — a layered looper in SDRAM, for `type: looping` songs.
//
// Model: ONE loop per song, made of up to LOOPER_MAX_LAYERS layers. Every record pass writes a NEW
// layer; playback sums the layers. Nothing is ever mixed into an existing buffer, which is what
// makes undo free: undo = drop the top layer. There is no redo -- play it again.
//
// The first record pass sets the loop length (LOOPER_MAX_SECONDS cap). Later passes are exactly
// that length: an overdub records for one loop and then drops back to playing on its own.
//
// Where it sits in the signal path: AFTER the chain (IR / EQ / comp), BEFORE the backing mix -- so
// a layer captures the sound the player heard, and the loop is a bed exactly like a backing track.
// No dry capture: a looping song on the `default` preset IS the dry option.
//
// Storage: SDRAM (64 MB, nothing else uses it). The bit-banged SD card is ~291 kB/s, blocking and
// foreground-only -- right for streaming a backing track, wrong for an overdubbing looper. Loops are
// session-lifetime, which is what RAM is; saving one to the card afterwards is a separate feature.
//
// Threading: looper_process() runs in the audio callback; the control calls run in the foreground
// and only set state, the callback does the work at the next block. Layer memory is static.
#ifndef TT_LOOPER_H
#define TT_LOOPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOOPER_MAX_LAYERS   8
#define LOOPER_MAX_SECONDS  60
#define LOOPER_RATE         48000

typedef enum {
    LOOPER_EMPTY,       // no loop yet; the next press ARMS the first record pass
    LOOPER_ARMED,       // waiting for the first note: recording starts on the input threshold
    LOOPER_RECORDING,   // first pass in progress: the length is being set
    LOOPER_PLAYING,     // loop running, no layer being recorded
    LOOPER_OVERDUB,     // loop running AND a new layer being recorded (auto-ends after one loop)
    LOOPER_STOPPED,     // loop exists, not running; press = play from the top
} looper_state_t;

// Foreground control. All cheap; the audio callback applies them at the next block boundary.
void           looper_reset(void);           // discard everything (leaving the song, or "clear")
void           looper_press(void);           // the RIGHT switch's press: empty->armed, armed/rec->play, play->overdub, overdub->play, stopped->play
void           looper_unpress(void);         // ⚠ revert the LAST press (the hold that follows a press means "undo", not "do then undo")
void           looper_stop(void);            // stop (a record pass in progress is kept as a layer)
void           looper_undo(void);            // drop the top layer; if that was the only one -> EMPTY
void           looper_set_level(float g);    // playback level 0..2 (default 1.0)

// Optional tempo lock. bpm > 0 and bars > 0: the length is fixed at bars*4 beats and the first pass
// is quantised to it. bpm > 0 alone: the first pass snaps to the nearest whole bar. Both 0: free --
// the end press snaps back to the last onset if it came within LOOPER_LATE_MS after it.
void           looper_set_tempo(float bpm, int bars);

// Arming: the first pass does not start on the press but on the first note -- input |x| above
// LOOPER_ARM_THRESHOLD. -40 dBFS sits 50 dB above this build's floor and 35 dB under a strum.
#define LOOPER_ARM_THRESHOLD 0.01f
#define LOOPER_LATE_MS       150

looper_state_t looper_state(void);
int            looper_layers(void);          // layers committed (not counting a pass in progress)
uint32_t       looper_length(void);          // loop length in samples, 0 while EMPTY/first pass
uint32_t       looper_position(void);        // play head, samples
float          looper_bpm(void);             // locked bpm, or the bpm detected from the first pass (4/4 assumed)

// Audio thread: mix the loop into buf[] (n samples, mono) and record from it if a pass is active.
// ⚠ Call AFTER the chain and BEFORE backing_mix(): the loop is a bed, not something to be processed.
void           looper_process(float *buf, int n);

#ifdef __cplusplus
}
#endif

#endif // TT_LOOPER_H
