/* PLAY MODES -- the playhead direction engine.
 *
 * The stock sequencer keeps advancing its own per-track step counter; this
 * engine only answers "which step should this track PLAY when the stock
 * playhead is on step `raw`?". Everything stock derives from the counter
 * (tempo, track scale/length, swing, pattern end, chains, CHAIN AFTER, the
 * MASTER LENGTH restart) is left alone.
 *
 * Every mapping is a pure function of (mode, length, cycle, raw, seed):
 * asking for the NEXT step (micro-timing look-ahead, the tick-2 pre-check)
 * returns exactly the step that will play there, and asking twice changes
 * nothing. The only state per track is the cycle count (how many times the
 * stock playhead wrapped since the last reset) and the seed.
 *
 * Freestanding: no floating point, no library calls, no allocation. Compile
 * for the unit with generate.py (m68k-elf-gcc -mcpu=5475), test on the host
 * with verify.py. */
#ifndef PLAYMODE_H
#define PLAYMODE_H
#include <stdint.h>

enum {
    PM_NORMAL = 0,   /* 1 2 3 4 ...                                         */
    PM_REVERSE,      /* 16 15 14 ... 1                                      */
    PM_PINGPONG,     /* 1 .. 16 15 .. 2 | 1 .. : the end steps play once    */
    PM_RANDOM,       /* any step, repeats allowed                           */
    PM_SHUFFLE,      /* every step once per pass, in a new order each pass  */
    PM_PINGPONG2,    /* 1 .. 16 16 .. 1 | 1 .. : the end steps play twice.
                        Numbered last so saved projects keep their digits;
                        the list the keys walk puts it after PINGPONG      */
    PM_MODES
};

#define PM_TRACKS 16      /* 0..7 audio T1..T8, 8..15 MIDI M1..M8 */
#define PM_MAX_LEN 64

/* What the user chose. `global` is used for every track while the pattern's
 * scale mode is NORMAL; `track[t]` while it is PER TRACK. Switching the
 * scale mode back and forth never loses either. */
typedef struct {
    uint8_t global;
    uint8_t track[PM_TRACKS];
    uint8_t reserved[3];
} PmSettings;

/* Runtime state per track. */
typedef struct {
    uint32_t cycle;      /* completed passes of the stock playhead          */
    uint32_t seed;       /* re-drawn at every reset (PLAY, pattern switch)  */
    uint8_t  last_raw;   /* the raw step pm_advance last saw                */
    uint8_t  started;    /* pm_advance has seen a step since the reset      */
    uint8_t  reserved[2];
} PmTrack;

typedef struct {
    PmSettings settings;
    PmTrack tracks[PM_TRACKS];
    uint32_t entropy;    /* the base every run's seeds derive from          */
    uint32_t run;        /* counts restarts; seeds = f(entropy, run, track) */
} PmState;

void     pm_init(PmState *s, uint32_t entropy);

/* The effective mode of track t under the pattern's scale mode. */
unsigned pm_mode(const PmState *s, unsigned track, unsigned per_track);

/* Transport start or pattern switch: every pass begins again from the start
 * of the pattern, and RANDOM / SHUFFLE draw a fresh sequence (the next run's
 * seeds). pm_reset restarts one track inside the current run. */
void     pm_reset(PmState *s, unsigned track);
void     pm_reset_all(PmState *s);

/* The step the NEXT run will play first-pass at `raw`: what stock prepares
 * while stopped must be what PLAY then plays. Changes nothing. */
unsigned pm_lookup_next_run(const PmState *s, unsigned track, unsigned raw,
                            unsigned len, unsigned per_track);

/* Call once when the stock playhead of `track` lands on `raw` (0..len-1).
 * A wrap (raw not greater than the last raw step) completes a pass. */
void     pm_advance(PmState *s, unsigned track, unsigned raw, unsigned len);

/* The step to play while the stock playhead is on `raw`. Under NORMAL scale
 * mode (per_track 0) all tracks share track 0's seed, so RANDOM / SHUFFLE
 * move every track together. `raw == len` is the
 * look-ahead across the pattern end: the first step of the next pass. Does
 * not change any state. */
unsigned pm_lookup(const PmState *s, unsigned track, unsigned raw,
                   unsigned len, unsigned per_track);

/* The pure mapping underneath pm_lookup. */
unsigned pm_map(unsigned mode, unsigned len, uint32_t cycle, unsigned raw,
                uint32_t seed);

/* UP / DOWN in the play-mode popup while a TRACK key is held. delta is -1
 * (UP: towards NORMAL) or +1 (DOWN: towards SHUFFLE); the list does not
 * wrap. Writes the changed mode (the shared one under NORMAL scale mode,
 * the track's own under PER TRACK) and returns it. */
unsigned pm_ui_step(PmState *s, unsigned track, int delta, unsigned per_track);

/* The popup text, e.g. "T3 PINGPONG", "M1 REVERSE", "ALL SHUFFLE".
 * `out` must hold 16 bytes. Returns `out`. */
char    *pm_ui_label(const PmState *s, unsigned track, unsigned per_track,
                     char *out);

#endif
