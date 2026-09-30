#ifndef __MUSIC_H__
#define __MUSIC_H__

#define SONG_COUNT      2

#define MUS_STOP        0
#define MUS_PLAY        1
#define MUS_PAUSE       2

extern unsigned int g_ui_ms;

void          Music_Init(void);
void          Music_Load(unsigned char song);
void          Music_Start(void);
void          Music_Halt(void);

unsigned char Music_State(void);
unsigned char Music_UnitMs(void);
unsigned int  Music_ClockTick(void);  /* elapsed play time, 4 ms ticks */
void          Music_DisplayBegin(void);
void          Music_DisplayEnd(void);
/* Foreground scheduler; Timer 1 only advances the clock and never calls this
 * function from interrupt context. */
void          Music_Run(void);
unsigned char code *Music_Name(unsigned char song);

#endif
