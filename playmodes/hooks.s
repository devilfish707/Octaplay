| PLAY MODES -- the hand-written part of the unit: the detour stubs, the
| register-saving entries they call, and the state the C keeps in
| loader-owned DRAM. generate.py appends this to the compiled playmode.c +
| adapter.c.
|
| Sites read from the owner's original 1.40C with investigate.py (rounds 1
| and 2, 3 Oct 2026); INVESTIGATION.md section 4 has the stock listings.
| Every stub is reached by a six-byte `jmp` that the manifest plants over
| whole stock instructions, replays them, and jumps back.
        .text
        .balign 4
        .global pm_step_entry, pm_peek_entry, pm_show_entry, pm_key_entry

| pm_step_entry(track, raw) -> d0 = the step to play.
| Saves everything the C may clobber except d0, so a stub can call it in
| the middle of stock code: push raw, push track, jsr, addq #8,sp.
pm_step_entry:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  20(%sp),-(%sp)          | raw
        move.l  20(%sp),-(%sp)          | track (shifted by the first push)
        jsr     pm_seq_step
        addq.l  #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        rts

| pm_peek_entry(track, raw) -> d0: the look-ahead form, no state change.
pm_peek_entry:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     pm_seq_peek
        addq.l  #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        rts

| pm_show_entry(track, raw) -> d0: the step the display should show.
pm_show_entry:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     pm_show
        addq.l  #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        rts

| pm_key_entry(track, delta): TRACK held + UP (-1) / DOWN (+1). Changes the
| mode and opens the stock toast with the new text. Key-handler context
| ONLY: the toast's close path reaches the kernel post 0x40000c3c, which
| hard-crashed a unit when reached from the frame path (Kyoti, Session 93).
| The duration must be > 0 (0 or less hung a unit: Kyoti, Session 50).
        .equ    NOTIFY, 0x4005a2b8      | FUN_4005a2b8(text, duration)
        .equ    REBUILD, 0x4009da20     | the working-set rebuild an edit calls; -1 = all tracks
        .equ    TOAST_TIME, 0x3c        | 1 s at the 60 Hz UI tick
pm_key_entry:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  24(%sp),-(%sp)          | delta
        move.l  24(%sp),-(%sp)          | track
        jsr     pm_key_updown           | fills pm_toast
        addq.l  #8,%sp
        pea     TOAST_TIME
        pea     pm_toast
        jsr     NOTIFY
        addq.l  #8,%sp
        pea     0xffffffff              | re-prepare every track's step in the
        jsr     REBUILD                 | new mode, as stock does after an edit
        addq.l  #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        rts

| ---- the detour stubs ------------------------------------------------------
        .global pm_audio_step, pm_midi_step, pm_audio_rebuild, pm_midi_rebuild
        .global pm_arrow_key, pm_track_key, pm_step_getter
        .global pm_start_play, pm_start_ext1, pm_start_ext2, pm_start_ext3

| 0x400a2d6a, the tick's audio pass, just before 0x4009d1e8(track, bank,
| pattern, step, n): stock loads step = 0x800064d0[track] (track in d5).
| Displaced: lea 0x800064d0,%a0 (6). The next instruction, mvzb
| (0,%a0,%d5.l),%d0, is done here and skipped on the way back.
pm_audio_step:
        lea     0x800064d0,%a0
        mvz.b   %a0@(0,%d5:l),%d0         | MIT syntax, as objdump prints it
        move.l  %d0,-(%sp)              | raw step
        move.l  %d5,-(%sp)              | track 0..7
        jsr     pm_step_entry           | d0 := the step to play; d1/a0/a1 kept
        addq.l  #8,%sp
        jmp     0x400a2d74              | movel %d0,%sp@- (the step argument)

