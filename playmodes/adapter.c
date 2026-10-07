/* PLAY MODES -- the firmware side: reads the sequencer's own state, keeps
 * the engine's state in loader-owned DRAM and the modes in battery RAM,
 * reads and writes the project file's line, and is what the detours call.
 * Every address is cited here or in INVESTIGATION.md; MIDI_LENGTH is still
 * inferred.
 *
 * Compiled with -DPM_HOST, the firmware addresses become an array so the
 * glue can be tested on the host (test_adapter.c). */
#include "playmode.h"

#ifdef PM_HOST
volatile uint8_t *pm_host_addr(uint32_t address);
#define ADDR(a) pm_host_addr((uint32_t)(a))
#else
#define ADDR(a) ((volatile uint8_t *)(uintptr_t)(a))
#endif
#define U8(a)  (*(volatile uint8_t *)ADDR(a))
#define U32(a) (*(volatile uint32_t *)ADDR(a))

/* --- the sequencer's own state (sources in INVESTIGATION.md "Anchors") --- */
enum {
    SEQ_TRANSPORT  = 0x800065b8u, /* 0 stopped, 1 playing (euclid, emu_frames.py) */
    SEQ_BANK       = 0x800065bdu, /* playing bank (direct-jump README)            */
    SEQ_PATTERN    = 0x800065beu, /* playing pattern                              */
    BANK_BLOB      = 0x400e21e0u, /* bank 0 in RAM (STEP_LOCKS.md section 1)      */
    BANK_STRIDE    = 0x9b340u,
    PATTERN_STRIDE = 0x8ed8u,
    AUDIO_TRACK    = 0x91au,      /* audio track record stride                    */
    MIDI_TRACKS    = 0x48d0u,     /* MIDI track records: pattern + 0x48d0 + t*0x8b0 */
    MIDI_TRACK     = 0x8b0u,
    PAT_MASTER_LEN = 0x8e50u,     /* PER TRACK MASTER LENGTH, signed big-endian word,
                                     -1 = INF (DIRECT JUMP's direct_jump.s)     */
    PAT_LENGTH     = 0x8e53u,     /* pattern length / scale / scale mode (euclid)  */
    PAT_SCALE      = 0x8e54u,     /* the (master) scale index                      */
    PAT_SCALE_MODE = 0x8e55u,
    TRK_LENGTH     = 0x50u,       /* audio track length in PER TRACK (euclid)      */
    TRK_SCALE      = 0x51u,       /* audio track scale index in PER TRACK (euclid) */
    MIDI_SCALE     = 0x29u,       /* MIDI track scale (Kyoti NOTES, "+0x48f9")    */
    MIDI_LENGTH    = 0x28u,       /* PENDING: inferred from the MIDI scale byte at
                                     +0x29 (Kyoti NOTES, "+0x48f9"), audio's
                                     length sits one byte below its scale        */
    UI_FRAME_CLOCK = 0x46104cf0u, /* free-running; entropy only (euclid)          */
    NV_MODES       = 0x100b14e2u, /* battery RAM, 9 bytes (to 0x100b14ea): the
                                     linker's padding before the project record
                                     0x100b14f0, no stock reference; the
                                     quantizer holds 0x100b14ec..ee (its README) */

};

/* Loader-owned DRAM, zeroed at boot (hooks.s). Not the 0x80006a40 scratch
 * block: that sits in the DSP shared window and does not survive live audio
 * on hardware (Kyoti quantize-live-rec-toggle, Session 95). */
extern PmState pm_state;
extern uint8_t pm_ready, pm_last_transport, pm_last_bank, pm_last_pattern;
extern uint32_t pm_restart;      /* set by the four transport-start stubs */
extern char pm_toast[16];

static volatile uint8_t *playing_pattern(void) {
    return ADDR(BANK_BLOB + U8(SEQ_BANK) * BANK_STRIDE
                + U8(SEQ_PATTERN) * PATTERN_STRIDE);
}

unsigned pm_per_track(void) {
    return playing_pattern()[PAT_SCALE_MODE] != 0;
}

/* The track's length in steps, as the sequencer plays it. Tracks 8..15 are
 * the MIDI tracks. */
/* Ticks per step for scale index 0..6 (2X .. 1/8X): stock's 0x400aba50,
 * as EUCLID reads it. */
static unsigned ticks_per_step(unsigned scale) {
    static const uint8_t ticks[7] = {3, 4, 6, 8, 12, 24, 48};
    return ticks[scale < 7 ? scale : 2];
}

/* The steps a track really plays before the sequencer starts it over.
 * PER TRACK: its own length, unless MASTER LENGTH restarts every track
 * sooner; then the steps it reaches by then (master steps at the master
 * scale, counted in the track's own scale, a partly reached step included).
 * So REVERSED mirrors what NORMAL plays, also when MASTER LENGTH cuts in. */
