/* Host tests for the PLAY MODES engine (run by verify.py). No firmware. */
#include <stdio.h>
#include <string.h>
#include "playmode.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Drive the engine the way the firmware adapter will: one pm_advance per
 * stock step, then the lookup for that step. Returns the steps played. */
static void play(PmState *s, unsigned track, unsigned len, unsigned steps,
                 unsigned per_track, unsigned *out) {
    for (unsigned i = 0; i < steps; ++i) {
        unsigned raw = i % len;
        pm_advance(s, track, raw, len);
        out[i] = pm_lookup(s, track, raw, len, per_track);
    }
}

static void test_normal_reverse(void) {
    PmState s; pm_init(&s, 1);
    unsigned out[200];
    for (unsigned len = 1; len <= 64; ++len) {
        s.settings.global = PM_NORMAL; pm_reset_all(&s);
        play(&s, 0, len, 3 * len, 0, out);
        for (unsigned i = 0; i < 3 * len; ++i)
            CHECK(out[i] == i % len, "normal len %u step %u -> %u", len, i, out[i]);
        s.settings.global = PM_REVERSE; pm_reset_all(&s);
        play(&s, 0, len, 3 * len, 0, out);
        for (unsigned i = 0; i < 3 * len; ++i)
            CHECK(out[i] == len - 1 - i % len, "reverse len %u step %u -> %u", len, i, out[i]);
    }
}

static void test_pingpong(void) {
    PmState s; pm_init(&s, 2);
    s.settings.global = PM_PINGPONG;
    unsigned out[400];
    static const unsigned four[] = {0,1,2,3,2,1,0,1,2,3,2,1,0,1,2,3};
    play(&s, 0, 4, 16, 0, out);
    CHECK(memcmp(out, four, sizeof four) == 0, "pingpong len 4 sequence");
    pm_reset_all(&s); play(&s, 0, 1, 5, 0, out);
    for (unsigned i = 0; i < 5; ++i) CHECK(out[i] == 0, "pingpong len 1");
    pm_reset_all(&s); play(&s, 0, 2, 6, 0, out);
    for (unsigned i = 0; i < 6; ++i) CHECK(out[i] == i % 2, "pingpong len 2 step %u", i);
    /* Every length: consecutive steps differ by exactly one and the ends
     * are visited once per bounce (no doubled 16 or 1). */
    for (unsigned len = 3; len <= 64; ++len) {
        pm_reset_all(&s); play(&s, 0, len, 6 * len, 0, out);
        CHECK(out[0] == 0, "pingpong starts on step 1");
        for (unsigned i = 1; i < 6 * len; ++i) {
            int d = (int)out[i] - (int)out[i - 1];
            CHECK(d == 1 || d == -1, "pingpong len %u jump at %u: %u -> %u", len, i, out[i-1], out[i]);
        }
    }
}

static void test_shuffle(void) {
    PmState s; pm_init(&s, 3);
    s.settings.global = PM_SHUFFLE;
    unsigned out[64 * 8];
    for (unsigned len = 1; len <= 64; ++len) {
        for (unsigned seedrun = 0; seedrun < 4; ++seedrun) {
            pm_reset_all(&s);
            play(&s, 0, len, 8 * len, 0, out);
            unsigned identical_passes = 0;
            for (unsigned pass = 0; pass < 8; ++pass) {
                unsigned seen[64] = {0};
                for (unsigned i = 0; i < len; ++i) {
                    unsigned v = out[pass * len + i];
                    CHECK(v < len, "shuffle out of range");
                    if (v < len) ++seen[v];
                }
                for (unsigned v = 0; v < len; ++v)
                    CHECK(seen[v] == 1, "shuffle len %u pass %u: step %u played %u times", len, pass, v, seen[v]);
                if (pass && memcmp(&out[pass * len], &out[(pass - 1) * len], len * sizeof *out) == 0)
                    ++identical_passes;
            }
            if (len >= 4)
                CHECK(identical_passes < 2, "shuffle len %u repeats its order", len);
        }
    }
}

static void test_random(void) {
    PmState s; pm_init(&s, 4);
    s.settings.global = PM_RANDOM;
    enum { LEN = 16, STEPS = 16 * 4000 };
    static unsigned out[STEPS];
    play(&s, 0, LEN, STEPS, 0, out);
    unsigned count[LEN] = {0}, repeats = 0;
    for (unsigned i = 0; i < STEPS; ++i) {
        CHECK(out[i] < LEN, "random out of range");
        ++count[out[i] % LEN];
        if (i && out[i] == out[i - 1]) ++repeats;
    }
    for (unsigned v = 0; v < LEN; ++v)   /* expected 4000 each */
        CHECK(count[v] > 3600 && count[v] < 4400, "random bias: step %u played %u times", v, count[v]);
    CHECK(repeats > 0, "random never repeats a step (it should be allowed to)");
    /* Different resets draw different sequences. */
    static unsigned again[64];
    pm_reset_all(&s); play(&s, 0, LEN, 64, 0, again);
    CHECK(memcmp(again, out, sizeof again) != 0, "random repeats its sequence after a reset");
}

