/* Host tests for adapter.c, built with -DPM_HOST: the firmware addresses
 * the adapter reads are backed by plain arrays. No firmware. */
#include <stdio.h>
#include <string.h>
#include "playmode.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* What hooks.s allocates on the unit. */
PmState pm_state;
uint8_t pm_ready, pm_last_transport, pm_last_bank, pm_last_pattern;
uint32_t pm_restart;
char pm_toast[16];

unsigned pm_seq_step(unsigned track, unsigned raw);
unsigned pm_seq_peek(unsigned track, unsigned raw);
void pm_key_updown(unsigned track, int delta);
static const char *key(unsigned track, int delta) { pm_key_updown(track, delta); return pm_toast; }
unsigned pm_track_length(unsigned track);
unsigned pm_show(unsigned track, unsigned raw);
unsigned pm_project_format(char *out);
void pm_project_begin(unsigned storing);
unsigned pm_project_line(const char *line, unsigned parse_only);

/* Three regions: the bank blobs, the sequencer globals, the frame clock. */
static uint8_t blob[2 * 0x9b340];
static uint8_t seq[0x200];
static uint8_t clock_ram[16];
static uint8_t nv[16];               /* battery RAM 0x100b14e0.. */

volatile uint8_t *pm_host_addr(uint32_t a) {
    if (a >= 0x400e21e0u && a < 0x400e21e0u + sizeof blob) return blob + (a - 0x400e21e0u);
    if (a >= 0x80006500u && a < 0x80006500u + sizeof seq) return seq + (a - 0x80006500u);
    if (a >= 0x100b14e2u && a < 0x100b14ebu) return nv + (a - 0x100b14e0u);
    if (a >= 0x46104cf0u && a < 0x46104cf0u + sizeof clock_ram) return clock_ram + (a - 0x46104cf0u);
    printf("FAIL: adapter read an unexpected address 0x%08x\n", a);
    ++failures;
    static uint8_t junk[8];
    return junk;
}

static uint8_t *pattern(unsigned bank, unsigned pat) {
    return blob + bank * 0x9b340u + pat * 0x8ed8u;
}

static void set_transport(uint32_t v) { memcpy(seq + 0xb8, &v, 4); }
static void set_playing(unsigned bank, unsigned pat) { seq[0xbd] = (uint8_t)bank; seq[0xbe] = (uint8_t)pat; }

