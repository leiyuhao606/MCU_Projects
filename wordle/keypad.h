#ifndef __KEYPAD_H__
#define __KEYPAD_H__

/*  Key codes returned by Key_Scan().
    Matrix keys 1..16 are numbered exactly like the teach_project
    reference project, so key 1 is the same physical button here.       */
#define KEY_NONE   0    /* no new key event                        */
#define KEY_PAGE   14   /* matrix 14 : letter page A-M <-> N-Z     */
#define KEY_BACK   15   /* matrix 15 : backspace                   */
#define KEY_OK     16   /* matrix 16 : commit letter / submit word */

#define KEY_MODE   17   /* independent P3.0 (K2) : play <-> history view */
#define KEY_PREV   18   /* independent P3.1 (K1) : previous history      */
#define KEY_NEXT   19   /* independent P3.2 (K3) : next history          */
                        /*   ... and also P3.3 (K4), because P3.2 is     */
                        /*   shared with the on-board infrared receiver, */
                        /*   which pulses on ambient IR light.  K4 is on */
                        /*   a pin with nothing else on it and is always */
                        /*   reliable - use K4 if K3 ever misbehaves.    */

void Key_Init(void);
unsigned char Key_Scan(void);

#endif