/* The look-ahead (micro-timing, the tick-2 pre-check) must name the step
 * that then really plays, for every mode, including across the pattern end. */
static void test_lookahead(void) {
    for (unsigned mode = 0; mode < PM_MODES; ++mode) {
        PmState s; pm_init(&s, 5 + mode);
        s.settings.global = (uint8_t)mode;
        unsigned len = 13;
        unsigned predicted = 0;
        for (unsigned i = 0; i < 10 * len; ++i) {
            unsigned raw = i % len;
            pm_advance(&s, 0, raw, len);
            unsigned now = pm_lookup(&s, 0, raw, len, 0);
            if (i) CHECK(now == predicted, "mode %u step %u: look-ahead said %u, played %u", mode, i, predicted, now);
            CHECK(pm_lookup(&s, 0, raw, len, 0) == now, "mode %u: lookup is not repeatable", mode);
            predicted = pm_lookup(&s, 0, raw + 1, len, 0);
        }
    }
}

static void test_advance(void) {
    PmState s; pm_init(&s, 6);
    pm_advance(&s, 2, 0, 16);
    pm_advance(&s, 2, 0, 16);           /* asked twice: no pass */
    CHECK(s.tracks[2].cycle == 0, "duplicate advance counted a pass");
    for (unsigned r = 1; r < 16; ++r) pm_advance(&s, 2, r, 16);
    pm_advance(&s, 2, 0, 16);
    CHECK(s.tracks[2].cycle == 1, "wrap not counted");
    pm_advance(&s, 2, 5, 16);
    pm_advance(&s, 2, 3, 16);           /* a jump backwards also restarts */
    CHECK(s.tracks[2].cycle == 2, "backward jump not counted");
    pm_reset(&s, 2);
    CHECK(s.tracks[2].cycle == 0 && !s.tracks[2].started, "reset");
    CHECK(s.tracks[3].cycle == 0, "other tracks untouched");
}

static void test_pingpong2(void) {
    /* Exact sequences: the end steps twice, continuous across passes. */
    static const unsigned want4[] = {0, 1, 2, 3, 3, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 0};
    for (unsigned i = 0; i < 16; ++i)
        CHECK(pm_map(PM_PINGPONG2, 4, i / 4, i % 4, 0) == want4[i], "len 4 step %u", i);
    for (unsigned i = 0; i < 6; ++i)
        CHECK(pm_map(PM_PINGPONG2, 1, i, 0, 0) == 0, "len 1");
    for (unsigned len = 2; len <= 64; ++len) {
        unsigned prev = 0, same = 0;
        for (unsigned i = 0; i < 6 * len; ++i) {
            unsigned now = pm_map(PM_PINGPONG2, len, i / len, i % len, 0);
            CHECK(now < len, "len %u in range", len);
            if (i) {
                int d = (int)now - (int)prev;
                CHECK(d >= -1 && d <= 1, "len %u: moves by one", len);
                if (!d) { ++same; CHECK(now == 0 || now == len - 1, "len %u: repeats only at the ends", len); }
            }
            prev = now;
        }
        CHECK(same == 6 - 1, "len %u: each turn repeats its end step (%u)", len, same);
    }
}

