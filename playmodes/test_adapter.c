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
uint8_t pm_table[256][17];
uint16_t pm_cur;
uint8_t pm_clip[17], pm_undo[17];
uint8_t pm_comp[16];

unsigned pm_seq_step(unsigned track, unsigned raw);
unsigned pm_seq_peek(unsigned track, unsigned raw);
void pm_key_updown(unsigned track, int delta);
static const char *key(unsigned track, int delta) { pm_key_updown(track, delta); return pm_toast; }
unsigned pm_track_length(unsigned track);
unsigned pm_show(unsigned track, unsigned raw);
unsigned pm_project_format(char *out, unsigned index);
void pm_project_begin(unsigned storing);
unsigned pm_project_line(const char *line, unsigned parse_only);
void pm_pattern_copy(uint32_t dst, uint32_t src, uint32_t n);
void pm_pattern_clear(uint32_t address);

/* Three regions: the bank blobs, the sequencer globals, the frame clock. */
static uint8_t blob[2 * 0x9b340];
static uint8_t seq[0x200];
static uint8_t clock_ram[16];
static uint8_t nv[0x910];             /* battery RAM 0x100f8600.. */

volatile uint8_t *pm_host_addr(uint32_t a) {
    if (a >= 0x400e21e0u && a < 0x400e21e0u + sizeof blob) return blob + (a - 0x400e21e0u);
    if (a >= 0x80006500u && a < 0x80006500u + sizeof seq) return seq + (a - 0x80006500u);
    if (a >= 0x100f8600u && a < 0x100f8f06u) return nv + (a - 0x100f8600u);
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
        p[0x8e52] = 2;                          /* MASTER SCALE 1X (6 ticks) */
        p[0x8e54] = 0;                          /* the NORMAL-mode scale (2X) must not count */
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
        pm_table[1 * 16 + 3][1] = PM_REVERSE;   /* B04's T1 */
        pm_cur = 0;
        pm_restart = 1;
        for (unsigned r = 0; r < 16; ++r)
            CHECK(pm_seq_step(0, r) == 15 - r, "T1 reversed under master 16: %u -> %u", r, 15 - r);
        p[0x8e50] = 0xff; p[0x8e51] = 0xff;
    }

    /* The playhead wins over the pattern bytes: the reported case (NORMAL
     * LEN 10, switched to PER TRACK) seen from both sides. */
    {
        uint8_t *p = pattern(1, 3);
        p[0x8e55] = 1; p[0x8e50] = 0xff; p[0x8e51] = 0xff;
        p[0x91a * 4 + 0x50] = 10; p[0x91a * 4 + 0x51] = 2;   /* T5: bytes say 10 */
        set_playing(1, 3);
        pm_table[1 * 16 + 3][5] = PM_REVERSE;                 /* B04's T5 REVERSED */
        pm_cur = 0; pm_restart = 1;
        for (unsigned r = 0; r < 16; ++r) pm_seq_step(4, r);  /* stock plays 16 */
        pm_seq_step(4, 0);                                    /* and wraps */
        for (unsigned r = 1; r < 16; ++r)
            CHECK(pm_seq_step(4, r) == 15 - r, "T5 learnt 16 steps: %u -> %u", r, 15 - r);
        CHECK(pm_show(4, 3) == 12, "the LEDs agree (%u)", pm_show(4, 3));
        p[0x91a * 4 + 0x50] = 24;                             /* an edit: learnt length dropped */
        CHECK(pm_seq_peek(4, 0) == 23, "an edited length is taken at once (%u)", pm_seq_peek(4, 0));
        p[0x91a * 4 + 0x50] = 16;
        pm_restart = 1;
        for (unsigned r = 0; r < 10; ++r) pm_seq_step(4, r);  /* bytes say 16, stock wraps at 10 */
        pm_seq_step(4, 0);
        for (unsigned r = 1; r < 10; ++r)
            CHECK(pm_seq_step(4, r) == 9 - r, "T5 learnt 10 steps: %u -> %u", r, 9 - r);
        /* MASTER LENGTH 16 over a 14-step track: stock plays 0..13, 0, 1,
         * then restarts every track. The short pass is not its length:
         * REVERSED keeps playing 14..1, 14, 13 (the user's report). */
        p[0x91a * 4 + 0x50] = 14; p[0x8e50] = 0; p[0x8e51] = 16; p[0x8e52] = 2;
        pm_table[1 * 16 + 3][5] = PM_REVERSE; pm_cur = 0; pm_restart = 1;
        static const unsigned stock[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,0,1};
        for (unsigned pass = 0; pass < 4; ++pass)
            for (unsigned i = 0; i < 16; ++i) {
                unsigned got = pm_seq_step(4, stock[i]);
                CHECK(got == 13 - stock[i], "pass %u, stock step %u -> %u (got %u)", pass, stock[i], 13 - stock[i], got);
            }
        /* PINGPONG and PINGPONG 2 start the bounce again at each master
         * restart; within the master loop the bounce turns at step 14. */
        static const unsigned pp[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,12,11};
        static const unsigned pp2[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,13,12};
        for (unsigned mode = PM_PINGPONG; mode <= PM_PINGPONG2; mode += PM_PINGPONG2 - PM_PINGPONG) {
            pm_table[1 * 16 + 3][5] = (uint8_t)mode; pm_cur = 0; pm_restart = 1;
            for (unsigned loop = 0; loop < 4; ++loop)
                for (unsigned i = 0; i < 16; ++i) {
                    unsigned got = pm_seq_step(4, stock[i]), want = (mode == PM_PINGPONG ? pp : pp2)[i];
                    CHECK(got == want, "mode %u loop %u step %u: %u, want %u", mode, loop, i, got, want);
                }
        }
        p[0x8e50] = 0xff; p[0x8e51] = 0xff;
        /* NORMAL is stock, whatever the length. */
        pm_table[1 * 16 + 3][5] = PM_NORMAL; pm_cur = 0;
        for (unsigned r = 0; r < 20; ++r)
            CHECK(pm_seq_step(4, r) == r && pm_show(4, r) == r && pm_seq_peek(4, r) == r, "NORMAL passes %u through", r);
        p[0x91a * 4 + 0x50] = 16;
        for (unsigned i = 0; i < 17; ++i) pm_table[1 * 16 + 3][i] = 0;
        pm_cur = 0;
        set_playing(0, 0);
    }

    /* Per pattern: each pattern keeps its own modes. */
    {
        set_transport(1);
        set_playing(0, 0);                 /* A01, NORMAL scale mode */
        pm_project_begin(1);               /* all NORMAL */
        key(0, +1);                        /* A01: ALL REVERSED */
        CHECK(strcmp(pm_toast, "ALL REVERSED") == 0, "A01 %s", pm_toast);
        set_playing(1, 3);                 /* B04, PER TRACK */
        pm_seq_step(0, 0);
        CHECK(pm_state.settings.global == 0 && pm_state.settings.track[0] == 0, "B04 starts NORMAL");
        key(2, +1); key(2, +1);            /* B04 T3 PINGPONG */
        set_playing(0, 0);                 /* back to A01 */
        pm_seq_step(0, 0);
        CHECK(pm_state.settings.global == PM_REVERSE, "A01 is REVERSED again (the reported bug)");
        CHECK(pm_state.settings.track[2] == 0, "A01's T3 untouched");
        set_playing(1, 3);
        pm_seq_step(0, 0);
        CHECK(pm_state.settings.track[2] == PM_PINGPONG && pm_state.settings.global == 0, "B04 kept T3 PINGPONG");
        set_playing(0, 0);
        pm_seq_step(0, 0);

        /* The project lines: one per pattern that is not all NORMAL. */
        char line[40];
        unsigned lines = 0;
        for (unsigned i = 0; i < 256; ++i) if (pm_project_format(line, i)) ++lines;
        CHECK(lines == 2, "two lines (%u)", lines);
        CHECK(pm_project_format(line, 0) == 35 && strcmp(line, "#PLAY_MODES=A01:10000000000000000\r\n") == 0, "A01 %s", line);
        CHECK(pm_project_format(line, 19) && strcmp(line, "#PLAY_MODES=B04:00020000000000000\r\n") == 0, "B04 %s", line);
        CHECK(pm_project_format(line, 1) == 0, "A02 all NORMAL: no line");
        CHECK(pm_project_format(line, 256) == 0, "past the end: nothing");

        /* Loading: a storing pass starts all NORMAL, then reads the lines. */
        pm_project_begin(0);
        CHECK(pm_table[0][0] == PM_REVERSE, "parse-only begin keeps the table");
        pm_project_begin(1);
        CHECK(pm_table[0][0] == 0 && pm_table[19][3] == 0, "storing begin: all NORMAL");
        pm_seq_step(0, 0);
        CHECK(pm_state.settings.global == 0, "the playing pattern follows the reset");
        CHECK(pm_project_line("#PLAY_MODES=P16:4", 1) == 1 && pm_table[255][0] == 0, "parse-only stores nothing");
        CHECK(pm_project_line("#SEQUENCER_SCALE=3", 0) == 0, "another '#' line is not ours");
        CHECK(pm_project_line("#PLAY_MODES=P16:4", 0) == 1 && pm_table[255][0] == 4, "P16 SHUFFLE");
        CHECK(pm_project_line("#PLAY_MODES=A01:31402", 0) == 1, "A01 short line");
        CHECK(pm_table[0][0] == 3 && pm_table[0][1] == 1 && pm_table[0][2] == 4 && pm_table[0][3] == 0
              && pm_table[0][4] == 2 && pm_table[0][5] == 0, "short line: given digits, the rest NORMAL");
        pm_seq_step(0, 0);
        CHECK(pm_state.settings.global == 3, "the playing pattern picks up its loaded row");
        CHECK(pm_project_line("#PLAY_MODES=A17:4", 0) == 1 && pm_project_line("#PLAY_MODES=Q01:4", 0) == 1
              && pm_project_line("#PLAY_MODES=A00:4", 0) == 1 && pm_project_line("#PLAY_MODES=A1:4", 0) == 1,
              "bad pattern names are ours, and skipped");
        CHECK(pm_project_line("#PLAY_MODES=B02:19x4", 0) == 1 && pm_table[17][0] == 1 && pm_table[17][1] == 0
              && pm_table[17][2] == 0, "9 -> NORMAL, stop at a non-digit");
        pm_project_begin(1);
        CHECK(pm_project_line("#PLAY_MODES=20000000100000000", 0) == 1, "build 17's line");
        CHECK(pm_table[0][0] == 2 && pm_table[255][0] == 2 && pm_table[100][8] == 1, "applies to every pattern");

        /* Battery RAM: a power cycle (DRAM fresh) brings the table back. */
        pm_project_begin(1);
        pm_project_line("#PLAY_MODES=A01:10000000000000000", 0);
        pm_project_line("#PLAY_MODES=C05:01234012340123401", 0);
        pm_project_line("#PLAY_MODES=P16:00000000000000004", 0);
        uint8_t before[256][17];
        memcpy(before, pm_table, sizeof before);
        memset(&pm_state, 0, sizeof pm_state); memset(pm_table, 0, sizeof pm_table);
        pm_ready = 0; pm_cur = 0;
        pm_seq_step(0, 0);
        CHECK(memcmp(before, pm_table, sizeof before) == 0, "the table after a power cycle");
        CHECK(pm_state.settings.global == PM_REVERSE, "A01 plays REVERSED after the power cycle");

        /* A damaged copy reads as all NORMAL (and a new copy is started). */
        nv[40] ^= 0x40;
        memset(pm_table, 0, sizeof pm_table); pm_ready = 0; pm_cur = 0;
        pm_seq_step(0, 0);
        unsigned any = 0;
        for (unsigned i = 0; i < 256; ++i) for (unsigned k = 0; k < 17; ++k) any |= pm_table[i][k];
        CHECK(!any, "a bad checksum: all NORMAL");
        CHECK(nv[0] == 'P' && nv[3] == 'V', "a fresh copy is marked");

        /* Every pattern fits: all 256 full rows survive a power cycle. */
        pm_project_begin(1);
        char l2[48];
        for (unsigned i = 0; i < 256; ++i) {
            unsigned n = (unsigned)snprintf(l2, sizeof l2, "#PLAY_MODES=%c%02u:", 'A' + i / 16, i % 16 + 1);
            for (unsigned k = 0; k < 17; ++k) l2[n++] = (char)('0' + (i + k) % 5);
            l2[n] = 0;
            pm_project_line(l2, 0);
        }
        memcpy(before, pm_table, sizeof before);
        memset(pm_table, 0, sizeof pm_table); pm_ready = 0; pm_cur = 0;
        pm_seq_step(0, 0);
        CHECK(memcmp(before, pm_table, sizeof before) == 0, "all 256 rows after a power cycle");
        unsigned lines2 = 0;
        for (unsigned i = 0; i < 256; ++i) if (pm_project_format(l2, i)) ++lines2;
        CHECK(lines2 == 256, "and 256 project lines (%u)", lines2);

        /* Copy, paste, undo: the modes go where stock's memcpy takes the
         * pattern. Bank RAM 0x400e21e0 + bank * 0x9b340 + pattern * 0x8ed8. */
        pm_project_begin(1);
        pm_project_line("#PLAY_MODES=A01:10000000000000000", 0);
        pm_project_line("#PLAY_MODES=B04:00020000000000000", 0);
        const uint32_t A01 = 0x400e21e0u, B04 = 0x400e21e0u + 0x9b340u + 3 * 0x8ed8u,
                       A02 = 0x400e21e0u + 0x8ed8u, CLIP = 0x460c8122u, UNDO = 0x460bf218u;
        pm_pattern_copy(CLIP, A01, 0x8ed8);                 /* copy A01 */
        CHECK(pm_clip[0] == PM_REVERSE, "the clipboard holds A01's modes");
        pm_pattern_copy(UNDO, B04, 0x8ed8);                 /* paste on B04: undo snapshot */
        pm_pattern_copy(B04, CLIP, 0x8ed8);                 /* ... the paste */
        pm_pattern_copy(0x1001614eu + 3 * 0x8ed8u, CLIP, 0x8ed8);   /* its battery copy: ignored */
        CHECK(pm_table[19][0] == PM_REVERSE && pm_table[19][3] == 0, "B04 now has A01's modes");
        CHECK(pm_undo[3] == PM_PINGPONG, "the undo buffer kept B04's");
        pm_pattern_copy(B04, UNDO, 0x8ed8);                 /* undo */
        CHECK(pm_table[19][3] == PM_PINGPONG && pm_table[19][0] == 0, "undo brings B04's back");
        pm_pattern_copy(A02, A01, 0x91a);                   /* a track copy: not a pattern */
        pm_pattern_copy(A02 + 4, A01, 0x8ed8);              /* not a pattern start */
        CHECK(pm_table[1][0] == 0, "other copies leave the modes alone");
        set_playing(0, 1);                                  /* A02 playing */
        pm_seq_step(0, 0);
        pm_pattern_copy(A02, CLIP, 0x8ed8);                 /* paste onto the playing pattern */
        pm_seq_step(0, 1);
        CHECK(pm_state.settings.global == PM_REVERSE, "the playing pattern plays the pasted modes");
        memcpy(before, pm_table, sizeof before);
        memset(pm_table, 0, sizeof pm_table); pm_ready = 0; pm_cur = 0;
        pm_seq_step(0, 0);
        CHECK(memcmp(before, pm_table, sizeof before) == 0, "pastes reach battery RAM");
        /* Clear pattern: that pattern's modes back to NORMAL, only it. */
        pm_pattern_clear(A02);              /* A02 is playing, REVERSED */
        pm_seq_step(0, 2);
        CHECK(pm_state.settings.global == 0 && pm_table[1][0] == 0, "cleared A02 plays NORMAL");
        CHECK(pm_table[19][3] == PM_PINGPONG && pm_table[0][0] == PM_REVERSE, "other patterns keep theirs");
        pm_pattern_clear(A02 + 0x50);       /* not a pattern start: nothing */
        pm_pattern_clear(CLIP);
        CHECK(pm_clip[0] == PM_REVERSE, "the clipboard is not a pattern to clear");
        memset(pm_table, 0, sizeof pm_table); pm_ready = 0; pm_cur = 0;
        pm_seq_step(0, 0);
        CHECK(pm_table[1][0] == 0 && pm_table[0][0] == PM_REVERSE, "the clear reaches battery RAM");
        set_playing(0, 0);
        pm_project_begin(1);
    }

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("PLAY MODES adapter: all host tests passed\n");
    return 0;
}
