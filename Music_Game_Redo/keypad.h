#ifndef __KEYPAD_H__
#define __KEYPAD_H__

/*
 * The independent-key block on the PuZhong board is active low.  In the
 * schematic the physical keys are K1, K2, K3, K4 from left to right and are
 * wired to P3.1, P3.0, P3.2 and P3.3 respectively.  The game uses the
 * physical order (right to left) for its four lanes, so this mapping is kept
 * explicit here rather than relying on the key labels.
 */
#define KEY_NONE   0
#define KEY_MODE   1        /* K2, P3.0: selection -> start */
#define KEY_PREV   2        /* K1, P3.1: previous song */
#define KEY_NEXT   3        /* K3, P3.2: next song */
#define KEY_STOP   4        /* K4, P3.3: stop/back */

/* bit values returned by Key_ReadMask(), in port-bit order */
#define KEY_MASK_MODE  0x01 /* P3.0 / K2 */
#define KEY_MASK_PREV  0x02 /* P3.1 / K1 */
#define KEY_MASK_NEXT  0x04 /* P3.2 / K3 */
#define KEY_MASK_STOP  0x08 /* P3.3 / K4 */

void          Key_Init(void);
unsigned char Key_Scan(void);       /* debounced single-key event */
unsigned char Key_ReadMask(void);   /* instantaneous active-low mask */

#endif
