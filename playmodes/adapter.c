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
    NV_A           = 0x100ffe00u, /* battery RAM, 256 bytes to 0x100fff00 and   */
    NV_A_LEN       = 0x100u,
    NV_B           = 0x100f859cu, /* 100 bytes to 0x100f8600: stock references
                                     nothing in 0x100f859c..0x100fff00
                                     (STEP_LOCKS.md 6); PLOCKS P2 holds
                                     0x100f8600..0x100ffe00                     */
    NV_B_LEN       = 0x64u,

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
/* --- the modes, per pattern ----------------------------------------------
 * Every pattern of every bank has its own 17 modes (the shared one, T1..T8,
 * M1..M8): pm_table[bank * 16 + pattern], loader-owned DRAM. The engine
 * works on pm_state.settings, a copy of the playing pattern's row: copied in
 * when the playing pattern changes, written back when a key changes it.
 * (The pattern record itself has no room the bank file keeps: the bank
 * serializer 0x4008a6fc writes only 0x8e50..0x8e5b of its tail; probe 5.) */
#define PM_SLOTS (1 + PM_TRACKS)
#define PM_PATTERNS 256
extern uint8_t pm_table[PM_PATTERNS][PM_SLOTS];
extern uint16_t pm_cur;          /* the row in pm_state.settings, + 1; 0 = none */

static uint8_t *mode_slot(unsigned i) {
    return i ? &pm_state.settings.track[i - 1] : &pm_state.settings.global;
}

static unsigned row_is_normal(const uint8_t *row) {
    for (unsigned i = 0; i < PM_SLOTS; ++i) if (row[i]) return 0;
    return 1;
}

/* --- over a power-off: battery RAM ------------------------------------------
 * A power cycle reads no project file: the unit comes back from battery RAM
 * (CS1 0x10000000..; a cold boot clears it), as stock CHAIN AFTER does
 * (the quantizer README, "Where the setting lives"). The table does not fit
 * the free bytes, so only the patterns with a mode other than NORMAL are
 * kept, in 356 bytes over two free ranges read as one stream:
 *   'P' 'M' len_hi len_lo sum, then per pattern: its index (bank << 4 |
 *   pattern), a 17-bit mask of the slots not NORMAL (3 bytes, big-endian)
 *   and 3 bits per such slot, packed from the top bit.
 * Simple patterns take 5 bytes (about 70 fit), fully per-track ones 11
 * (about 30). Patterns beyond that are dropped from this copy (not from the
 * table or the project file); a bad header reads as all NORMAL. */
#define NV_LEN (NV_A_LEN + NV_B_LEN)
#define NV_HEAD 5
static volatile uint8_t *nv_byte(unsigned i) {
    return i < NV_A_LEN ? ADDR(NV_A + i) : ADDR(NV_B + (i - NV_A_LEN));
}

static void nv_put(unsigned i, uint8_t v) {
    if (*nv_byte(i) != v) *nv_byte(i) = v;
}