static void test_ui(void) {
    PmState s; pm_init(&s, 7);
    char label[16];
    /* NORMAL scale mode: one shared setting, whichever TRACK key is held. */
    CHECK(pm_ui_step(&s, 2, +1, 0) == PM_REVERSE, "down from NORMAL");
    CHECK(pm_mode(&s, 0, 0) == PM_REVERSE && pm_mode(&s, 7, 0) == PM_REVERSE, "shared mode");
    CHECK(strcmp(pm_ui_label(&s, 5, 0, label), "ALL REVERSED") == 0, "label %s", label);
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_PINGPONG, "then PINGPONG");
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_PINGPONG2, "then PINGPONG 2");
    CHECK(strcmp(pm_ui_label(&s, 5, 0, label), "ALL PINGPONG 2") == 0, "label %s", label);
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_RANDOM, "then RANDOM");
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_SHUFFLE, "then SHUFFLE");
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_REPEAT, "then REPEAT");
    CHECK(strcmp(pm_ui_label(&s, 5, 0, label), "ALL REPEAT") == 0, "label %s", label);
    CHECK(pm_ui_step(&s, 0, +1, 0) == PM_REPEAT, "clamps at REPEAT");
    CHECK(pm_ui_step(&s, 0, -1, 0) == PM_SHUFFLE && pm_ui_step(&s, 0, -1, 0) == PM_RANDOM
          && pm_ui_step(&s, 0, -1, 0) == PM_PINGPONG2, "UP walks back through PINGPONG 2");
    for (int i = 0; i < 9; ++i) pm_ui_step(&s, 0, -1, 0);
    CHECK(s.settings.global == PM_NORMAL, "clamps at NORMAL");
    /* PER TRACK: each track its own, and the shared one is kept. */
    pm_ui_step(&s, 0, +1, 0);                      /* shared: REVERSED */
    pm_ui_step(&s, 2, +1, 1); pm_ui_step(&s, 2, +1, 1);  /* T3: PINGPONG */
    pm_ui_step(&s, 9, +1, 1);                      /* M2: REVERSED */
    CHECK(pm_mode(&s, 2, 1) == PM_PINGPONG, "T3 own mode");
    CHECK(pm_mode(&s, 3, 1) == PM_NORMAL, "T4 untouched");
    CHECK(pm_mode(&s, 9, 1) == PM_REVERSE, "M2 own mode");
    CHECK(pm_mode(&s, 2, 0) == PM_REVERSE, "back to NORMAL scale: shared mode again");
    CHECK(strcmp(pm_ui_label(&s, 2, 1, label), "T3 PINGPONG") == 0, "label %s", label);
    CHECK(strcmp(pm_ui_label(&s, 9, 1, label), "M2 REVERSED") == 0, "label %s", label);
    s.settings.track[4] = 200;                     /* garbage reads as NORMAL */
    CHECK(pm_mode(&s, 4, 1) == PM_NORMAL, "invalid stored mode");
    pm_ui_step(&s, 15, +1, 1); pm_ui_step(&s, 15, +1, 1); pm_ui_step(&s, 15, +1, 1);
    pm_ui_step(&s, 15, +1, 1); pm_ui_step(&s, 15, +1, 1);
    CHECK(strcmp(pm_ui_label(&s, 15, 1, label), "M8 SHUFFLE") == 0, "label %s", label);
}

/* Per-track lengths (PER TRACK scale mode): each track wraps on its own. */
static void test_per_track_lengths(void) {
    PmState s; pm_init(&s, 8);
    s.settings.track[0] = PM_REVERSE;
    s.settings.track[1] = PM_PINGPONG;
    unsigned a[24], b[24];
    play(&s, 0, 6, 24, 1, a);
    play(&s, 1, 4, 24, 1, b);
    for (unsigned i = 0; i < 24; ++i) {
        CHECK(a[i] == 5 - i % 6, "T1 len 6 reverse");
        static const unsigned pp[] = {0,1,2,3,2,1};
        CHECK(b[i] == pp[i % 6], "T2 len 4 pingpong step %u -> %u", i, b[i]);
    }
}

/* NORMAL scale mode: one random order for every track; PER TRACK: each its own. */
static void test_shared_sequence(void) {
    PmState s; pm_init(&s, 9);
    s.settings.global = PM_SHUFFLE;
    s.settings.track[0] = s.settings.track[3] = PM_SHUFFLE;
    unsigned a[32], b[32], c[32], d[32];
    play(&s, 0, 16, 32, 0, a);
    play(&s, 3, 16, 32, 0, b);
    CHECK(memcmp(a, b, sizeof a) == 0, "NORMAL scale: tracks share the shuffle");
    pm_reset_all(&s);
    play(&s, 0, 16, 32, 1, c);
    play(&s, 3, 16, 32, 1, d);
    CHECK(memcmp(c, d, sizeof c) != 0, "PER TRACK: each track shuffles on its own");
}

/* What stock prepares while stopped (the next run, first pass) is exactly
 * what the run then plays, for every mode and both scale modes. */
static void test_next_run(void) {
    for (unsigned mode = 0; mode < PM_MODES; ++mode)
        for (unsigned per_track = 0; per_track < 2; ++per_track) {
            PmState s; pm_init(&s, 11 + mode);
            s.settings.global = (uint8_t)mode;
            s.settings.track[5] = (uint8_t)mode;
            unsigned out[20];
            play(&s, 5, 7, 20, per_track, out);          /* a run in progress */
            unsigned prepared[7];
            for (unsigned r = 0; r < 7; ++r)
                prepared[r] = pm_lookup_next_run(&s, 5, r, 7, per_track);
            pm_reset_all(&s);                             /* STOP, PLAY */
            for (unsigned r = 0; r < 7; ++r) {
                pm_advance(&s, 5, r, 7);
                unsigned got = pm_lookup(&s, 5, r, 7, per_track);
                CHECK(got == prepared[r], "mode %u per_track %u step %u: prepared %u, played %u",
                      mode, per_track, r, prepared[r], got);
            }
        }
}

