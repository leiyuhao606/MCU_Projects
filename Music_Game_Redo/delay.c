#include <REGX52.H>
#include <intrins.h>

/*  1 ms blocking delay, calibrated for an 11.0592 MHz crystal.
    The schematic of the PuZhong-2/3/4 board shows an 11.0592 MHz crystal,
    and this is the same code the teach_project test project uses.        */
void Delay1ms(void)
{
    unsigned char data i, j;

    _nop_();
    i = 2;
    j = 199;
    do
    {
        while (--j);
    } while (--i);
}

void Delay_ms(unsigned char ms)
{
    while (ms--)
    {
        Delay1ms();
    }
}