unsigned pm_track_length(unsigned track) {
    volatile uint8_t *pattern = playing_pattern();
    if (!pattern[PAT_SCALE_MODE]) {
        unsigned len = pattern[PAT_LENGTH];
        return len && len <= PM_MAX_LEN ? len : 16;
    }
    volatile uint8_t *rec = track < 8 ? pattern + track * AUDIO_TRACK
                                      : pattern + MIDI_TRACKS + (track - 8) * MIDI_TRACK;
    unsigned len = rec[track < 8 ? TRK_LENGTH : MIDI_LENGTH];
    unsigned scale = rec[track < 8 ? TRK_SCALE : MIDI_SCALE];
    if (!len || len > PM_MAX_LEN) len = 16;
    int16_t master = (int16_t)((pattern[PAT_MASTER_LEN] << 8) | pattern[PAT_MASTER_LEN + 1]);
    if (master > 0) {
        uint32_t master_ticks = (uint32_t)master * ticks_per_step(pattern[PAT_SCALE]);
        unsigned per_step = ticks_per_step(scale);
        uint32_t reached = (master_ticks + per_step - 1) / per_step;
        if (reached < len) len = reached ? reached : 1;
    }
    return len;
}

/* Fresh passes on PLAY and on a pattern switch. The tick only runs while
 * playing, so it never sees the transport stopped; the four places that set
 * it playing (0x4009c3da PLAY, 0x400a2210 / 0x400a24d6 / 0x400a27e8 in the
 * tick for external start and continue) raise pm_restart through hooks.s.
 * (0x46c775ce, the run's start time, is no use: 0x400a3f76 rewrites it
 * during playback, which on the unit restarted pingpong on every step.) */
/* --- the modes in battery RAM ---------------------------------------------
 * A power cycle reads no project file: the unit comes back from its battery
 * RAM (CS1 0x10000000..; cold boot clears it all), as stock CHAIN AFTER does
 * (the quantizer README, "Where the setting lives"). So the 17 modes live
 * there as nibbles, global first, then T1..T8, M1..M8; DRAM holds the
 * working copy. Out-of-range nibbles read as NORMAL. */
#define PM_NV_MODES (1 + PM_TRACKS)
static uint8_t *mode_slot(unsigned i) {
    return i ? &pm_state.settings.track[i - 1] : &pm_state.settings.global;
}

static void pm_nv_load(void) {
    for (unsigned i = 0; i < PM_NV_MODES; ++i) {
        unsigned nib = (U8(NV_MODES + i / 2) >> (i & 1 ? 0 : 4)) & 15;
        *mode_slot(i) = (uint8_t)(nib < PM_MODES ? nib : PM_NORMAL);
    }
}

static void pm_nv_store(void) {
    for (unsigned b = 0; b < (PM_NV_MODES + 1) / 2; ++b) {
        unsigned hi = *mode_slot(2 * b) & 15;
        unsigned lo = 2 * b + 1 < PM_NV_MODES ? *mode_slot(2 * b + 1) & 15 : 0;
        uint8_t v = (uint8_t)(hi << 4 | lo);
        if (U8(NV_MODES + b) != v) U8(NV_MODES + b) = v;
    }
}

static void pm_ensure(void) {
    if (!pm_ready) {
        pm_init(&pm_state, U32(UI_FRAME_CLOCK));
        pm_nv_load();
        pm_ready = 1;
    }
}

static void pm_sync(void) {
    pm_ensure();
    unsigned transport = U32(SEQ_TRANSPORT) == 1;
    unsigned bank = U8(SEQ_BANK), pattern = U8(SEQ_PATTERN);
    if (pm_restart || (transport && !pm_last_transport)
        || bank != pm_last_bank || pattern != pm_last_pattern) {
        pm_restart = 0;
        pm_reset_all(&pm_state);
    }
    pm_last_transport = (uint8_t)transport;
    pm_last_bank = (uint8_t)bank;
    pm_last_pattern = (uint8_t)pattern;
}

/* The sequencer detour calls this once per track per step, with the step
 * the stock playhead has just reached, and plays the step it returns: its
 * trig bits, its locks (0x4009d1e8's `step` argument), its conditions. */
unsigned pm_seq_step(unsigned track, unsigned raw) {
    pm_sync();
    unsigned len = pm_track_length(track);
    pm_advance(&pm_state, track, raw, len);
    return pm_lookup(&pm_state, track, raw, len, pm_per_track());
}

static unsigned pm_playing(void) { return U32(SEQ_TRANSPORT) == 1; }

/* Look-ahead readers and the rebuild 0x4009da20 ask about a step that has
 * not been reached; `raw` may equal the length (the next pass). Changes
 * nothing. While stopped, stock is preparing the first step of the next
 * run (it does so at STOP and after every edit), so the answer is the next
 * run's: first pass, its seeds. Otherwise a stale preparation fires at PLAY
 * (on the unit: NORMAL, STOP, switch to REVERSED, PLAY -> step 1's trig at
 * step 16's place, once). */
unsigned pm_seq_peek(unsigned track, unsigned raw) {
    pm_ensure();
    unsigned len = pm_track_length(track);
    if (!pm_playing())
        return pm_lookup_next_run(&pm_state, track, raw, len, pm_per_track());
    return pm_lookup(&pm_state, track, raw, len, pm_per_track());
}

