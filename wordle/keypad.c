#include <REGX52.H>
#include "keypad.h"
#include "delay.h"

/*  4x4 matrix keypad on P1 (schematic header J20, pins 1..8 -> P1.7..P1.0).
    One row is pulled low at a time and the four columns are read:

             P1.7  P1.6  P1.5  P1.4      <- columns
    P1.3  ->   1     5     9    13
    P1.2  ->   2     6    10    14
    P1.1  ->   3     7    11    15
    P1.0  ->   4     8    12    16
             ^
             rows (driven low)

    Keys 1..13 are the letter keys, 14 = page, 15 = backspace, 16 = enter.  */
sbit KP_R3 = P1^3;
sbit KP_R2 = P1^2;
sbit KP_R1 = P1^1;
sbit KP_R0 = P1^0;

sbit KP_C3 = P1^7;
sbit KP_C2 = P1^6;
sbit KP_C1 = P1^5;
sbit KP_C0 = P1^4;

/*  Independent keys, active low.  The board has 10K pull-up networks
    (RP7/RP12/RP14 on the schematic), so writing 1 makes them inputs.

    !! P3.2 IS SHARED WITH THE INFRARED RECEIVER !!
    On this board the net P32 carries both button K3 and pin 1 (OUT) of the
    HS0038 infrared remote receiver IR1 - see the schematic, block "IR1 /
    HS0038", whose OUT pin is labelled P32.  That receiver pulls its output
    low in bursts whenever it sees modulated infrared light: fluorescent or
    LED room lighting, sunlight, or any IR remote control.  Read as a plain
    level, every burst looks like "next entry" being pressed, which made the
    history screen page through the entries all by itself.
    The history screen was the only place it showed up because in play mode
    the KEY_PREV / KEY_NEXT codes are simply ignored.

    Two defences are used here:
      * a key on port 3 is only accepted when its pin stays low WITHOUT A
        SINGLE TRANSITION for about 30 ms - a push button does that, a pulse
        train never does, and
      * the pin must then be steadily high again for 20 ms before the key can
        fire a second time, so even a slow burst cannot produce a stream of
        events.
    In addition K4 on P3.3 is accepted as a second "next" key.  P3.3 has
    nothing else connected to it, so if the IR receiver ever manages to hold
    P3.2 low solidly, K4 still works perfectly.                          */
sbit IND_MODE  = P3^0;  /* K2 : switch view            */
sbit IND_PREV  = P3^1;  /* K1 : previous history entry */
sbit IND_NEXT  = P3^2;  /* K3 : next history entry (shared with IR1)  */
sbit IND_NEXT2 = P3^3;  /* K4 : also next history entry (always clean) */

/* remembers the key that is currently held down, so that one press
   produces exactly one event (no auto repeat)                        */
static unsigned char key_latch = 0;


void Key_Init(void)
{
    P1 = 0xFF;
    IND_MODE  = 1;
    IND_PREV  = 1;
    IND_NEXT  = 1;
    IND_NEXT2 = 1;
    key_latch = 0;
}

/*  Watch one pin for ms milliseconds and report whether it stayed at the
    wanted level the whole time without a single transition.              */
static unsigned char Pin_Steady(bit pin, bit level, unsigned char ms)
{
    unsigned char i, changes = 0;
    bit last = pin;

    for (i = 0; i < ms; i++)
    {
        Delay1ms();
        if (pin != last)
        {
            changes++;
            last = pin;
        }
    }
    return (unsigned char)((changes == 0) && (pin == level));
}

/* is this independent key really being held down? */
static unsigned char Key_Is_Pressed(unsigned char k)
{
    if (k == KEY_MODE) return Pin_Steady(IND_MODE, 0, 30);
    if (k == KEY_PREV) return Pin_Steady(IND_PREV, 0, 30);
    if (IND_NEXT2 == 0) return Pin_Steady(IND_NEXT2, 0, 30);
    return Pin_Steady(IND_NEXT, 0, 30);
}

/*  Has this independent key really been let go of, and stayed let go of?
    Requiring the pin to be steadily HIGH for 20 ms is what stops a slow
    burst - say 40 ms low followed by 10 ms high - from re-arming the key
    and producing one event after another.                                */
static unsigned char Key_Is_Free(unsigned char k)
{
    if (k == KEY_MODE) return Pin_Steady(IND_MODE, 1, 20);
    if (k == KEY_PREV) return Pin_Steady(IND_PREV, 1, 20);
    if (IND_NEXT2 == 0) return 0;               /* K4 still held down */
    return Pin_Steady(IND_NEXT, 1, 20);
}

/* read the keypad without any debounce: 0 when nothing is pressed */
static unsigned char Key_Raw(void)
{
    unsigned char k = 0;

    /* independent keys first, they are a plain level on P3 */
    if (IND_MODE == 0) return KEY_MODE;
    if (IND_PREV == 0) return KEY_PREV;
    if (IND_NEXT == 0) return KEY_NEXT;     /* K3, shared with the IR receiver */
    if (IND_NEXT2 == 0) return KEY_NEXT;    /* K4, a pin with nothing else on it */

    P1 = 0xFF;
    KP_R3 = 0;
    if      (KP_C3 == 0) k = 1;
    else if (KP_C2 == 0) k = 5;
    else if (KP_C1 == 0) k = 9;
    else if (KP_C0 == 0) k = 13;

    if (k == 0)
    {
        P1 = 0xFF;
        KP_R2 = 0;
        if      (KP_C3 == 0) k = 2;
        else if (KP_C2 == 0) k = 6;
        else if (KP_C1 == 0) k = 10;
        else if (KP_C0 == 0) k = 14;
    }
    if (k == 0)
    {
        P1 = 0xFF;
        KP_R1 = 0;
        if      (KP_C3 == 0) k = 3;
        else if (KP_C2 == 0) k = 7;
        else if (KP_C1 == 0) k = 11;
        else if (KP_C0 == 0) k = 15;
    }
    if (k == 0)
    {
        P1 = 0xFF;
        KP_R0 = 0;
        if      (KP_C3 == 0) k = 4;
        else if (KP_C2 == 0) k = 8;
        else if (KP_C1 == 0) k = 12;
        else if (KP_C0 == 0) k = 16;
    }

    P1 = 0xFF;
    return k;
}

/*  Non blocking scan: returns KEY_NONE when there is no *new* press.
    A matrix key is accepted after it reads the same twice, 10 ms apart.
    An independent key needs more than that: its pin must stay low without
    any transition for 20 ms, and must be steadily high again before it can
    fire once more.  That is what keeps the infrared receiver on P3.2 from
    being mistaken for key presses.                                       */
unsigned char Key_Scan(void)
{
    unsigned char k = Key_Raw();

    if (k == 0)
    {
        if (key_latch >= KEY_MODE)
        {
            /*  independent key: re-arm only when its pin is genuinely idle.
                While the IR receiver keeps pulsing on P3.2 this never comes
                true, so one burst cannot produce a stream of events.       */
            if (Key_Is_Free(key_latch)) key_latch = 0;
        }
        else
        {
            key_latch = 0;              /* matrix key released - re-arm */
        }
        return KEY_NONE;
    }

    if (k == key_latch)
    {
        return KEY_NONE;            /* still the same press */
    }

    Delay_ms(10);                   /* let the contact bounce settle */

    if (Key_Raw() != k)
    {
        return KEY_NONE;            /* bounced away, ignore it */
    }

    if (k >= KEY_MODE)
    {
        if (Key_Is_Pressed(k) == 0)
        {
            return KEY_NONE;        /* a pulse train, not a button */
        }
    }

    key_latch = k;
    return k;
}
