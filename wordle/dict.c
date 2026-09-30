#include <REGX52.H>
#include "dict.h"
#include "words.h"

/*  The dictionary is stored as two tables of 24 bit word codes, sorted
    ascending and delta + varint compressed (see words.h).

    A table looks like:
        byte 0..2     the first code, stored absolutely (big endian)
        then          one varint per following entry: the gap to the
                      previous code, 7 bits per byte, most significant
                      group first, 0x80 set while more bytes follow

    This costs about 1.7 bytes per word instead of 5, which is what makes
    a dictionary of several thousand words fit into the 8 KB flash.       */

#define DICT_NO_CODE    0xFFFFFFFFUL

/* 26^4, 26^3, 26^2, 26^1, 26^0 - used to decode a code back to letters
   by repeated subtraction, which avoids pulling in the 32 bit division
   library routine.                                                      */
static unsigned long code POW26[WORD_LEN] =
{
    456976UL, 17576UL, 676UL, 26UL, 1UL
};


unsigned long Dict_Code(unsigned char idata *w)
{
    unsigned long c = 0;
    unsigned char i;
    unsigned int  d;

    for (i = 0; i < WORD_LEN; i++)
    {
        d = (unsigned int)(w[i] - 'A');
        /* c = c * 26 + d.  26 = 2 + 8 + 16, so the multiply is written as
           three shifts and two adds and no multiplication routine is
           needed.  Do NOT add a plain c here - that would give 27*c.  */
        c = (c << 1) + (c << 3) + (c << 4) + (unsigned long)d;
    }
    return c;
}


/*  Walk one table.
    by_index = 1 : v is an entry number, the code of that entry is returned
    by_index = 0 : v is a code, the first entry >= v is returned, or
                   DICT_NO_CODE when every entry is smaller.
    Because the entries are sorted this early exit is also what makes a
    failed lookup cheap.                                                  */
static unsigned long Dict_Walk(unsigned char code *tbl, unsigned int len,
                               unsigned long v, unsigned char by_index)
{
    unsigned int  i;
    unsigned char b;
    unsigned long acc, gap;

    /* first entry is stored absolutely */
    acc = ((unsigned long)tbl[0] << 16) |
          ((unsigned long)tbl[1] << 8)  |
           (unsigned long)tbl[2];
    i = 3;

    /* one single decode loop serves both modes, which keeps the code small */
    while (i < len)
    {
        if (by_index)
        {
            if (v == 0) break;              /* reached the wanted entry */
        }
        else
        {
            if (acc >= v) break;            /* first entry not smaller than v */
        }

        gap = 0;
        do
        {
            b = tbl[i];
            i++;
            gap = (gap << 7) | (unsigned long)(b & 0x7F);
        } while (((b & 0x80) != 0) && (i < len));

        acc += gap;
        if (by_index) v--;
    }

    if (by_index) return acc;
    return (acc >= v) ? acc : DICT_NO_CODE;
}


unsigned char Dict_Has(unsigned char idata *w)
{
    unsigned long c = Dict_Code(w);

    if (Dict_Walk(DICT_A, DICT_A_LEN, c, 0) == c) return 1;
    if (Dict_Walk(DICT_B, DICT_B_LEN, c, 0) == c) return 1;
    return 0;
}


void Dict_Answer(unsigned char idata *out, unsigned int idx)
{
    unsigned long c;
    unsigned long p;
    unsigned char i, n;

    c = Dict_Walk(DICT_A, DICT_A_LEN, (unsigned long)idx, 1);

    for (i = 0; i < WORD_LEN; i++)
    {
        p = POW26[i];
        n = 0;
        while (c >= p)
        {
            c -= p;
            n++;
        }
        out[i] = (unsigned char)('A' + n);
    }
}


void Dict_RandomAnswer(unsigned char idata *out, unsigned int rnd)
{
    /* rnd modulo ANSWER_COUNT, written as a subtraction loop so that the
       32 bit division library routine is not needed */
    while (rnd >= ANSWER_COUNT)
    {
        rnd -= ANSWER_COUNT;
    }
    Dict_Answer(out, rnd);
}
