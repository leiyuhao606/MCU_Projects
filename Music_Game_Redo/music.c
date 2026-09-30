/* Buzzer music engine for the STC89C52RC (11.0592 MHz).
 *
 * Timer 0 makes the square wave on P2.5.  Timer 1 supplies a 1 ms clock and
 * the game clock; the foreground calls Music_Run() against absolute note
 * deadlines.  P2.5 is also LCD1602 RW, so the renderer mutes Timer 0 briefly
 * while it transfers a frame. */

#include <REGX52.H>
#include "music.h"
#include "songs.h"

sbit BEEP = P2^5;

static unsigned char code *song;
static unsigned int  idx;
static unsigned char state;
static unsigned char phase;       /* 0 = sounding, 1 = silent gap */
static unsigned char unit_ms;
static unsigned int  gap_ms;
static unsigned int  half_period;
/* Timer 1 owns the millisecond clock.  The foreground music scheduler uses
 * this clock and an absolute deadline, so a long LCD transaction does not
 * stretch the tune and the interrupt never calls into the C51 state machine. */
static unsigned int  music_time_ms;
static unsigned int  music_deadline_ms;
static unsigned int  music_clock_tick;
static unsigned char clock_div;
static unsigned char display_muted;

unsigned int g_ui_ms;

void Timer0_ISR(void) interrupt 1
{
    TH0 = (unsigned char)(half_period >> 8);
    TL0 = (unsigned char)half_period;
    BEEP = !BEEP;
}

void Timer1_ISR(void) interrupt 3
{
    TH1 = 0xFC;
    TL1 = 0x67;
    if (state == MUS_PLAY)
    {
        music_time_ms++;
        if (++clock_div >= 4)
        {
            clock_div = 0;
            music_clock_tick++;
        }
    }
    if (g_ui_ms != 0) g_ui_ms--;
}

static void Tone_On(unsigned char note)
{
    unsigned char saved_ea = EA;

    /* half_period is read by Timer 0.  Keep the 16-bit update and reload
     * atomic; otherwise a Timer 0 interrupt can observe one old and one new
     * byte and produce an invalid/repeating tone. */
    EA = 0;
    if (note >= 37)
    {
        EA = saved_ea;
        return;
    }
    half_period = TONE[note];
    TH0 = (unsigned char)(half_period >> 8);
    TL0 = (unsigned char)half_period;
    if (!display_muted) TR0 = 1;
    EA = saved_ea;
}

static void Tone_Off(void)
{
    unsigned char saved_ea = EA;

    EA = 0;
    TR0  = 0;
    BEEP = 0;
    EA = saved_ea;
}

static void Start_Event_At(unsigned int start_ms)
{
    unsigned char note;
    unsigned char units;
    unsigned int  ms;

    /* Every caller checks the terminator first, but keep this guard here as
     * well so a damaged index can only stop the song instead of indexing
     * outside the code table and jumping to a random result page. */
    if (song == 0 || idx >= 65534U || song[idx] == N_END)
    {
        state = MUS_STOP;
        gap_ms = 0;
        Tone_Off();
        return;
    }

    note  = song[idx];
    units = song[idx + 1];
    idx += 2;
    ms = (unsigned int)units * unit_ms;

    if (note == N_REST)
    {
        Tone_Off();
        phase    = 1;
        gap_ms   = ms;
        music_deadline_ms = (unsigned int)(start_ms + ms);
        return;
    }

    if (note >= 37)
    {
        state = MUS_STOP;
        gap_ms = 0;
        Tone_Off();
        return;
    }

    /* Keep a visible, quiet bus window after every note.  The minimum is
     * deliberately a little longer than the fast LCD frame transaction. */
    gap_ms = ms >> 4;
    if (gap_ms < 8) gap_ms = 8;
    if (gap_ms > (ms >> 1)) gap_ms = ms >> 1;

    Tone_On(note);
    phase    = 0;
    music_deadline_ms = (unsigned int)(start_ms + ms - gap_ms);
}

void Music_Init(void)
{
    TMOD = 0x11;

    TR0  = 0;
    BEEP = 0;

    TH1  = 0xFC;
    TL1  = 0x67;
    TR1  = 1;

    music_time_ms  = 0;
    music_deadline_ms = 0;
    g_ui_ms        = 0;
    music_clock_tick = 0;
    clock_div = 0;
    display_muted   = 0;
    half_period    = TONE[24];

    ET0 = 1;
    ET1 = 1;
    /* Finish loading the first table before either timer can observe the
     * partially initialized song state. */
    EA  = 0;
    Music_Load(0);
    EA  = 1;
}

