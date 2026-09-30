/* ===========================================================================
   Wordle  -  STC89C52RC + LCD1602 + 4x4 matrix keypad + 3 independent keys
   Keil C51 (uVision 5)

   Hardware (PuZhong-2/3/4 board, verified against the schematic PDF):
     LCD1602 : data P0.0-P0.7, RS = P2.6, RW = P2.5, E = P2.7
     Keypad  : 4x4 matrix on P1.0-P1.7  (header J20)
     Keys    : P3.0 = play/history, P3.1 = previous entry, P3.2 = next entry
     Crystal : 11.0592 MHz

   Matrix key map (same numbering as the teach_project test project):
     1..13 = letters, 14 = flip letter page, 15 = backspace, 16 = enter
     page A-M : key n -> 'A' + (n-1)
     page N-Z : key n -> 'N' + (n-1)

   Screen layout
     line 1  "Wordle" (1-6) | page hint (8-10) | "n/6" (14-16)
     line 2  play, input  : [ ][ ][ ][ ][ ]  [ ]     boxes 1-5, staging box 7
     line 2  play, graded : [word 1-5]   [G/Y/R at 7-11]
     line 2  history      : n (1) | word (3-7) | marks (9-13)
     line 2  game over    : "Congratulation" from column 1 on a win,
                            "You lose! <answer>" from column 1 on a loss
     line 2  bad word     : "Invalid word" from column 1
   =========================================================================== */

#include <REGX52.H>
#include "lcd1602.h"
#include "keypad.h"
#include "delay.h"
#include "dict.h"

#define MAX_ROUND   6

/* column positions, 1 based, as used by LCD1602_SetCursor */
#define COL_HINT     8          /* page hint "A-M" / "N-Z"          */
#define COL_ROUND   14          /* "n/6"                            */
#define COL_INPUT    1          /* five input boxes                 */
#define COL_STAGE    7          /* single staging box               */
#define COL_MARK     7          /* five G/Y/R marks after grading   */
#define COL_HIST_W   3          /* word inside the history line     */
#define COL_HIST_M   9          /* marks inside the history line    */

/* what an empty box looks like */
#define BOX_EMPTY   '_'

/* set to 0 if you do not want the "A-M"/"N-Z" hint on line 1 */
#define SHOW_PAGE_HINT  1

/* view */
#define VIEW_PLAY    0
#define VIEW_HIST    1

/* state of the current game */
#define ST_INPUT     0          /* typing a word                        */
#define ST_RESULT    1          /* last word graded, waiting for key 16 */
#define ST_WIN       2
#define ST_LOSE      3
#define ST_INVALID   4          /* word is not in the dictionary        */

/* --------------------------------------------------------------------------
   game data
   The arrays live in idata so that the 128 byte direct data area stays free
   for the stack and for the compiler temporaries.
   -------------------------------------------------------------------------- */
static unsigned char idata guess_word[MAX_ROUND][WORD_LEN];
static unsigned char idata guess_mark[MAX_ROUND][WORD_LEN];
static unsigned char idata cur_word[WORD_LEN];      /* word being typed   */
static unsigned char idata answer[WORD_LEN];        /* the secret word    */

static unsigned char input_len;         /* 0..5 letters in the input boxes  */
static unsigned char stage_ch;          /* 0 = staging box empty            */
static unsigned char page;              /* 0 = A-M, 1 = N-Z                 */
static unsigned char view;              /* VIEW_PLAY / VIEW_HIST            */
static unsigned char hist_idx;          /* history entry being shown        */
static unsigned char rounds;            /* number of graded guesses, 0..6   */
static unsigned char state;             /* ST_xxx                           */
static unsigned char won;               /* last graded guess was correct    */
static unsigned char answer_ready;


/* ======================= drawing ========================================= */

static void Draw_Page(void)
{
#if SHOW_PAGE_HINT
    LCD1602_ShowChar (1, COL_HINT,     page ? 'N' : 'A');
    LCD1602_ShowChar (1, COL_HINT + 1, '-');
    LCD1602_ShowChar (1, COL_HINT + 2, page ? 'Z' : 'M');
#endif
}

/* the attempt counter.  While a graded word is on screen it shows the number
   of that word, otherwise it shows the attempt the player is about to make.
   A rejected word does not use up a try, so ST_INVALID counts as "about to". */
static void Draw_Round(void)
{
    unsigned char r;

    r = (state == ST_RESULT || state == ST_WIN || state == ST_LOSE)
        ? rounds : (unsigned char)(rounds + 1);
    if (r < 1)         r = 1;
    if (r > MAX_ROUND) r = MAX_ROUND;

    LCD1602_ShowDigit(1, COL_ROUND,     r);
    LCD1602_ShowChar (1, COL_ROUND + 1, '/');
    LCD1602_ShowDigit(1, COL_ROUND + 2, MAX_ROUND);
}

/* line 1 of the play screen */
static void Draw_PlayTop(void)
{
    LCD1602_ShowString(1, 1, "Wordle  ");   /* also wipes "History" */
    Draw_Page();
    Draw_Round();
}