static void pm_nv_store(void) {
    unsigned at = NV_HEAD, sum = 0;
    for (unsigned p = 0; p < PM_PATTERNS; ++p) {
        const uint8_t *row = pm_table[p];
        if (row_is_normal(row)) continue;
        uint32_t mask = 0;
        unsigned count = 0;
        for (unsigned i = 0; i < PM_SLOTS; ++i)
            if (row[i]) { mask |= 1u << i; ++count; }
        unsigned size = 4 + (3 * count + 7) / 8;
        if (at + size > NV_LEN) continue;            /* full: try smaller ones */
        uint8_t entry[4 + 7] = {0};
        entry[0] = (uint8_t)p;
        entry[1] = (uint8_t)(mask >> 16);
        entry[2] = (uint8_t)(mask >> 8);
        entry[3] = (uint8_t)mask;
        unsigned bit = 0;
        for (unsigned i = 0; i < PM_SLOTS; ++i) {
            if (!row[i]) continue;
            for (unsigned b = 0; b < 3; ++b, ++bit)
                if (row[i] & (4u >> b)) entry[4 + bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
        }
        for (unsigned k = 0; k < size; ++k) { nv_put(at + k, entry[k]); sum += entry[k]; }
        at += size;
    }
    unsigned len = at - NV_HEAD;
    nv_put(2, (uint8_t)(len >> 8));
    nv_put(3, (uint8_t)len);
    nv_put(4, (uint8_t)sum);
    nv_put(0, 'P');
    nv_put(1, 'M');
}

static void pm_nv_load(void) {
    for (unsigned p = 0; p < PM_PATTERNS; ++p)
        for (unsigned i = 0; i < PM_SLOTS; ++i) pm_table[p][i] = PM_NORMAL;
    if (*nv_byte(0) != 'P' || *nv_byte(1) != 'M') return;
    unsigned len = (unsigned)*nv_byte(2) << 8 | *nv_byte(3);
    if (len > NV_LEN - NV_HEAD) return;
    unsigned sum = 0;
    for (unsigned k = 0; k < len; ++k) sum += *nv_byte(NV_HEAD + k);
    if ((uint8_t)sum != *nv_byte(4)) return;
    unsigned at = NV_HEAD, end = NV_HEAD + len;
    while (at + 4 <= end) {
        unsigned p = *nv_byte(at);
        uint32_t mask = (uint32_t)*nv_byte(at + 1) << 16 | (uint32_t)*nv_byte(at + 2) << 8
                      | *nv_byte(at + 3);
        unsigned count = 0;
        for (unsigned i = 0; i < PM_SLOTS; ++i) count += (mask >> i) & 1;
        unsigned size = 4 + (3 * count + 7) / 8;
        if (mask >> PM_SLOTS || at + size > end) break;     /* damaged: stop */
        unsigned bit = 0;
        for (unsigned i = 0; i < PM_SLOTS; ++i) {
            if (!((mask >> i) & 1)) continue;
            unsigned v = 0;
            for (unsigned b = 0; b < 3; ++b, ++bit)
                v = v << 1 | ((*nv_byte(at + 4 + bit / 8) >> (7 - bit % 8)) & 1);
            pm_table[p][i] = (uint8_t)(v < PM_MODES ? v : PM_NORMAL);
        }
        at += size;
    }
}

/* The playing pattern's row into pm_state.settings, when it changed. */
static void pm_load_current(void) {
    unsigned bank = U8(SEQ_BANK), pattern = U8(SEQ_PATTERN);
    if (bank > 15 || pattern > 15) return;
    unsigned row = bank * 16 + pattern;
    if (pm_cur == row + 1) return;
    for (unsigned i = 0; i < PM_SLOTS; ++i) *mode_slot(i) = pm_table[row][i];
    pm_cur = (uint16_t)(row + 1);
}

/* After a key change: the settings back into the pattern's row. */
static void pm_store_current(void) {
    if (!pm_cur) return;
    for (unsigned i = 0; i < PM_SLOTS; ++i) pm_table[pm_cur - 1][i] = *mode_slot(i);
    pm_nv_store();
}

static void pm_ensure(void) {
    if (!pm_ready) {
        pm_init(&pm_state, U32(UI_FRAME_CLOCK));
        pm_nv_load();
        pm_cur = 0;
        pm_ready = 1;
    }
    pm_load_current();
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
        pm_store_current();
    }
    pm_ui_label(&pm_state, track, per_track, pm_toast);
}

/* --- the project file ------------------------------------------------------
 * project.work is text, KEY=value lines; the loader 0x400866c4 skips every
 * line that starts with '#', on stock firmware too, so a project saved here
 * still loads on a stock OS. One line per pattern that has a mode other than
 * NORMAL, after the stock settings the writer 0x400882a2 prints before
 * PATTERN_CHANGE_AUTO_SILENCE_TRACKS:
 *
 *     #PLAY_MODES=A01:10000000000000000
 *
 * the bank (A..P) and pattern (01..16), then a digit per mode (0 NORMAL ..
 * 4 SHUFFLE): the shared one, T1..T8, M1..M8. SAVE writes project.work and
 * copies it to project.strd; RELOAD copies it back; PROJECT > CHANGE first
 * writes the working state, then loads (the quantizer README measured all
 * of it), so the lines follow the project everywhere the stock settings go.
 * Only the modes: every track still starts from its first step.
 * Build 17's single line (17 digits, no pattern) is read as every pattern's. */
