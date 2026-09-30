#ifndef __MUSIC_H__
#define __MUSIC_H__

/*  Number of tunes in the player (the tables themselves live in songs.h,
    which only music.c includes).                                        */
#define SONG_COUNT      2

/*  player states */
#define MUS_STOP        0
#define MUS_PLAY        1
#define MUS_PAUSE       2

/*  1 ms countdown the user interface can use (Timer 1 decrements it)     */
extern unsigned int g_ui_ms;

void          Music_Init(void);

/*  choose a song and rewind it to the beginning (state becomes MUS_STOP) */
void          Music_Load(unsigned char song);

void          Music_Start(void);        /* play from the current position */
void          Music_Pause(void);
void          Music_Resume(void);
void          Music_Halt(void);         /* stop and stay on this song     */

unsigned char Music_State(void);
unsigned int  Music_Pos(void);          /* units played so far            */
unsigned int  Music_Total(void);        /* units in the whole song        */
unsigned char Music_UnitMs(void);       /* ms per unit for this song      */

/*  jump by +/- units - the 10 second rewind / fast forward uses this      */
void          Music_Seek(int units);

/*  service the player; call it as often as possible while playing         */
void          Music_Run(void);

/*  1 when the buzzer is silent right now, so the LCD may be written to
    (P2.5 is both the buzzer and the LCD RW line)                          */
unsigned char Music_BusFree(void);

/*  short name of a song, for the display                                 */
unsigned char code *Music_Name(unsigned char song);

#endif