void Music_Load(unsigned char s)
{
    unsigned char saved_ea = EA;

    EA = 0;
    if (s >= SONG_COUNT) s = 0;

    song           = (s == 0) ? SONG0 : SONG1;
    unit_ms        = SONG_UNITMS[s];
    idx            = 0;
    phase          = 0;
    music_time_ms  = 0;
    music_deadline_ms = 0;
    music_clock_tick = 0;
    clock_div = 0;
    display_muted = 0;
    state          = MUS_STOP;
    Tone_Off();
    EA = saved_ea;
}

unsigned char Music_State(void)  { return state;   }
unsigned char Music_UnitMs(void) { return unit_ms; }

/* A short atomic copy prevents a Timer 1 carry from producing a torn value. */
unsigned int Music_ClockTick(void)
{
    unsigned int value;
    unsigned char saved_ea = EA;

    EA = 0;
    value = music_clock_tick;
    EA = saved_ea;
    return value;
}

static unsigned int Music_TimeMs(void)
{
    unsigned int value;
    unsigned char saved_ea = EA;

    EA = 0;
    value = music_time_ms;
    EA = saved_ea;
    return value;
}

static unsigned char Deadline_Reached(unsigned int now,
                                      unsigned int deadline)
{
    /* All individual note phases are well below 32768 ms, so the signed
     * modulo comparison remains valid across the 16-bit clock wrap. */
    return (unsigned char)(((signed int)(now - deadline)) >= 0);
}

/* P2.5 is shared by the buzzer and LCD RW.  A complete pixel frame needs
 * several LCD transactions, so the game briefly mutes Timer 0 around that
 * transfer instead of waiting for the relatively infrequent note gaps. */
void Music_DisplayBegin(void)
{
    unsigned char saved_ea = EA;

    EA = 0;
    display_muted = 1;
    if (state == MUS_PLAY && phase == 0)
    {
        TR0  = 0;
        BEEP = 0;
    }
    EA = saved_ea;
}

void Music_DisplayEnd(void)
{
    unsigned char saved_ea = EA;

    EA = 0;
    display_muted = 0;
    if (state == MUS_PLAY && phase == 0)
    {
        TH0 = (unsigned char)(half_period >> 8);
        TL0 = (unsigned char)half_period;
        TR0 = 1;
    }
    EA = saved_ea;
}

unsigned char code *Music_Name(unsigned char s)
{
    return (s == 0) ? SONG_NAME0 : SONG_NAME1;
}

void Music_Start(void)
{
    unsigned char saved_ea;

    if (state == MUS_PLAY) return;

    saved_ea = EA;
    EA = 0;

    /* A start always begins a clean run.  MUS_PAUSE is not exposed by the
     * game, and treating it like STOP avoids resuming from a stale pointer. */
    idx            = 0;
    phase          = 0;
    music_time_ms  = 0;
    music_deadline_ms = 0;
    music_clock_tick = 0;
    clock_div = 0;
    state = MUS_PLAY;

    if (song == 0 || song[idx] == N_END)
    {
        state = MUS_STOP;
        EA = saved_ea;
        return;
    }
    Start_Event_At(0);
    EA = saved_ea;
}

void Music_Halt(void)
{
    unsigned char saved_ea = EA;

    EA = 0;
    state    = MUS_STOP;
    music_deadline_ms = 0;
    display_muted = 0;
    Tone_Off();
    EA = saved_ea;
}

void Music_Run(void)
{
    unsigned int now;
    unsigned char steps;

    if (state != MUS_PLAY) return;

    now = Music_TimeMs();

    /* The loop normally executes once.  A bounded catch-up handles a
     * foreground LCD transfer that crossed several short phases while still
     * guaranteeing that a malformed table cannot trap the main loop.  Each
     * new deadline is based on the previous deadline, preserving the original
     * song tempo instead of restarting the phase from a late foreground time. */
    for (steps = 0; steps < 8; steps++)
    {
        if (!Deadline_Reached(now, music_deadline_ms)) return;

        if (phase == 0)
        {
            Tone_Off();
            phase = 1;
            music_deadline_ms = (unsigned int)(music_deadline_ms + gap_ms);
            continue;
        }

        if (song == 0 || song[idx] == N_END)
        {
            state = MUS_STOP;
            Tone_Off();
            return;
        }
        Start_Event_At(music_deadline_ms);
        if (state != MUS_PLAY) return;
    }
}