/* STEP REPEAT: `n` steps of a repeat from its first, the keys unchanged. */
static void repeat_run(uint16_t held, unsigned base, unsigned len, unsigned n, unsigned *out) {
    unsigned last = PM_NO_STEP;
    for (unsigned i = 0; i < n; ++i) last = out[i] = pm_repeat_next(held, base, len, last);
}

/* The held steps in step order, round and round; steps past the track's
 * end left out. Keys are bits (bit k = trig key k + 1); steps count from 0. */
static void test_repeat(void) {
    unsigned out[8];
    static const unsigned want5_8[] = {4, 7, 4, 7, 4, 7};
    repeat_run(1u << 4 | 1u << 7, 0, 16, 6, out);
    CHECK(memcmp(out, want5_8, sizeof want5_8) == 0, "5 and 8: 5 8 5 8 5 8");
    repeat_run(1u << 2, 0, 16, 5, out);
    for (unsigned i = 0; i < 5; ++i) CHECK(out[i] == 2, "one key: that step every step");
    const uint16_t k1_9_16 = 1u << 0 | 1u << 8 | 1u << 15;
    static const unsigned want3[] = {0, 8, 15, 0, 8, 15};
    repeat_run(k1_9_16, 0, 16, 6, out);
    CHECK(memcmp(out, want3, sizeof want3) == 0, "1, 9, 16 in step order");
    /* An 8-step track: keys 9 and 16 lie past its end. */
    repeat_run(k1_9_16, 0, 8, 4, out);
    for (unsigned i = 0; i < 4; ++i) CHECK(out[i] == 0, "len 8: step 1 repeats");
    CHECK(pm_repeat_next(1u << 12, 0, 8, PM_NO_STEP) == PM_NO_STEP, "no held step inside the track");
    CHECK(pm_repeat_next(0, 0, 16, 3) == PM_NO_STEP, "nothing held");
    CHECK(pm_repeat_next(1u << 3, 0, 0, PM_NO_STEP) == PM_NO_STEP, "length 0");
    /* Keys pressed or let go mid-loop join or leave in their place: 1 3 6,
     * let go of 3 after it played: 6 comes next, not 1. */
    const uint16_t k1_3_6 = 1u << 0 | 1u << 2 | 1u << 5;
    CHECK(pm_repeat_next(k1_3_6, 0, 16, PM_NO_STEP) == 0 && pm_repeat_next(k1_3_6, 0, 16, 0) == 2,
          "1 then 3");
    CHECK(pm_repeat_next(1u << 0 | 1u << 5, 0, 16, 2) == 5, "3 let go after it played: 6 next");
    CHECK(pm_repeat_next(k1_3_6 | 1u << 3, 0, 16, 2) == 3, "4 pressed after 3 played: 4 next");
    CHECK(pm_repeat_next(1u << 9, 0, 16, 2) == 9, "only 10 left: 10");
    CHECK(pm_repeat_next(1u << 1, 0, 16, 9) == 1, "past the highest held step: round to the lowest");
    /* The second page: keys name steps 17..32; a 20-step track has 17..20. */
    CHECK(pm_repeat_next(1u << 0 | 1u << 3, 16, 32, 16) == 19, "page 2: key 4 is step 20");
    static const unsigned page2[] = {16, 17, 18, 19, 16, 17};
    repeat_run(0xffffu, 16, 20, 6, out);
    CHECK(memcmp(out, page2, sizeof page2) == 0, "page 2 of 20 steps: 17..20 only");
    /* Long patterns cap at 64 steps, as pm_map does. */
    CHECK(pm_repeat_next(0xffffu, 48, 200, 62) == 63 && pm_repeat_next(0xffffu, 48, 200, 63) == 48,
          "page 4 of a capped length");
    /* With no key held REPEAT plays what NORMAL plays. */
    for (unsigned raw = 0; raw < 20; ++raw)
        CHECK(pm_map(PM_REPEAT, 16, raw / 16, raw, 0) == raw % 16, "REPEAT idle is NORMAL");
}

int main(void) {
    test_repeat();
    test_normal_reverse();
    test_pingpong();
    test_shuffle();
    test_random();
    test_lookahead();
    test_advance();
    test_pingpong2();
    test_ui();
    test_per_track_lengths();
    test_shared_sequence();
    test_next_run();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("PLAY MODES engine: all host tests passed\n");
    return 0;
}
