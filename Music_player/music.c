/* ===========================================================================
   Buzzer music player engine  -  STC89C52RC, 11.0592 MHz

   Buzzer : P2.5 -> ULN2003D IN5, OUT5 (pin 12) -> net BEEP -> BZ1.
            The ULN2003 inverts, so P2.5 HIGH sounds the buzzer.

   !! P2.5 is also the LCD1602 RW line.  The LCD is only ever written to
      while the buzzer is silent (see Music_BusFree) - that is safe because
      the LCD ignores RW while E is low, and the buzzer pin is free then.

   Timer 0 makes the square wave: 16 bit, reloaded from TONE[] inside its
   interrupt, toggling the buzzer pin once per half period.
   Timer 1 gives a 1 ms tick that times the notes and the display blink.

   The player is a small state machine.  Music_Run() is called from the main
   loop and returns after every note phase, so the keys stay responsive
   while the music plays.
   =========================================================================== */

#include <REGX52.H>
#include "music.h"
#include "songs.h"

sbit BEEP = P2^5;

static unsigned char code *song;        /* the tune being played           */
static unsigned char  cur_song;         /* its number                      */
static unsigned int   idx;              /* position in the note table      */
static unsigned int   pos;              /* units played so far             */
static unsigned char  state;            /* MUS_STOP / MUS_PLAY / MUS_PAUSE */
static unsigned char  phase;            /* 0 = sounding, 1 = gap           */
static unsigned char  unit_ms;          /* ms per unit for this song       */
static unsigned int   gap_ms;           /* length of the gap after a note  */
static unsigned int   sound_ms;         /* length of the note itself       */
static unsigned int   half_period;      /* Timer 0 reload                  */
static unsigned int   ms_count;         /* note phase countdown            */

unsigned int g_ui_ms;                   /* UI countdown, 1 ms per tick     */


/* ---- Timer 0: one interrupt per half period -> square wave -------------- */
void Timer0_ISR(void) interrupt 1
{
    TH0 = (unsigned char)(half_period >> 8);
    TL0 = (unsigned char)half_period;
    BEEP = !BEEP;
}

/* ---- Timer 1: 1 ms tick for the note lengths and the UI ---------------- */
void Timer1_ISR(void) interrupt 3
{
    TH1 = 0xFC;             /* 65536 - 921 = 1 ms at 11.0592 MHz */
    TL1 = 0x67;
    if (ms_count != 0) ms_count--;
    if (g_ui_ms  != 0) g_ui_ms--;
}


static void Tone_On(unsigned char note)
{
    half_period = TONE[note];
    TH0 = (unsigned char)(half_period >> 8);
    TL0 = (unsigned char)half_period;
    TR0 = 1;
}

static void Tone_Off(void)
{
    TR0  = 0;
    BEEP = 0;
}


/*  Pick up the event at idx and start sounding it.                        */
static void Start_Event(void)
{
    unsigned char note  = song[idx];
    unsigned char units = song[idx + 1];
    unsigned int  ms;

    pos += units;
    idx += 2;

    ms = (unsigned int)units * unit_ms;

    if (note == N_REST)
    {
        /*  a rest is silent for its WHOLE length - getting this wrong is
            what makes a tune run fast and blur together                 */
        Tone_Off();
        phase    = 1;
        sound_ms = 0;
        gap_ms   = ms;
        ms_count = ms;
        return;
    }

    /*  a note sounds for most of its length and leaves a short gap at the
        end, so repeated notes stay separate.  That gap is also the only
        moment the LCD may be written to.                                */
    gap_ms = ms >> 4;
    if (gap_ms < 8)         gap_ms = 8;     /* room for a display update */
    if (gap_ms > (ms >> 1)) gap_ms = ms >> 1;

    Tone_On(note);
    sound_ms = ms - gap_ms;
    phase    = 0;
    ms_count = sound_ms;
}


void Music_Init(void)
{
    TMOD = 0x11;                /* Timer 0 and Timer 1, both 16 bit */

    TR0  = 0;
    BEEP = 0;

    TH1  = 0xFC;                /* start the 1 ms tick */
    TL1  = 0x67;
    TR1  = 1;

    ms_count    = 0;
    g_ui_ms     = 0;
    half_period = TONE[24];     /* C5, a sane starting value */

    ET0 = 1;
    ET1 = 1;
    EA  = 1;

    Music_Load(0);
}


void Music_Load(unsigned char s)
{
    if (s >= SONG_COUNT) s = 0;

    cur_song = s;
    song     = (s == 0) ? SONG0 : SONG1;
    unit_ms  = SONG_UNITMS[s];

    idx      = 0;
    pos      = 0;
    phase    = 0;
    ms_count = 0;
    state    = MUS_STOP;
    Tone_Off();
}

unsigned char Music_State(void)   { return state;    }
unsigned int  Music_Pos(void)     { return pos;      }
unsigned int  Music_Total(void)   { return SONG_UNITS[cur_song]; }
unsigned char Music_UnitMs(void)  { return unit_ms;  }

unsigned char code *Music_Name(unsigned char s)
{
    return (s == 0) ? SONG_NAME0 : SONG_NAME1;
}

unsigned char Music_BusFree(void)
{
    if (state != MUS_PLAY) return 1;        /* not playing: pin is free */
    return (unsigned char)(phase != 0);     /* silent part of a note    */
}


void Music_Start(void)
{
    if (state == MUS_PLAY) return;

    if (state == MUS_STOP)
    {
        idx   = 0;                          /* restart from the beginning */
        pos   = 0;
        phase = 0;
    }
    state = MUS_PLAY;

    if (song[idx] == N_END)                 /* nothing to play */
    {
        state = MUS_STOP;
        return;
    }
    Start_Event();
}

void Music_Pause(void)
{
    if (state != MUS_PLAY) return;
    state = MUS_PAUSE;
    Tone_Off();
}

void Music_Resume(void)
{
    if (state != MUS_PAUSE) return;
    state = MUS_PLAY;

    if (song[idx] == N_END)
    {
        state = MUS_STOP;
        return;
    }
    Start_Event();
}

void Music_Halt(void)
{
    state    = MUS_STOP;
    ms_count = 0;
    Tone_Off();
}


void Music_Seek(int units)
{
    int           target = (int)pos + units;
    unsigned int  i      = 0;
    unsigned int  acc    = 0;
    unsigned char resume = (unsigned char)(state == MUS_PLAY);

    if (target < 0) target = 0;

    while (acc < (unsigned int)target && song[i] != N_END)
    {
        acc += song[i + 1];
        i   += 2;
    }

    idx      = i;
    pos      = acc;
    ms_count = 0;
    phase    = 0;
    Tone_Off();

    if (song[idx] == N_END)                 /* seeked past the end */
    {
        state = MUS_STOP;
        return;
    }
    if (resume)
    {
        Start_Event();
    }
}


void Music_Run(void)
{
    if (state != MUS_PLAY) return;
    if (ms_count != 0)     return;          /* still busy with this note */

    if (phase == 0)                         /* note finished -> the gap */
    {
        Tone_Off();
        phase    = 1;
        ms_count = gap_ms;
        return;
    }

    /* the gap is over, move on */
    if (song[idx] == N_END)
    {
        state = MUS_STOP;
        Tone_Off();
        return;
    }
    Start_Event();
}
