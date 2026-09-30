#include <REGX52.H>
#include "lcd1602.h"
#include "delay.h"

/*  LCD1602 wiring on the PuZhong-2/3/4 board, confirmed against the
    schematic ("LCD1602" connector):

        pin 4  RS -> P2.6  (net LCD_RS)
        pin 5  RW -> P2.5  (net LCD_WR)   <- shared with the buzzer !
        pin 6  E  -> P2.7  (net LCD_EN)
        pin 7-14  D0-D7 -> P0 (nets LCD_D0 .. LCD_D7)                   */
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
    LCD1602_WriteCommand(0x01);     /* clear                        */
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

/*  Copy an 8 byte pattern into one of the eight character generator slots.
    Slot 0..7 is then displayed by sending the byte 0..7 to the display.    */
void LCD1602_LoadChar(unsigned char Location, unsigned char code *Pattern)
{
    unsigned char i;

    LCD1602_WriteCommand(0x40 | ((Location & 0x07) << 3));
    for (i = 0; i < 8; i++)
    {
        LCD1602_WriteData(Pattern[i]);
    }
}
