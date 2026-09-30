#include <REGX52.H>
#include "keypad.h"
#include "delay.h"

/*  Independent keys, active low (10K pull-ups, RP7/RP12/RP14 on the board).

    !! P3.2 IS SHARED WITH THE INFRARED RECEIVER !!
    Net P32 carries both button K3 and pin 1 (OUT) of the HS0038 infrared
    remote receiver IR1 - see the schematic block "IR1 / HS0038", whose OUT
    pin is labelled P32.  That receiver pulls its output low in bursts
    whenever it sees modulated infrared light (fluorescent or LED room
    lighting, sunlight, any IR remote), and read as a plain level every
    burst looks like a key press.

    So a key on port 3 is only accepted when its pin stays low WITHOUT A
    SINGLE TRANSITION for 30 ms - a push button does that, a pulse train
    never does - and the pin must then be steadily high for 20 ms before the
    same key can fire again, which stops even a slow burst from producing a
    stream of events.
    K4 on P3.3 has nothing else on it and is always clean.               */
sbit IND_MODE  = P3^0;  /* K2 */
sbit IND_PREV  = P3^1;  /* K1, shared with TXD */
sbit IND_NEXT  = P3^2;  /* K3, shared with IR1 */
sbit IND_STOP  = P3^3;  /* K4 */

static unsigned char key_latch = 0;


void Key_Init(void)
{
    IND_MODE = 1;
    IND_PREV = 1;
    IND_NEXT = 1;
    IND_STOP = 1;
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

static unsigned char Key_Is_Pressed(unsigned char k)
{
    if (k == KEY_MODE) return Pin_Steady(IND_MODE, 0, 30);
    if (k == KEY_PREV) return Pin_Steady(IND_PREV, 0, 30);
    if (k == KEY_STOP) return Pin_Steady(IND_STOP, 0, 30);
    return Pin_Steady(IND_NEXT, 0, 30);
}

static unsigned char Key_Is_Free(unsigned char k)
{
    if (k == KEY_MODE) return Pin_Steady(IND_MODE, 1, 20);
    if (k == KEY_PREV) return Pin_Steady(IND_PREV, 1, 20);
    if (k == KEY_STOP) return Pin_Steady(IND_STOP, 1, 20);
    return Pin_Steady(IND_NEXT, 1, 20);
}

/* read the keys without any debounce: 0 when nothing is pressed */
static unsigned char Key_Raw(void)
{
    if (IND_MODE == 0) return KEY_MODE;
    if (IND_PREV == 0) return KEY_PREV;
    if (IND_NEXT == 0) return KEY_NEXT;     /* also the IR receiver */
    if (IND_STOP == 0) return KEY_STOP;
    return KEY_NONE;
}

unsigned char Key_Scan(void)
{
    unsigned char k = Key_Raw();

    if (k == KEY_NONE)
    {
        /*  re-arm only once the pin is genuinely idle, so a noisy pin
            cannot keep producing events                                 */
        if (key_latch != 0)
        {
            if (Key_Is_Free(key_latch)) key_latch = 0;
        }
        return KEY_NONE;
    }

    if (k == key_latch) return KEY_NONE;    /* still the same press */

    Delay_ms(10);                           /* let the contact settle */

    if (Key_Raw() != k) return KEY_NONE;

    if (Key_Is_Pressed(k) == 0) return KEY_NONE;    /* a pulse train */

    key_latch = k;
    return k;
}
