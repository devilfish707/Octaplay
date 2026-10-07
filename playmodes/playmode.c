/* PLAY MODES engine. See playmode.h. Original code, MIT (LICENSE). */
#include "playmode.h"

_Static_assert(sizeof(PmSettings) == 20, "settings layout");
_Static_assert(sizeof(PmTrack) == 12, "track layout");
_Static_assert(sizeof(PmState) == 20 + 12 * PM_TRACKS + 8, "state layout");

static const char *const NAMES[PM_MODES] = {
    "NORMAL", "REVERSED", "PINGPONG", "RANDOM", "SHUFFLE", "PINGPONG 2",
};

/* The order TRACK + UP / DOWN walks (stored numbers in list order). */
static const uint8_t ORDER[PM_MODES] = {
    PM_NORMAL, PM_REVERSE, PM_PINGPONG, PM_PINGPONG2, PM_RANDOM, PM_SHUFFLE,
};

/* A 32-bit integer mix (the lowbias32 constants). Multiplies and shifts
 * only: one mulu.l each on the ColdFire, no library call. */
static uint32_t mix(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Run r's seed for track t: a pure function, so the step stock prepares
 * while stopped can be computed for the run that PLAY is about to start. */
static uint32_t run_seed(const PmState *s, uint32_t run, unsigned track) {
    return mix(s->entropy ^ mix(run * 0x9e3779b9u + track * 0x85ebca6bu + 1u));
}

/* A keyed bijection on 0 .. 2^bits - 1. Each round is invertible on `bits`
 * bits: a multiply by an odd number, an add, and a right xorshift. */
static unsigned permute_pow2(unsigned x, unsigned bits, uint32_t key) {
    unsigned mask = (1u << bits) - 1u;
    unsigned shift = bits > 1 ? bits / 2 : 1;
    for (unsigned round = 0; round < 3; ++round) {
        uint32_t k = mix(key + round * 0x9e3779b9u);
        x = (x * ((k << 1) | 1u) + (k >> 8)) & mask;
        x ^= x >> shift;
    }
    return x & mask;
}

/* A keyed permutation of 0 .. len-1: walk the power-of-two permutation
 * until it lands inside the range. A permutation's cycles always return to
 * the start, and len is more than half the domain, so the walk is short
 * (two steps on average) and always ends. */
static unsigned shuffle_at(unsigned raw, unsigned len, uint32_t key) {
    unsigned bits = 0;
    while ((1u << bits) < len) ++bits;
    if (!bits) return 0;
    unsigned x = raw;
    do x = permute_pow2(x, bits, key); while (x >= len);
    return x;
}

unsigned pm_map(unsigned mode, unsigned len, uint32_t cycle, unsigned raw,
                uint32_t seed) {
    if (!len) return 0;
    if (len > PM_MAX_LEN) len = PM_MAX_LEN;
    if (raw >= len) {            /* look-ahead across the pattern end */
        cycle += raw / len;
        raw %= len;
    }
    switch (mode) {
    case PM_REVERSE:
        return len - 1 - raw;
    case PM_PINGPONG: {
        if (len == 1) return 0;
        /* One continuous bounce over the stock passes: position p in the
         * stream of steps, period 2*len - 2, the end steps played once. */
        uint32_t period = 2u * len - 2u;
        uint32_t p = ((cycle % period) * len + raw) % period;
        return p < len ? p : period - p;
    }
    case PM_PINGPONG2: {
        /* The same bounce, period 2*len: the end steps play twice
         * (1 .. 16 16 .. 1 1 .. 16). */
        uint32_t period = 2u * len;
        uint32_t p = ((cycle % period) * len + raw) % period;
        return p < len ? p : period - 1 - p;
    }
    case PM_RANDOM: {
        uint32_t h = mix(seed ^ mix(cycle * 0x85ebca6bu + raw * 0xc2b2ae35u + 1u));
        return ((h >> 16) * len) >> 16;      /* 16 x 7 bits: no overflow */
    }
    case PM_SHUFFLE:
        return shuffle_at(raw, len, mix(seed ^ (cycle * 0x27d4eb2fu)));
    default:
        return raw;
    }
}

void pm_init(PmState *s, uint32_t entropy) {
    uint8_t *bytes = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; ++i) bytes[i] = 0;
    s->entropy = entropy ? entropy : 0x6d2b79f5u;
    pm_reset_all(s);                       /* run 1 */
}

