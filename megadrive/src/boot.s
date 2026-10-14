/* Toast Simulator for the Sega Mega Drive: vector table, ROM header and start-up. */

    .section .text.vectors,"ax"
vectors:
    .long   0x01000000          /* initial stack pointer: the top of work RAM */
    .long   _start              /* reset */
    .long   _exc_bus            /* 2 bus error */
    .long   _exc_addr           /* 3 address error */
    .long   _exc_ill            /* 4 illegal instruction */
    .rept   19
    .long   _exc                /* divide by zero, privilege and the rest */
    .endr
    .long   _exc                /* 24 spurious interrupt */
    .long   _exc                /* 25 level 1 */
    .long   _exc                /* 26 level 2: external (light gun) */
    .long   _exc                /* 27 level 3 */
    .long   _hint               /* 28 level 4: horizontal blank */
    .long   _exc                /* 29 level 5 */
    .long   _vint               /* 30 level 6: vertical blank */
    .long   _exc                /* 31 level 7 */
    .rept   32
    .long   _exc                /* traps and reserved */
    .endr

header:
    .ascii  "SEGA MEGA DRIVE "
    .ascii  "(C)TOST 2026.OCT"
    .ascii  "TOAST SIMULATOR"
    .fill   33, 1, 0x20
    .ascii  "TOAST SIMULATOR"
    .fill   33, 1, 0x20
    .ascii  "GM 00000000-00"
    .word   0x0000              /* checksum, filled in by tools/fix_header.py */
    .ascii  "J               "
    .long   0x00000000          /* ROM start */
    .long   0x00000000          /* ROM end, filled in by tools/fix_header.py */
    .long   0x00FF0000, 0x00FFFFFF
    .ascii  "RA"                /* battery-backed save RAM on odd bytes */
    .word   0xF820
    .long   0x00200001, 0x00203FFF
    .fill   12, 1, 0x20         /* modem */
    .fill   40, 1, 0x20         /* notes */
    .ascii  "JUE             "

    .text
    .globl  _start
_start:
    move.w  #0x2700, %sr        /* interrupts off */
    move.b  0xA10001, %d0       /* hardware version: newer consoles need the TMSS handshake */
    andi.b  #0x0F, %d0
    beq.s   1f
    move.l  #0x53454741, 0xA14000   /* "SEGA" */
1:
    lea     0xFF0000, %a0       /* clear work RAM */
    move.w  #0x3FFF, %d0
2:  clr.l   (%a0)+
    dbra    %d0, 2b
    lea     _data_load, %a0     /* copy initialised data from ROM */
    lea     _data_start, %a1
    lea     _data_end, %a2
3:  cmpa.l  %a2, %a1
    bcc.s   4f
    move.b  (%a0)+, (%a1)+
    bra.s   3b
4:  jsr     main
5:  bra.s   5b

_vint:
    addq.l  #1, vblank_count
    rte

_hint:
    rte

/* A CPU exception: note which one and the stack frame (for the debugger), then stop. */
_exc_bus:   move.w  #2, exc_vector
            bra.s   _exc_save
_exc_addr:  move.w  #3, exc_vector
            bra.s   _exc_save
_exc_ill:   move.w  #4, exc_vector
            bra.s   _exc_save
_exc:       move.w  #99, exc_vector
_exc_save:  move.l  %sp, exc_sp
            move.l  (%sp), exc_frame
            move.l  4(%sp), exc_frame+4
            move.l  8(%sp), exc_frame+8
            move.l  12(%sp), exc_frame+12
1:          bra.s   1b

    .section .note.GNU-stack,"",@progbits
