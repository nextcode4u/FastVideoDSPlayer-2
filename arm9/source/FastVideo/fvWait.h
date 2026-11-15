#pragma once
#include <nds.h>
#include <stdio.h>
#include "../../../common/ipc.h"

/* ARM9 timers 0/1 are a free-running clock initialized before any IPC. */
static inline u32 fv_ticks(void)
{
    u16 high, low;
    do { high = TIMER1_DATA; low = TIMER0_DATA; } while (high != TIMER1_DATA);
    return ((u32)high << 16) | low;
}
static inline void fv_checkWait(u32 start)
{
    if ((u32)(fv_ticks() - start) < 32728u * 30u) return;
    /* Do not free or reuse shared memory while a timed-out ARM7 might still
       be writing to it. A restart is safer than attempting another IPC. */
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_CONTROL_AUDIO, IPC_ARG_CONTROL_AUDIO_STOP));
    lcdMainOnTop();
    powerOn(PM_BACKLIGHT_BOTTOM | PM_BACKLIGHT_TOP);
    REG_MASTER_BRIGHT_SUB = 0;
    consoleDemoInit();
    iprintf("Storage/CPU response timed out.\n\nPlayback has stopped.\nPower off before checking\nthe SD card.\n");
    for (;;) swiWaitForVBlank();
}
static inline void fv_waitReply(int channel)
{
    u32 start = fv_ticks();
    while (!fifoCheckValue32(channel)) { fv_checkWait(start); swiDelay(1000); }
}