unsigned pm_mode(const PmState *s, unsigned track, unsigned per_track) {
    unsigned mode = per_track && track < PM_TRACKS ? s->settings.track[track]
                                                   : s->settings.global;
    return mode < PM_MODES ? mode : PM_NORMAL;
}

void pm_reset(PmState *s, unsigned track) {
    if (track >= PM_TRACKS) return;
    PmTrack *t = &s->tracks[track];
    t->cycle = 0;
    t->started = 0;
    t->last_raw = 0;
    t->seed = run_seed(s, s->run, track);
}

void pm_reset_all(PmState *s) {
    ++s->run;
    for (unsigned t = 0; t < PM_TRACKS; ++t) pm_reset(s, t);
}

unsigned pm_lookup_next_run(const PmState *s, unsigned track, unsigned raw,
                            unsigned len, unsigned per_track) {
    if (track >= PM_TRACKS) return raw;
    uint32_t seed = run_seed(s, s->run + 1, per_track ? track : 0);
    return pm_map(pm_mode(s, track, per_track), len, 0, raw, seed);
}

void pm_advance(PmState *s, unsigned track, unsigned raw, unsigned len) {
    if (track >= PM_TRACKS) return;
    PmTrack *t = &s->tracks[track];
    if (len && raw >= len) raw %= len;
    if (!t->started) {
        t->started = 1;
    } else if (raw == t->last_raw) {
        return;                   /* the same step asked about twice */
    } else if (raw < t->last_raw) {
        ++t->cycle;               /* the stock playhead wrapped: a pass */
    }
    t->last_raw = (uint8_t)raw;
}

unsigned pm_lookup(const PmState *s, unsigned track, unsigned raw,
                   unsigned len, unsigned per_track) {
    if (track >= PM_TRACKS) return raw;
    const PmTrack *t = &s->tracks[track];
    /* NORMAL scale mode: every track follows one sequence, so RANDOM and
     * SHUFFLE move all tracks together, as the shared mode suggests. */
    uint32_t seed = per_track ? t->seed : s->tracks[0].seed;
    return pm_map(pm_mode(s, track, per_track), len, t->cycle, raw, seed);
}

unsigned pm_ui_step(PmState *s, unsigned track, int delta, unsigned per_track) {
    uint8_t *slot = per_track && track < PM_TRACKS ? &s->settings.track[track]
                                                   : &s->settings.global;
    int at = 0;
    for (int k = 0; k < PM_MODES; ++k)
        if (ORDER[k] == *slot) at = k;
    at += delta < 0 ? -1 : delta > 0 ? 1 : 0;
    if (at < 0) at = 0;
    if (at >= PM_MODES) at = PM_MODES - 1;
    *slot = ORDER[at];
    return ORDER[at];
}

char *pm_ui_label(const PmState *s, unsigned track, unsigned per_track,
                  char *out) {
    unsigned n = 0;
    if (per_track && track < PM_TRACKS) {
        out[n++] = track < 8 ? 'T' : 'M';
        out[n++] = (char)('1' + track % 8);
    } else {
        out[n++] = 'A'; out[n++] = 'L'; out[n++] = 'L';
    }
    out[n++] = ' ';
    for (const char *p = NAMES[pm_mode(s, track, per_track)]; *p; ++p)
        out[n++] = *p;
    out[n] = 0;
    return out;
}
