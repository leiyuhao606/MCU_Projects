#ifndef __KEYPAD_H__
#define __KEYPAD_H__

/*  The four independent keys on port 3 (schematic: K1..K4).
    They are active low with 10K pull-up networks.                        */
#define KEY_NONE   0
#define KEY_MODE   1        /* P3.0 (K2): selection -> play, play <-> pause */
#define KEY_PREV   2        /* P3.1 (K1): previous song, rewind 10 s        */
#define KEY_NEXT   3        /* P3.2 (K3): next song, forward 10 s           */
#define KEY_STOP   4        /* P3.3 (K4): stop, back to the selection screen*/

void          Key_Init(void);
unsigned char Key_Scan(void);

#endif