#define PM_MASTER 0xffffffffu
unsigned pm_show(unsigned track, unsigned raw) {
    pm_ensure();
    if (track == PM_MASTER) {
        /* The master step (stock asks with track -1): under NORMAL scale
         * mode every track plays it, so show track 0's mapping. */
        if (pm_per_track()) return raw;
        track = 0;
    }
    if (track >= PM_TRACKS) return raw;
    unsigned len = pm_track_length(track);
    if (!pm_playing())
        return pm_lookup_next_run(&pm_state, track, len ? raw % len : raw, len,
                                  pm_per_track());
    const PmTrack *t = &pm_state.tracks[track];
    uint32_t cycle = t->cycle;
    if (t->started && raw > t->last_raw && cycle) --cycle;
    unsigned per_track = pm_per_track();
    return pm_map(pm_mode(&pm_state, track, per_track), len, cycle,
                  len ? raw % len : raw,
                  per_track ? t->seed : pm_state.tracks[0].seed);
}

/* TRACK held + UP (delta -1) / DOWN (+1), from the key handler: change the
 * mode and write the popup text into pm_toast; hooks.s then has stock
 * rebuild its prepared steps (0x4009da20(-1), as after an edit), so the
 * step it prepared in the old mode does not fire, which hooks.s hands to the
 * stock toast 0x4005a2b8(text, dur). (No pointer return: m68k ELF may
 * return pointers in a0, and the asm reads pm_toast itself.)
 * The toast is only ever opened from the key handler's context: opening it
 * from the frame or tick path hard-crashed a real MKI (Kyoti, Session 93). */
void pm_key_updown(unsigned track, int delta) {
    pm_ensure();
    unsigned per_track = pm_per_track();
    if (delta) {
        pm_ui_step(&pm_state, track, delta, per_track);
        pm_nv_store();
    }
    pm_ui_label(&pm_state, track, per_track, pm_toast);
}

/* --- the project file ------------------------------------------------------
 * project.work is text, KEY=value lines; the loader 0x400866c4 skips every
 * line that starts with '#', on stock firmware too, so a project saved here
 * still loads on a stock OS. One line, after the stock settings the writer
 * 0x400882a2 prints before PATTERN_CHANGE_AUTO_SILENCE_TRACKS:
 *
 *     #PLAY_MODES=00000000000000000
 *
 * a digit per mode (0 NORMAL .. 4 SHUFFLE): the shared one, T1..T8, M1..M8.
 * SAVE writes project.work and copies it to project.strd; RELOAD copies it
 * back; PROJECT > CHANGE first writes the working state, then loads (the
 * quantizer README measured all of it), so the line follows the project
 * everywhere the stock settings go. Only the modes: every track still
 * starts from its first step. */
static const char pm_key[] = "#PLAY_MODES=";
#define PM_KEY_LEN (sizeof pm_key - 1)

/* The line, CR LF ended like stock's, into `out` (32 bytes: 12 + 17 + CR LF + NUL);
 * returns its length without the NUL. */
unsigned pm_project_format(char *out) {
    pm_ensure();
    unsigned n = 0;
    for (; n < PM_KEY_LEN; ++n) out[n] = pm_key[n];
    for (unsigned i = 0; i < PM_NV_MODES; ++i) {
        unsigned m = *mode_slot(i);
        out[n++] = (char)('0' + (m < PM_MODES ? m : PM_NORMAL));
    }
    out[n++] = '\r';
    out[n++] = '\n';
    out[n] = 0;
    return n;
}

/* The loader's entry, once per load pass: `storing` is its second argument
 * (0 = the parse-only pass). A storing pass starts from NORMAL, so a project
 * saved without the line (or on stock firmware) loads as NORMAL. */
void pm_project_begin(unsigned storing) {
    pm_ensure();
    if (!storing) return;
    for (unsigned i = 0; i < PM_NV_MODES; ++i) *mode_slot(i) = PM_NORMAL;
    pm_nv_store();
}

/* A '#' line (the loader's comment branch). Ours sets the modes on a
 * storing pass (`parse_only` = the loader's own flag at 58(sp), nonzero on
 * the parse-only pass); every '#' line is then skipped as stock skips it.
 * A short line or a bad digit leaves that mode NORMAL. Returns 1 if the
 * line was ours. */
unsigned pm_project_line(const char *line, unsigned parse_only) {
    for (unsigned k = 0; k < PM_KEY_LEN; ++k)
        if (line[k] != pm_key[k]) return 0;
    if (parse_only) return 1;
    pm_ensure();
    const char *p = line + PM_KEY_LEN;
    for (unsigned i = 0; i < PM_NV_MODES; ++i) {
        unsigned d = (unsigned)(unsigned char)*p - '0';
        if (d > 9) break;               /* the end of the digits */
        *mode_slot(i) = (uint8_t)(d < PM_MODES ? d : PM_NORMAL);
        ++p;
    }
    pm_nv_store();
    return 1;
}
