#include <REGX52.H>
#include "lcd1602.h"
#include "delay.h"

/*  LCD1602 wiring on the PuZhong-2/3/4 board.
    Taken from the teach_project test project and confirmed against the
    schematic PDF ("LCD1602" connector):

        LCD pin 4  RS  -> P2.6   (net LCD_RS)
        LCD pin 5  RW  -> P2.5   (net LCD_WR)
        LCD pin 6  E   -> P2.7   (net LCD_EN)
        LCD pin 7-14 D0-D7 -> P0 (nets LCD_D0 .. LCD_D7)

    Note: P0 is also wired to the 7-segment display of the board.  This
    project therefore never drives the 7-segment digit select lines
    (P2.2-P2.4), so the two displays do not disturb each other.          */
sbit LCD1602_RS = P2^6;
sbit LCD1602_RW = P2^5;
sbit LCD1602_E  = P2^7;

#define LCD1602_DATAPORT P0


void LCD1602_WriteCommand(unsigned char Command)
{
    LCD1602_RS = 0;
    LCD1602_RW = 0;
    LCD1602_DATAPORT = Command;
    LCD1602_E = 1;
    Delay1ms();
    LCD1602_E = 0;
    Delay1ms();
}

void LCD1602_WriteData(unsigned char Data)
{
    LCD1602_RS = 1;
    LCD1602_RW = 0;
    LCD1602_DATAPORT = Data;
    LCD1602_E = 1;
    Delay1ms();
    LCD1602_E = 0;
    Delay1ms();
}

void LCD1602_Init(void)
{
    LCD1602_WriteCommand(0x38);     /* 8 bit bus, 2 lines, 5x7 dots */
    LCD1602_WriteCommand(0x0C);     /* display on, cursor off       */
    LCD1602_WriteCommand(0x06);     /* auto increment, no shift     */
    LCD1602_WriteCommand(0x01);     /* clear display                */
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

/* show one decimal digit, 0..9 */
void LCD1602_ShowDigit(unsigned char Line, unsigned char Column, unsigned char Number)
{
    LCD1602_ShowChar(Line, Column, (unsigned char)('0' + Number));
}
