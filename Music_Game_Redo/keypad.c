#include <REGX52.H>
#include "keypad.h"
#include "delay.h"

/* K1..K4 are physically left-to-right on the board, but their port bits are
 * P3.1, P3.0, P3.2, P3.3.  Keep the two notions separate in the comments and
 * in the game row map. */
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

/* The chart input path samples this mask once per millisecond and applies its
 * own multi-key stability filter.  That lets two physically independent keys
 * be recognised together, which Key_Scan intentionally cannot do. */
unsigned char Key_ReadMask(void)
{
    unsigned char mask = 0;

    if (IND_MODE == 0) mask |= KEY_MASK_MODE;
    if (IND_PREV == 0) mask |= KEY_MASK_PREV;
    if (IND_NEXT == 0) mask |= KEY_MASK_NEXT;
    if (IND_STOP == 0) mask |= KEY_MASK_STOP;
    return mask;
}

/* Watch one pin for ms milliseconds and report whether it stayed at the
 * wanted level without a transition.  P3.2 is also the IR receiver output;
 * this rejects its short pulse trains on the selection screen. */
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

static unsigned char Key_Raw(void)
{
    if (IND_MODE == 0) return KEY_MODE;
    if (IND_PREV == 0) return KEY_PREV;
    if (IND_NEXT == 0) return KEY_NEXT;
    if (IND_STOP == 0) return KEY_STOP;
    return KEY_NONE;
}

unsigned char Key_Scan(void)
{
    unsigned char k = Key_Raw();

    if (k == KEY_NONE)
    {
        if (key_latch != 0)
        {
            if (Key_Is_Free(key_latch)) key_latch = 0;
        }
        return KEY_NONE;
    }

    if (k == key_latch) return KEY_NONE;

    Delay_ms(10);
    if (Key_Raw() != k) return KEY_NONE;
    if (Key_Is_Pressed(k) == 0) return KEY_NONE;

    key_latch = k;
    return k;
}
