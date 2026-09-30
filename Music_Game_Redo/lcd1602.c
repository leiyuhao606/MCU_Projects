#include <REGX52.H>
#include <intrins.h>
#include "lcd1602.h"
#include "delay.h"

/* LCD1602 wiring on the PuZhong board:
 * RS -> P2.6, RW -> P2.5, E -> P2.7, D0..D7 -> P0.  P2.5 is also the
 * buzzer output.  The game mutes Timer 0 briefly while it transfers a frame. */
sbit LCD1602_RS = P2^6;
sbit LCD1602_RW = P2^5;
sbit LCD1602_E  = P2^7;

#define LCD1602_DATAPORT P0

/* A normal HD44780 data/command write needs roughly 37 us.  The original
 * player used a millisecond delay for every byte, which makes a pixel frame
 * too slow; this calibrated short wait keeps the shared buzzer pin quiet for
 * only the actual LCD transaction.  Clear/home still receive a full delay. */
static void LCD1602_ShortWait(void)
{
    unsigned char i = 12;

    while (i--)
    {
        _nop_();
    }
}

void LCD1602_WriteCommand(unsigned char Command)
{
    LCD1602_RS = 0;
    LCD1602_RW = 0;
    LCD1602_DATAPORT = Command;
    LCD1602_E = 1;
    LCD1602_ShortWait();
    LCD1602_E = 0;
    LCD1602_ShortWait();
}

void LCD1602_WriteData(unsigned char Data)
{
    LCD1602_RS = 1;
    LCD1602_RW = 0;
    LCD1602_DATAPORT = Data;
    LCD1602_E = 1;
    LCD1602_ShortWait();
    LCD1602_E = 0;
    LCD1602_ShortWait();
}

void LCD1602_Init(void)
{
    LCD1602_WriteCommand(0x38);     /* 8-bit bus, 2 lines, 5x7 dots */
    Delay_ms(1);
    LCD1602_WriteCommand(0x0C);     /* display on, cursor off       */
    Delay_ms(1);
    LCD1602_WriteCommand(0x06);     /* auto increment, no shift     */
    Delay_ms(1);
    LCD1602_WriteCommand(0x01);     /* clear                        */
    Delay_ms(2);
}

void LCD1602_SetCursor(unsigned char Line, unsigned char Column)
{
    if (Line == 1)
    {
        LCD1602_WriteCommand(0x80 | (Column - 1));
    }
    else
    {
        LCD1602_WriteCommand(0x80 | (Column - 1) + 0x40);
    }
}

void LCD1602_ShowChar(unsigned char Line, unsigned char Column, unsigned char Char)
{
    LCD1602_SetCursor(Line, Column);
    LCD1602_WriteData(Char);
}

void LCD1602_ShowString(unsigned char Line, unsigned char Column, unsigned char code *String)
{
    unsigned char i;

    LCD1602_SetCursor(Line, Column);
    for (i = 0; String[i] != '\0'; i++)
    {
        LCD1602_WriteData(String[i]);
    }
}

/* Dynamic game frames are assembled in internal RAM.  Keil C51 keeps code
 * and data pointers distinct, so provide a RAM variant instead of casting a
 * pointer across memory spaces. */
void LCD1602_LoadCharRam(unsigned char Location, unsigned char idata *Pattern)
{
    unsigned char i;

    LCD1602_WriteCommand(0x40 | ((Location & 0x07) << 3));
    for (i = 0; i < 8; i++)
    {
        LCD1602_WriteData(Pattern[i]);
    }
}
