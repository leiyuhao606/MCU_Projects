#ifndef __LCD1602_H__
#define __LCD1602_H__

void LCD1602_WriteCommand(unsigned char Command);
void LCD1602_WriteData(unsigned char Data);

void LCD1602_Init(void);
void LCD1602_SetCursor(unsigned char Line, unsigned char Column);
void LCD1602_ShowChar(unsigned char Line, unsigned char Column, unsigned char Char);
void LCD1602_ShowString(unsigned char Line, unsigned char Column, unsigned char code *String);

/*  store a custom 5x8 pattern (8 bytes) in character generator slot 0..7  */
void LCD1602_LoadChar(unsigned char Location, unsigned char code *Pattern);

#endif