/* the whole of line 2 for the current state, rebuilt from the game data */
static void Draw_Line2(void)
{
    unsigned char i;

    if (state == ST_WIN)
    {
        LCD1602_ShowString(2, 1, "Congratulation  ");
        return;
    }
    if (state == ST_INVALID)
    {
        LCD1602_ShowString(2, 1, "Invalid word    ");
        return;
    }
    if (state == ST_LOSE)
    {
        /*  the game is over, so show the word the player was looking for:
            "You lose! " fills columns 1..10 and the answer follows it      */
        LCD1602_ShowString(2, 1, "You lose! ");
        for (i = 0; i < WORD_LEN; i++)
        {
            LCD1602_ShowChar(2, 11 + i, answer[i]);
        }
        LCD1602_ShowChar(2, 16, ' ');
        return;
    }

    /* the five input boxes */
    for (i = 0; i < WORD_LEN; i++)
    {
        if (state == ST_RESULT || i < input_len)
        {
            LCD1602_ShowChar(2, COL_INPUT + i, cur_word[i]);
        }
        else
        {
            LCD1602_ShowChar(2, COL_INPUT + i, BOX_EMPTY);
        }
    }
    LCD1602_ShowChar(2, COL_INPUT + WORD_LEN, ' ');     /* column 6 */

    if (state == ST_RESULT)
    {
        /* the staging box and the four cells after it show the result */
        for (i = 0; i < WORD_LEN; i++)
        {
            LCD1602_ShowChar(2, COL_MARK + i, guess_mark[rounds - 1][i]);
        }
        /* blank the rest of the line: the history screen writes further
           right, so those cells must not be left behind */
        for (i = COL_MARK + WORD_LEN; i <= 16; i++)
        {
            LCD1602_ShowChar(2, i, ' ');
        }
    }
    else
    {
        LCD1602_ShowChar(2, COL_STAGE, stage_ch ? stage_ch : BOX_EMPTY);
        for (i = COL_STAGE + 1; i <= 16; i++)
        {
            LCD1602_ShowChar(2, i, ' ');
        }
    }
}

/* the whole of line 2 in history view */
static void Draw_History(void)
{
    unsigned char i;

    LCD1602_ShowString(1, 1, "History");
    LCD1602_ShowString(1, COL_HINT, "      ");      /* wipe the page hint */

    if (rounds == 0)
    {
        LCD1602_ShowString(2, 1, "0");
        LCD1602_ShowString(2, 2, "               ");
        return;
    }

    LCD1602_ShowDigit(2, 1, (unsigned char)(hist_idx + 1));
    LCD1602_ShowChar (2, 2, ' ');
    for (i = 0; i < WORD_LEN; i++)
    {
        LCD1602_ShowChar(2, COL_HIST_W + i, guess_word[hist_idx][i]);
    }
    LCD1602_ShowChar(2, 8, ' ');
    for (i = 0; i < WORD_LEN; i++)
    {
        LCD1602_ShowChar(2, COL_HIST_M + i, guess_mark[hist_idx][i]);
    }
    LCD1602_ShowString(2, 14, "   ");
}


/* ======================= game logic ====================================== */

/*  Draw a random answer from the word pool.
    Called on the first key press of the game: timer 0 has been running free
    since power up, so its value depends on how long the player took to touch
    the keypad - the only real entropy an 8051 has.                        */
static void Pick_Answer(void)
{
    unsigned int t;

    t  = ((unsigned int)TH0 << 8) | (unsigned int)TL0;
    t ^= (unsigned int)(t << 7);        /* spread the timer value out */
    t ^= (unsigned int)(t >> 9);
    t ^= (unsigned int)(t << 8);

    Dict_RandomAnswer(answer, t);
    answer_ready = 1;
}

/*  Standard Wordle grading.
    Pass 1 marks every letter that is in the right place green ('G').
    Pass 2 walks the remaining positions from left to right and marks a
    letter yellow ('Y') only while the answer still holds an unused copy of
    it - that is what makes repeated letters behave correctly.
    Everything else is red ('R').                                         */
static void Evaluate(unsigned char idata *word, unsigned char idata *mark)
{
    unsigned char i, j, total, seen;

    for (i = 0; i < WORD_LEN; i++)
    {
        mark[i] = (word[i] == answer[i]) ? 'G' : 'R';
    }

    for (i = 0; i < WORD_LEN; i++)
    {
        if (mark[i] == 'G') continue;

        /* copies of this letter still available in the answer */
        total = 0;
        for (j = 0; j < WORD_LEN; j++)
        {
            if (mark[j] != 'G' && answer[j] == word[i]) total++;
        }
        /* copies already spent on yellows in front of position i */
        seen = 0;
        for (j = 0; j < i; j++)
        {
            if (mark[j] == 'Y' && word[j] == word[i]) seen++;
        }

        if (seen < total) mark[i] = 'Y';
    }
}