static const char pm_key[] = "#PLAY_MODES=";
#define PM_KEY_LEN (sizeof pm_key - 1)

/* Pattern `index`'s line (bank * 16 + pattern), CR LF ended like stock's,
 * into `out` (36 bytes: 12 + 4 + 17 + CR LF + NUL); returns its length
 * without the NUL, 0 for a pattern that is all NORMAL (no line). */
unsigned pm_project_format(char *out, unsigned index) {
    pm_ensure();
    if (index >= PM_PATTERNS) return 0;
    if (pm_cur == index + 1) pm_store_current();    /* the live copy is the row */
    const uint8_t *row = pm_table[index];
    if (row_is_normal(row)) return 0;
    unsigned n = 0;
    for (; n < PM_KEY_LEN; ++n) out[n] = pm_key[n];
    unsigned pattern = index % 16 + 1;
    out[n++] = (char)('A' + index / 16);
    out[n++] = (char)('0' + pattern / 10);
    out[n++] = (char)('0' + pattern % 10);
    out[n++] = ':';
    for (unsigned i = 0; i < PM_SLOTS; ++i)
        out[n++] = (char)('0' + (row[i] < PM_MODES ? row[i] : PM_NORMAL));
    out[n++] = '\r';
    out[n++] = '\n';
    out[n] = 0;
    return n;
}

/* The loader's entry, once per load pass: `storing` is its second argument
 * (0 = the parse-only pass). A storing pass starts from all NORMAL, so a
 * project saved without the lines (or on stock firmware) loads as NORMAL. */
void pm_project_begin(unsigned storing) {
    pm_ensure();
    if (!storing) return;
    for (unsigned p = 0; p < PM_PATTERNS; ++p)
        for (unsigned i = 0; i < PM_SLOTS; ++i) pm_table[p][i] = PM_NORMAL;
    pm_cur = 0;
    pm_nv_store();
}

static void parse_digits(const char *p, uint8_t *row) {
    for (unsigned i = 0; i < PM_SLOTS; ++i) {
        unsigned d = (unsigned)(unsigned char)*p - '0';
        if (d > 9) break;               /* the end of the digits */
        row[i] = (uint8_t)(d < PM_MODES ? d : PM_NORMAL);
        ++p;
    }
}

/* A '#' line (the loader's comment branch). Ours sets a pattern's modes on
 * a storing pass (`parse_only` = the loader's own flag at 58(sp), nonzero
 * on the parse-only pass); every '#' line is then skipped as stock skips
 * it. A short line or a bad digit leaves that mode NORMAL; a bad pattern
 * name skips the line. Returns 1 if the line was ours. */
unsigned pm_project_line(const char *line, unsigned parse_only) {
    for (unsigned k = 0; k < PM_KEY_LEN; ++k)
        if (line[k] != pm_key[k]) return 0;
    if (parse_only) return 1;
    pm_ensure();
    const char *p = line + PM_KEY_LEN;
    if (p[0] >= 'A' && p[0] <= 'P') {
        unsigned tens = (unsigned)(unsigned char)p[1] - '0';
        unsigned ones = (unsigned)(unsigned char)p[2] - '0';
        unsigned pattern = tens * 10 + ones;
        if (tens > 9 || ones > 9 || pattern < 1 || pattern > 16 || p[3] != ':') return 1;
        parse_digits(p + 4, pm_table[(unsigned)(p[0] - 'A') * 16 + pattern - 1]);
    } else {                            /* build 17: one set for every pattern */
        uint8_t row[PM_SLOTS] = {0};
        parse_digits(p, row);
        for (unsigned q = 0; q < PM_PATTERNS; ++q)
            for (unsigned i = 0; i < PM_SLOTS; ++i) pm_table[q][i] = row[i];
    }
    pm_cur = 0;                         /* the playing pattern re-reads its row */
    pm_nv_store();
    return 1;
}