int main(void) {
    uint32_t clk = 0x1234567u; memcpy(clock_ram, &clk, 4);
    uint8_t *p0 = pattern(0, 0), *p1 = pattern(1, 3);
    p0[0x8e53] = 8;                 /* master length 8 */
    p0[0x8e55] = 0;                 /* NORMAL scale mode */
    p1[0x8e53] = 16;
    p1[0x8e55] = 1;                 /* PER TRACK */
    p1[2 * 0x91a + 0x50] = 5;       /* T3 length 5 */
    p1[0x48d0 + 1 * 0x8b0 + 0x28] = 7;  /* M2 length 7 (offset PENDING) */
    set_playing(0, 0);
    set_transport(1);

    /* NORMAL scale mode: the shared mode, the pattern's length. */
    char want[16];
    CHECK(strcmp(key(4, +1), "ALL REVERSED") == 0, "toast %s", pm_toast);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(pm_seq_step(0, i % 8) == 7 - i % 8, "T1 reversed over length 8");
    CHECK(pm_seq_peek(0, 8) == 7, "peek across the end");

    /* Pattern switch to a PER TRACK pattern: own modes, own lengths. */
    set_playing(1, 3);
    CHECK(pm_track_length(2) == 5 && pm_track_length(9) == 7 && pm_track_length(0) == 0 + 16,
          "per-track lengths %u %u %u", pm_track_length(2), pm_track_length(9), pm_track_length(0));
    key(2, +1); key(2, +1);              /* T3 PINGPONG */
    strcpy(want, "T3 PINGPONG");
    CHECK(strcmp(key(2, 0), want) == 0, "toast %s", pm_toast);
    static const unsigned pp5[] = {0,1,2,3,4,3,2,1,0,1,2,3,4,3,2,1};
    for (unsigned i = 0; i < 16; ++i) {
        unsigned got = pm_seq_step(2, i % 5);
        CHECK(got == pp5[i], "T3 pingpong step %u -> %u", i, got);
    }
    CHECK(pm_seq_step(1, 0) == 0, "T2 stays NORMAL under PER TRACK");

    /* STOP, PLAY: the bounce starts again from step 1, going forward. On
     * the unit the tick does not run while stopped, so nothing is called
     * between the two; the transport-start stubs raise pm_restart. */
    pm_restart = 1;                 /* a clean start for this check */
    for (unsigned i = 0; i < 5; ++i)
        CHECK(pm_seq_step(2, i) == i, "pass 0 forward: %u", i);
    CHECK(pm_seq_step(2, 0) == 3, "pass 1 turns: plays index 3");
    CHECK(pm_seq_step(2, 1) == 2, "index 2: on the way back");
    /* Bounces without a restart keep going: no reset during playback. */
    CHECK(pm_seq_step(2, 2) == 1 && pm_seq_step(2, 3) == 0, "pingpong comes back down");
    pm_restart = 1;                 /* STOP, PLAY */
    for (unsigned i = 0; i < 5; ++i)
        CHECK(pm_seq_step(2, i) == i, "after PLAY, pingpong runs forward from step 1: %u", i);
    CHECK(pm_seq_step(2, 0) == 3, "and turns at the end");

    /* The display (stock 0x4009b2b0) shows the played step; when it trails
     * the scheduler across the pattern end it keeps the previous pass. */
    set_playing(0, 0);                 /* NORMAL scale, length 8, shared mode */
    key(0, +1);                        /* REVERSED -> PINGPONG */
    pm_seq_step(0, 0);                 /* the reset after the switch */
    for (unsigned r = 0; r < 8; ++r) {
        pm_seq_step(0, r);
        CHECK(pm_show(0, r) == r, "pingpong pass 0: display %u", r);
    }
    pm_seq_step(0, 0);                 /* the scheduler wraps: pass 1 */
    CHECK(pm_show(0, 7) == 7, "display still on the old pass's last step");
    CHECK(pm_show(0, 0) == 6, "then pass 1 starts on step 7 going back: %u", pm_show(0, 0));
    CHECK(pm_show(20, 3) == 3, "out-of-range track is passed through");
    CHECK(pm_show(0xffffffffu, 2) == pm_show(0, 2), "master step shows track 0's mapping (NORMAL scale)");
    set_playing(1, 3);                 /* PER TRACK */
    CHECK(pm_show(0xffffffffu, 2) == 2, "master step stock under PER TRACK");

    /* While stopped, the rebuild prepares the next run's first step; it
     * must be what PLAY then plays (here: REVERSED after NORMAL). */
    set_playing(0, 0);
    key(0, -1); key(0, -1); key(0, -1);   /* back to NORMAL */
    pm_seq_step(0, 0); pm_seq_step(0, 1);
    set_transport(0);                     /* STOP */
    CHECK(pm_seq_peek(0, 0) == 0, "stopped, NORMAL: prepares step 1");
    key(0, +1);                           /* REVERSED while stopped */
    CHECK(pm_seq_peek(0, 0) == 7, "stopped, REVERSED: prepares step 8 (the last)");
    CHECK(pm_show(0, 0) == 7, "the LED agrees");
    set_transport(1); pm_restart = 1;     /* PLAY */
    CHECK(pm_seq_step(0, 0) == 7, "and PLAY plays step 8 first");

    /* PER TRACK with MASTER LENGTH: a track plays only the steps it reaches
     * before the restart, and the play modes mirror exactly those. */
    {
        uint8_t *p = pattern(1, 3);
        uint8_t *t1 = p + 0 * 0x91a, *t2 = p + 1 * 0x91a, *t3 = p + 2 * 0x91a;
        p[0x8e54] = 2;                          /* master scale 1X (6 ticks) */
        t1[0x50] = 20; t1[0x51] = 2;            /* T1: 20 steps at 1X */
        t2[0x50] = 20; t2[0x51] = 1;            /* T2: 20 steps at 3/2X (4 ticks) */
        t3[0x50] = 5;  t3[0x51] = 2;            /* T3: 5 steps, shorter than the master */
        p[0x8e50] = 0xff; p[0x8e51] = 0xff;     /* INF */
        set_playing(1, 3);
        CHECK(pm_track_length(0) == 20, "INF: the track's own length (%u)", pm_track_length(0));
        p[0x8e50] = 0; p[0x8e51] = 16;          /* MASTER LENGTH 16 */
        CHECK(pm_track_length(0) == 16, "master 16 cuts a 20-step 1X track to 16 (%u)", pm_track_length(0));
        CHECK(pm_track_length(1) == 20, "a faster track reaches 24 > 20: its own 20 (%u)", pm_track_length(1));
        CHECK(pm_track_length(2) == 5, "a 5-step track is not cut (%u)", pm_track_length(2));
        t2[0x51] = 3;                           /* T2 at 3/4X (8 ticks): 96/8 = 12 */
        CHECK(pm_track_length(1) == 12, "a slower track reaches 12 (%u)", pm_track_length(1));
        t2[0x51] = 4;                           /* 1/2X (12 ticks): 96/12 = 8 */
        CHECK(pm_track_length(1) == 8, "1/2X reaches 8 (%u)", pm_track_length(1));
        p[0x8e50] = 0; p[0x8e51] = 0;           /* 0: no cut */
        CHECK(pm_track_length(0) == 20, "master 0: no cut");
        p[0x8e50] = 0; p[0x8e51] = 16;
        /* REVERSED on T1 under master 16: 16..1, the mirror of what NORMAL plays. */
        pm_state.settings.track[0] = PM_REVERSE;
        pm_restart = 1;
        for (unsigned r = 0; r < 16; ++r)
            CHECK(pm_seq_step(0, r) == 15 - r, "T1 reversed under master 16: %u -> %u", r, 15 - r);
        p[0x8e50] = 0xff; p[0x8e51] = 0xff;
    }

    /* The project file and battery RAM. */
    {
        char line[40];
        pm_state.settings.global = PM_PINGPONG;
        for (unsigned t = 0; t < PM_TRACKS; ++t) pm_state.settings.track[t] = (uint8_t)(t % PM_MODES);
        unsigned n = pm_project_format(line);
        CHECK(n == strlen(line) && n == 31, "line length %u", n);
        CHECK(strcmp(line, "#PLAY_MODES=20123401234012340\r\n") == 0, "line %s", line);

        /* A storing pass starts from NORMAL and writes battery RAM. */
        nv[2] = 0x55;
        pm_project_begin(0);               /* parse-only: untouched */
        CHECK(pm_state.settings.global == PM_PINGPONG, "parse-only begin keeps the modes");
        pm_project_begin(1);
        CHECK(pm_state.settings.global == 0 && pm_state.settings.track[3] == 0, "storing begin: NORMAL");
        for (unsigned b = 2; b < 11; ++b) CHECK(nv[b] == 0, "battery byte %u cleared", b);
        CHECK(nv[11] == 0 && nv[12] == 0, "no write past 0x100b14ea");

        /* Ours, on the parse-only pass: consumed, nothing stored. */
        CHECK(pm_project_line("#PLAY_MODES=4", 1) == 1, "ours, parse-only");
        CHECK(pm_state.settings.global == 0, "parse-only stores nothing");
        CHECK(pm_project_line("#SEQUENCER_SCALE=3", 0) == 0, "another '#' line is not ours");
        CHECK(pm_project_line("#PLAY_MODE", 0) == 0, "a short key is not ours");
        CHECK(pm_project_line("#PLAY_MODES=31402", 0) == 1, "ours, storing");
        CHECK(pm_state.settings.global == 3 && pm_state.settings.track[0] == 1
              && pm_state.settings.track[1] == 4 && pm_state.settings.track[2] == 0
              && pm_state.settings.track[3] == 2 && pm_state.settings.track[4] == 0,
              "short line: given digits, the rest NORMAL");
        CHECK(nv[2] == 0x31 && nv[3] == 0x40 && nv[4] == 0x20, "battery nibbles %02x %02x %02x", nv[2], nv[3], nv[4]);
        pm_project_begin(1);
        CHECK(pm_project_line("#PLAY_MODES=19x4", 0) == 1, "bad digits");
        CHECK(pm_state.settings.global == 1 && pm_state.settings.track[0] == 0
              && pm_state.settings.track[1] == 0, "9 -> NORMAL, stop at a non-digit");

        /* Round trip, and a power cycle: DRAM fresh, battery RAM kept. */
        pm_project_begin(1);
        CHECK(pm_project_line("#PLAY_MODES=01234012340123401", 0) == 1, "full line");
        n = pm_project_format(line);
        CHECK(strcmp(line, "#PLAY_MODES=01234012340123401\r\n") == 0, "round trip %s", line);
        memset(&pm_state, 0, sizeof pm_state);
        pm_ready = 0;
        n = pm_project_format(line);
        CHECK(strcmp(line, "#PLAY_MODES=01234012340123401\r\n") == 0, "after power cycle %s", line);
        nv[2] = 0xf7;                      /* garbage: NORMAL */
        memset(&pm_state, 0, sizeof pm_state);
        pm_ready = 0;
        pm_project_format(line);
        CHECK(line[12] == '0' && line[13] == '0', "out-of-range nibbles read NORMAL: %s", line);

        /* A key change reaches battery RAM. */
        set_playing(1, 3);                 /* PER TRACK */
        pm_project_begin(1);
        key(2, +1);                        /* T3 REVERSED */
        CHECK(nv[3] == 0x01, "T3 in battery RAM: %02x", nv[3]);
        set_playing(0, 0);
        key(0, +1); key(0, +1);            /* ALL PINGPONG */
        CHECK(nv[2] >> 4 == 2, "the shared mode in battery RAM: %02x", nv[2]);
        pm_project_format(line);
        CHECK(strcmp(line, "#PLAY_MODES=20010000000000000\r\n") == 0, "line %s", line);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("PLAY MODES adapter: all host tests passed\n");
    return 0;
}