/* the player pressed key 16 with a full word in the boxes */
static void Submit_Guess(void)
{
    unsigned char i;

    /* a guess only counts when it is a word the dictionary knows */
    if (Dict_Has(cur_word) == 0)
    {
        state = ST_INVALID;
        Draw_Line2();
        return;
    }

    for (i = 0; i < WORD_LEN; i++)
    {
        guess_word[rounds][i] = cur_word[i];
    }
    Evaluate(guess_word[rounds], guess_mark[rounds]);

    won = 1;
    for (i = 0; i < WORD_LEN; i++)
    {
        if (guess_mark[rounds][i] != 'G')
        {
            won = 0;
            break;
        }
    }

    rounds++;
    state = ST_RESULT;
    Draw_Round();
    Draw_Line2();
}

/* the player pressed key 16 while a graded word was on screen */
static void Next_Round(void)
{
    if (won)
    {
        state = ST_WIN;
        Draw_Line2();
        return;
    }
    if (rounds >= MAX_ROUND)
    {
        state = ST_LOSE;
        Draw_Line2();
        return;
    }

    input_len = 0;
    stage_ch  = 0;
    state     = ST_INPUT;
    Draw_Round();
    Draw_Line2();
}


/* ======================= key handling ==================================== */

static void Key_Play(unsigned char k)
{
    /* keys 1..13 are the letter keys of the current page */
    if (k >= 1 && k <= 13)
    {
        /* a full input box cannot take a new letter, not even into the
           staging box, but backspace and enter still work */
        if (state == ST_INPUT && input_len < WORD_LEN)
        {
            stage_ch = (unsigned char)('A' + page * 13 + (k - 1));
            LCD1602_ShowChar(2, COL_STAGE, stage_ch);
        }
        return;
    }

    if (k == KEY_PAGE)
    {
        page = (unsigned char)(!page);
        Draw_Page();
        return;
    }

    if (k == KEY_BACK)
    {
        if (state != ST_INPUT) return;

        if (stage_ch != 0)
        {
            stage_ch = 0;                               /* drop staging letter   */
            LCD1602_ShowChar(2, COL_STAGE, BOX_EMPTY);
        }
        else if (input_len > 0)
        {
            input_len--;                                /* else rub out last box */
            LCD1602_ShowChar(2, COL_INPUT + input_len, BOX_EMPTY);
        }
        return;
    }

    if (k == KEY_OK)
    {
        if (state == ST_RESULT)
        {
            Next_Round();
            return;
        }
        if (state == ST_INVALID)
        {
            /* the word was rejected: clear the boxes and let the player type
               this attempt again - it does not use up one of the six tries */
            input_len = 0;
            stage_ch  = 0;
            state     = ST_INPUT;
            Draw_Line2();
            return;
        }
        if (state != ST_INPUT) return;

        if (stage_ch != 0)
        {
            cur_word[input_len] = stage_ch;             /* staging -> input box */
            LCD1602_ShowChar(2, COL_INPUT + input_len, stage_ch);
            input_len++;
            stage_ch = 0;
            LCD1602_ShowChar(2, COL_STAGE, BOX_EMPTY);
        }
        else if (input_len == WORD_LEN)
        {
            Submit_Guess();                             /* full word: grade it  */
        }
        return;
    }
}

static void Key_History(unsigned char k)
{
    if (k == KEY_PREV)
    {
        if (hist_idx > 0)
        {
            hist_idx--;
            Draw_History();
        }
    }
    else if (k == KEY_NEXT)
    {
        if (hist_idx + 1 < rounds)
        {
            hist_idx++;
            Draw_History();
        }
    }
}

/* P3.0 switches between the play screen and the history screen */
static void Key_Mode(void)
{
    if (view == VIEW_PLAY)
    {
        view     = VIEW_HIST;
        hist_idx = 0;                   /* always start at the first entry */
        Draw_History();
    }
    else
    {
        view = VIEW_PLAY;
        Draw_PlayTop();
        Draw_Line2();
    }
}


/* ======================= main ============================================ */

void main(void)
{
    unsigned char k;

    Key_Init();

    /* free running timer 0, used only as a source of randomness */
    TMOD = (TMOD & 0xF0) | 0x01;
    TH0  = 0;
    TL0  = 0;
    TR0  = 1;

    input_len    = 0;
    stage_ch     = 0;
    page         = 0;
    view         = VIEW_PLAY;
    hist_idx     = 0;
    rounds       = 0;
    state        = ST_INPUT;
    won          = 0;
    answer_ready = 0;

    Delay_ms(100);                  /* let the LCD finish its power on reset */
    LCD1602_Init();

    Draw_PlayTop();
    Draw_Line2();

    while (1)
    {
        k = Key_Scan();
        if (k == KEY_NONE) continue;

        if (answer_ready == 0)
        {
            Pick_Answer();          /* seeded by the first key press */
        }

        if (k == KEY_MODE)
        {
            Key_Mode();
            continue;
        }

        if (view == VIEW_HIST)
        {
            Key_History(k);
            continue;
        }

        Key_Play(k);
    }
}
