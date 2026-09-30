#ifndef __DICT_H__
#define __DICT_H__

/*  Every word in this game is five letters long. */
#define WORD_LEN    5

/*  24 bit base-26 code of a word:  'A'=0 ... 'Z'=25,
    code = ((((c0*26 + c1)*26 + c2)*26 + c3)*26 + c4)
    which is unique for every 5 letter combination and always fits in 24 bits
    (26^5 = 11881376 < 2^24).                                                */
unsigned long Dict_Code(unsigned char idata *w);

/*  1 when the word is in the dictionary, 0 when it is not.  */
unsigned char Dict_Has(unsigned char idata *w);

/*  Copy answer number idx (0 .. ANSWER_COUNT-1) into out[0..4].  */
void Dict_Answer(unsigned char idata *out, unsigned int idx);

/*  Pick a random answer from the answer pool.  rnd may be any 16 bit
    value, for example the free running timer.                        */
void Dict_RandomAnswer(unsigned char idata *out, unsigned int rnd);

#endif