| 0x400a39b6, the tick's MIDI pass, before 0x4009cf4c(track, bank, pattern,
| step, n): step = 0x800064d0[8 + track] via the walking pointer at
| 100(%sp); MIDI track in d7. Displaced (8, the manifest pads one nop):
| moveal %sp@(100),%a0 ; mvzb %a0@(8),%d0. A jmp leaves sp as stock had it.
pm_midi_step:
        movea.l 100(%sp),%a0
        mvz.b   8(%a0),%d0
        move.l  %d0,-(%sp)
        move.l  %d7,%d1
        addq.l  #8,%d1                  | the engine's MIDI tracks are 8..15
        move.l  %d1,-(%sp)
        jsr     pm_step_entry
        addq.l  #8,%sp
        jmp     0x400a39be              | movel %d0,%sp@-

| 0x4009dc86 / 0x4009e3dc, the working-set rebuild 0x4009da20 (after an
| edit): stock computes the current step in d2 and calls the step handler
| with n = -1. Track in d6 (0..7 audio; 8..15 on the MIDI branch, which
| 0x4009dc78 takes when d6 > 7). Displaced: pea 0xffffffff ; movel %d2,
| %sp@- (6). The look-ahead form: no engine state changes.
pm_audio_rebuild:
        pea     0xffffffff
        move.l  %d2,-(%sp)
        move.l  %d6,-(%sp)
        jsr     pm_peek_entry
        addq.l  #8,%sp
        move.l  %d0,-(%sp)
        jmp     0x4009dc8c

pm_midi_rebuild:
        pea     0xffffffff
        move.l  %d2,-(%sp)
        move.l  %d6,-(%sp)
        jsr     pm_peek_entry
        addq.l  #8,%sp
        move.l  %d0,-(%sp)
        jmp     0x4009e3e2

| 0x4009b2b0, stock step(track): track < 0 returns the master step
| (0x800065b4); otherwise it branches to 0x4009b2be for the track's own
| step. Its 11 callers are UI code: the trig LEDs, live recording, lock
| editing (track = the current track, MIDI tracks as 8 + t; -1 for the
| master). Both answers are mapped like the step that plays (the master
| only under NORMAL scale mode). Displaced: movel %sp@(4),%d0 ;
| bges 0x4009b2be (6).
pm_step_getter:
        move.l  4(%sp),%d0
        bge.s   .Lgetter_track
        mvz.w   0x800065b4,%d0          | stock: the master step
        move.l  %d0,-(%sp)              | raw
        pea     0xffffffff              | "track" -1: the master
        jsr     pm_show_entry           | mapped under NORMAL scale mode
        addq.l  #8,%sp
        rts
