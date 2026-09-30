/* ===========================================================================
   Music player  -  STC89C52RC (8 KB) + buzzer + LCD1602
   Keil C51 / uVision 5

   Two tunes are built in; the melody of each was taken note for note out of
   a MIDI file and lives in songs.h.

   Keys (the four independent buttons, active low):
     selection screen   P3.1 / P3.2   previous / next tune
                        P3.0          play the selected tune
     while playing      P3.0          pause, press again to resume
                        P3.1          rewind  10 seconds
                        P3.2          forward 10 seconds
                        P3.3          stop and go back to the selection

     (P3.2 is shared with the board's infrared receiver, so the key scan
      filters pulse trains out - see keypad.c.)

   Display
     line 1   <number> <short name>          e.g.  1 MKDR
     line 2   selection screen : <number>/<total>
              playing          : a progress bar whose leading edge blinks
                                 while the music runs, and stays solid
                                 while it is paused

   Hardware note: the buzzer sits on P2.5, which is also the LCD RW line.
   Everything written to the LCD therefore waits until the buzzer is silent
   (Music_BusFree), which keeps the two from interfering.
   =========================================================================== */

#include <REGX52.H>
#include "lcd1602.h"
#include "keypad.h"
#include "delay.h"
#include "music.h"

#define UI_SELECT   0
#define UI_PLAY     1

#define BAR_LEN     16
#define BLINK_MS    300

/*  character generator slot 0: a solid block, the progress bar cell        */
unsigned char code BLOCK[8] =
{
    0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F
};

static unsigned char ui;            /* UI_SELECT or UI_PLAY               */
static unsigned char sel;           /* tune highlighted in the selection  */
static unsigned char blink_on;      /* state of the blinking edge         */
static unsigned char bar_fill;      /* cells filled when last drawn       */


/* ---- drawing ------------------------------------------------------------ */

static void Draw_Head(void)         /* line 1: number + short name */
{
    LCD1602_ShowString(1, 1, "                ");
    LCD1602_ShowChar(1, 1, (unsigned char)('1' + sel));
    LCD1602_ShowChar(1, 2, ' ');
    LCD1602_ShowString(1, 3, Music_Name(sel));
}

static void Draw_Select(void)       /* line 2: number / total */
{
    LCD1602_ShowString(2, 1, "                ");
    LCD1602_ShowChar(2, 1, (unsigned char)('1' + sel));
    LCD1602_ShowChar(2, 2, '/');
    LCD1602_ShowChar(2, 3, (unsigned char)('0' + SONG_COUNT));
}

/* how many of the 16 cells are full right now */
static unsigned char Bar_Fill(void)
{
    unsigned int  total = Music_Total();
    unsigned char f;

    if (total == 0) return 0;

    f = (unsigned char)(((unsigned int)Music_Pos() * BAR_LEN) / total);
    if (f > BAR_LEN) f = BAR_LEN;
    return f;
}

static void Draw_Bar(void)          /* the whole progress bar */
{
    unsigned char i;

    bar_fill = Bar_Fill();
    for (i = 0; i < BAR_LEN; i++)
    {
        LCD1602_ShowChar(2, i + 1, (unsigned char)((i < bar_fill) ? 0 : ' '));
    }
    blink_on = 1;
}

static void Draw_Edge(void)         /* only the leading edge cell */
{
    unsigned char col = (unsigned char)(bar_fill + 1);

    if (col > BAR_LEN) col = BAR_LEN;
    LCD1602_ShowChar(2, col, (unsigned char)(blink_on ? 0 : ' '));
}


/* ---- the blink, driven by the 1 ms tick --------------------------------- */

/*  Called from the main loop while the music plays.
    Writing to the LCD pulls P2.5 low (the LCD RW line) for about 2 ms per
    character, so every update is kept to ONE character and only done while
    the buzzer is in the silent part of a note.  Redrawing the whole bar
    takes 64 ms, which is why the full bar is only ever drawn while the
    player is stopped (see Handle_Key).                                   */
static void Ui_Tick(void)
{
    unsigned char f = Bar_Fill();

    if (f > bar_fill)                       /* the bar has grown by a cell */
    {
        if (!Music_BusFree()) return;
        LCD1602_ShowChar(2, bar_fill + 1, 0);
        bar_fill++;
        blink_on = 1;
        return;
    }

    if (g_ui_ms == 0 && Music_BusFree())    /* blink the leading edge */
    {
        g_ui_ms  = BLINK_MS;
        blink_on = (unsigned char)(!blink_on);
        Draw_Edge();
    }
}


/* ---- keys --------------------------------------------------------------- */

static void Handle_Key(unsigned char k)
{
    int           step;
    unsigned char was_playing;

    if (ui == UI_SELECT)
    {
        if (k == KEY_PREV)
        {
            if (sel > 0)
            {
                sel--;
                Music_Load(sel);
                Draw_Head();
                Draw_Select();
            }
        }
        else if (k == KEY_NEXT)
        {
            if (sel + 1 < SONG_COUNT)
            {
                sel++;
                Music_Load(sel);
                Draw_Head();
                Draw_Select();
            }
        }
        else if (k == KEY_MODE)                 /* play */
        {
            Music_Load(sel);
            ui = UI_PLAY;
            Draw_Head();
            Draw_Bar();                         /* buzzer still silent here */
            Music_Start();                      /* ... then start playing   */
        }
        return;
    }

    /* ---- playing ---- */
    if (k == KEY_MODE)                          /* pause / resume */
    {
        if (Music_State() == MUS_PLAY)
        {
            Music_Pause();
            blink_on = 1;
            Draw_Edge();                        /* solid edge while paused */
        }
        else if (Music_State() == MUS_PAUSE)
        {
            Music_Resume();
        }
        return;
    }

    if (k == KEY_PREV || k == KEY_NEXT)         /* +- 10 seconds */
    {
        step = (int)(10000 / (unsigned int)Music_UnitMs());

        was_playing = (unsigned char)(Music_State() == MUS_PLAY);
        if (was_playing) Music_Pause();         /* quiet while the bar redraws */

        if (k == KEY_PREV) Music_Seek(-step);
        else               Music_Seek(step);

        Draw_Bar();

        if (was_playing) Music_Resume();
        return;
    }

    if (k == KEY_STOP)                          /* back to the selection */
    {
        Music_Halt();
        ui = UI_SELECT;
        Draw_Head();
        Draw_Select();
        return;
    }
}


/* ---- main --------------------------------------------------------------- */

void main(void)
{
    unsigned char k;

    Delay_ms(100);                              /* LCD power on reset */
    LCD1602_Init();
    LCD1602_LoadChar(0, BLOCK);

    Key_Init();
    Music_Init();

    ui       = UI_SELECT;
    sel      = 0;
    blink_on = 1;
    bar_fill = 0;

    Music_Load(sel);
    Draw_Head();
    Draw_Select();

    while (1)
    {
        k = Key_Scan();
        if (k != KEY_NONE)
        {
            Handle_Key(k);
            continue;
        }

        if (ui != UI_PLAY) continue;

        if (Music_State() == MUS_PLAY)
        {
            Music_Run();
            Ui_Tick();
        }
        else if (Music_State() == MUS_STOP)     /* the tune ran to its end */
        {
            ui = UI_SELECT;
            Draw_Head();
            Draw_Select();
        }
    }
}