.Lgetter_track:
        move.l  %d0,-(%sp)              | call the stock per-track branch as
        jsr     0x4009b2be              | stock enters it: d0 = track, arg at 4(sp)
        addq.l  #4,%sp
        move.l  %d0,-(%sp)              | raw step
        move.l  8(%sp),-(%sp)           | track (the caller's argument)
        jsr     pm_show_entry
        addq.l  #8,%sp
        rts

| The four places that set the transport playing (0x800065b8 := 1), each
| followed by the same `lea 0x4610757c,%a0` (6), displaced here: PLAY
| 0x4009c3da (EUCLID's PLAY stub returns to this very address, so the two
| compose), and 0x400a2210 / 0x400a24d6 / 0x400a27e8 in the tick (external
| start / continue). Raise pm_restart. Only a0 is used (the displaced lea
| loads it next); data registers stay as stock left them (d0/d1/d5 hold the
| 1 just stored), and the next stock instruction sets the condition codes.
pm_start_play:
        lea     pm_restart,%a0
        move.l  #1,(%a0)                | no data register touched
        lea     0x4610757c,%a0
        jmp     0x4009c3e0
pm_start_ext1:
        lea     pm_restart,%a0
        move.l  #1,(%a0)                | no data register touched
        lea     0x4610757c,%a0
        jmp     0x400a2216
pm_start_ext2:
        lea     pm_restart,%a0
        move.l  #1,(%a0)                | no data register touched
        lea     0x4610757c,%a0
        jmp     0x400a24dc
pm_start_ext3:
        lea     pm_restart,%a0
        move.l  #1,(%a0)                | no data register touched
        lea     0x4610757c,%a0
        jmp     0x400a27ee

| 0x40040250, the TRACK key handler (codes 0x10..0x17), handler(code,
| event): event 1 = press, 0 = release (as the REC handler 0x40048774 reads
| it), other values (hold, repeat) leave the state alone. Stock has no plain
| "TRACK held" byte: 0x400c0aac is its double-tap memory and survives the
| release. So the held key is kept here, as track + 1 (0 = none, which is
| also what the zeroed DRAM starts with). Displaced: movel %d2,%sp@- ;
| movel %sp@(8),%d1 (6). d0 is free: stock loads it next.
pm_track_key:
        move.l  4(%sp),%d1
        subi.l  #0x0f,%d1               | code 0x10..0x17 -> 1..8
        beq.s   .Ltrack_stock
        moveq   #8,%d0
        cmp.l   %d0,%d1
        bhi.s   .Ltrack_stock           | unsigned: not a TRACK key
        move.l  8(%sp),%d0
        cmpi.l  #1,%d0
        bne.s   .Ltrack_other
        move.l  %d1,pm_held_track       | press
        bra.s   .Ltrack_stock
.Ltrack_other:
        tst.l   %d0
        bne.s   .Ltrack_stock           | hold / repeat: keep
        cmp.l   pm_held_track,%d1       | release of the held key
        bne.s   .Ltrack_stock
        clr.l   pm_held_track
.Ltrack_stock:
        move.l  %d2,-(%sp)              | displaced
        move.l  8(%sp),%d1              | displaced
        jmp     0x40040256

| 0x400491a0, the UP / DOWN handler (key codes 0x33 / 0x20), handler(code,
| event). Stock acts on the main screen only inside the arranger or one
| mode check; otherwise it returns. With a TRACK key held (pm_held_track) a
| press changes the play mode and is consumed. MIDI mode (0x80000012 != 0)
| addresses M1..M8.
| ASSUMED, to confirm on the unit: 0x33 is UP (towards NORMAL). If the
| toast walks the wrong way, change KEY_UP to 0x20.
        .equ    KEY_UP, 0x33
        .equ    MIDI_MODE, 0x80000012
pm_arrow_key:
        move.l  8(%sp),%d0              | ColdFire cmpi takes a data register only
        cmpi.l  #1,%d0                  | a press?
        bne.s   .Larrow_stock
        move.l  pm_held_track,%d0       | 1..8, 0 = none
        beq.s   .Larrow_stock
        subq.l  #1,%d0
        tst.l   MIDI_MODE
        beq.s   .Larrow_audio
        addq.l  #8,%d0
.Larrow_audio:
        moveq   #1,%d1                  | DOWN: towards SHUFFLE
        move.l  4(%sp),%a0
        cmpa.l  #KEY_UP,%a0
        bne.s   .Larrow_delta
        moveq   #-1,%d1                 | UP: towards NORMAL
.Larrow_delta:
        move.l  %d1,-(%sp)
        move.l  %d0,-(%sp)
        jsr     pm_key_entry            | change the mode, open the toast
        addq.l  #8,%sp
        rts                             | consumed: back to the key dispatcher
.Larrow_stock:
        move.l  %d2,-(%sp)              | displaced
        movea.l 8(%sp),%a0              | displaced
        jmp     0x400491a6

| Explicitly initialised, loader-owned DRAM (as EUCLID's state), not the
| 0x80006a40 scratch block, which the live DSP path overwrites. The C's
| _Static_asserts pin the sizes.
        .balign 4
        .global pm_state, pm_ready, pm_last_transport, pm_last_bank
        .global pm_last_pattern, pm_toast, pm_held_track, pm_restart
pm_state:
        .zero   224
pm_ready:
        .byte   0
pm_last_transport:
        .byte   0
pm_last_bank:
        .byte   0
pm_last_pattern:
        .byte   0
pm_toast:
        .zero   16
pm_held_track:
        .long   0                       | the held TRACK key + 1, 0 = none
pm_restart:
        .long   0                       | 1 = a transport start since the last step
