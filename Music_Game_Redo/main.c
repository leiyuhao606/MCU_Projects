/*
 * Music Game Redo - STC89C52RC + LCD1602
 *
 * The LCD1602 gives us four 4-pixel lanes: the upper and lower half of each
 * character row.  Notes are 3 pixels wide by 4 pixels high and travel from
 * the left edge to the centre pixel of character column 12 (x = 57).  The
 * travel time is deliberately close to two seconds so a frame never skips a
 * visible pixel.  A note
 * is Perfect when its three columns include x = 57; it is Great only when a
 * note column is immediately beside that line (x = 54 or x = 58).
 *
 * Lane/key wiring follows the board schematic's physical right-to-left
 * requirement:
 *   LCD top       -> K4 / P3.3
 *   LCD upper     -> K3 / P3.2
 *   LCD lower     -> K2 / P3.0
 *   LCD bottom    -> K1 / P3.1
 *
 * Music and the two song tables are carried over from the standalone player;
 * charts.h selects real non-rest note starts from those same tables so their
 * Perfect moments stay aligned with audible notes.
 */

#include <REGX52.H>
#include "lcd1602.h"
#include "keypad.h"
#include "delay.h"
#include "music.h"
#include "charts.h"

#define UI_SELECT       0
#define UI_GAME         1
#define UI_RESULT       2

#define LCD_CELLS       16
#define LCD_PIXEL_W     80
#define JUDGE_X         57
#define NOTE_WIDTH      3
#define TRAVEL_TICKS       480U   /* 480 * 4 ms = 1.92 s */
#define MISS_AFTER_TICKS    90U   /* 360 ms */
#define FRAME_TICKS           4U  /* 16 ms */
#define INPUT_STABLE_TICKS    4U  /* 16 ms; also rejects most IR bursts */
#define HIT_FLASH_TICKS      24U  /* 96 ms; six visible 16-ms frames */

/* CGRAM slot 1 and 2 are the two independent judgement-line cells.  Slots
 * 0 and 3..7 are allocated to cells containing moving note pixels for this
 * frame.  0xFF in cell_code means that a cell has not been allocated yet. */
#define LINE_SLOT_TOP   1
#define LINE_SLOT_BOTTOM 2
#define NOTE_SLOT_FIRST 3
#define NOTE_SLOT_LAST  7

static unsigned char ui;
static unsigned char sel;

static ChartNote code *game_chart;
static unsigned int chart_count;
static unsigned char idata note_done[(CHART_MAX_COUNT + 7) / 8];
static unsigned int perfect_hits;
static unsigned int great_hits;
static unsigned long final_score;

/* Pixel renderer buffers.  8 * 8 bytes are the seven dynamic CGRAM glyphs
 * plus an unused slot zero; cell_code stores a CGRAM code or zero for blank. */
/* The STC89C52RC has 256 bytes of indirect internal RAM.  Keep the dynamic
 * 64-byte glyph table there so the ordinary DATA area still leaves room for
 * the call stack and C51's temporary values. */
static unsigned char idata glyph[8][8];
static unsigned char idata cell_code[32];
static unsigned int idata lcd_glyph_hash[8];
/* Two 4-bit screen tags per byte: 0=space, 1..8=CGRAM 0..7,
 * 9='P', 10='G'.  The LCD shadow therefore costs 16 bytes instead of 32. */
static unsigned char idata lcd_cell[16];
static unsigned char next_glyph;

/* Input stability timestamp; unlike Key_Scan this keeps all four independent
 * keys available at the same time for legal two-note chords. */
static unsigned char input_raw;
static unsigned char input_stable;
static unsigned int input_age;
static unsigned int input_last_tick;
static unsigned int last_frame_tick;
static unsigned char hit_flash[4];
static unsigned char hit_quality[4];
static unsigned char feedback_timer;
static unsigned char feedback_char;

static unsigned char code RATING_D[]   = "D";
static unsigned char code RATING_C[]   = "C";
static unsigned char code RATING_B[]   = "B";
static unsigned char code RATING_A[]   = "A";
static unsigned char code RATING_S[]   = "S";
static unsigned char code RATING_V[]   = "V";
static unsigned char code RATING_PHI[] = "Phi";


/* ---- small display helpers --------------------------------------------- */

static void LCD_ClearLine(unsigned char line)
{
    LCD1602_ShowString(line, 1, "                ");
}

static void LCD_ShowNumber(unsigned char line, unsigned char column,
                           unsigned long value)
{
    unsigned char digits[10];
    unsigned char count = 0;

    do
    {
        digits[count++] = (unsigned char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && count < 10);

    while (count != 0)
    {
        LCD1602_ShowChar(line, column++, digits[--count]);
    }
}

static unsigned char code *Rating_Name(unsigned long score)
{
    if (score >= 1000000UL) return RATING_PHI;
    if (score >= 920000UL)  return RATING_V;
    if (score >= 880000UL)  return RATING_S;
    if (score >= 800000UL)  return RATING_A;
    if (score >= 700000UL)  return RATING_B;
    if (score >= 600000UL)  return RATING_C;
    return RATING_D;
}

static void Draw_Select(void)
{
    LCD_ClearLine(1);
    LCD1602_ShowChar(1, 1, (unsigned char)('1' + sel));
    LCD1602_ShowChar(1, 2, ' ');
    LCD1602_ShowString(1, 3, Music_Name(sel));

    LCD_ClearLine(2);
    LCD1602_ShowChar(2, 1, (unsigned char)('1' + sel));
    LCD1602_ShowChar(2, 2, '/');
    LCD1602_ShowChar(2, 3, '2');
    LCD1602_ShowString(2, 5, "K1/K3 K2:GO");
}

static void Draw_Result(void)
{
    LCD_ClearLine(1);
    LCD1602_ShowString(1, 1, "SCORE ");
    LCD_ShowNumber(1, 7, final_score);

    LCD_ClearLine(2);
    LCD1602_ShowString(2, 1, Music_Name(sel));
    LCD1602_ShowChar(2, 10, ' ');
    LCD1602_ShowString(2, 11, Rating_Name(final_score));
}


/* ---- chart status and score -------------------------------------------- */

static unsigned char Note_Is_Done(unsigned int index)
{
    return (unsigned char)((note_done[index >> 3] >> (index & 7)) & 0x01);
}

static void Note_Mark_Done(unsigned int index)
{
    note_done[index >> 3] |= (unsigned char)(1 << (index & 7));
}

static unsigned int Note_TargetTick(unsigned int index)
{
    unsigned int units = game_chart[index].at;
    unsigned char unit = Music_UnitMs();

    /* Divide the unit duration before multiplying.  This keeps the complete
     * song clock inside 16 bits while retaining a 4 ms timing resolution. */
    return (unsigned int)(units * (unsigned int)(unit / 4)
           + (units * (unsigned int)(unit % 4)) / 4);
}

/* The denominator stays below 100 * 109, so splitting 1,000,000 into a
 * quotient and remainder avoids a 32-bit overflow while preserving exact
 * AllPerfect = 1,000,000 and ordinary integer rounding for every other run. */
static unsigned long Calculate_Score(void)
{
    unsigned long weighted;
    unsigned long denominator;
    unsigned long whole;
    unsigned long remainder;

    weighted = (unsigned long)perfect_hits * 100UL;
    weighted += (unsigned long)great_hits * 60UL;
    denominator = (unsigned long)chart_count * 100UL;

    whole = 1000000UL / denominator;
    remainder = 1000000UL % denominator;
    return weighted * whole
         + (weighted * remainder + denominator / 2UL) / denominator;
}


/* ---- pixel renderer ----------------------------------------------------- */

static unsigned char Dot_Mask(unsigned int x)
{
    /* CGRAM bit 4 is the leftmost dot of the 5-dot character cell. */
    return (unsigned char)(0x10 >> (x % 5));
}

static unsigned char Graphics_Slot(unsigned char cell)
{
    if (cell == 11) return LINE_SLOT_TOP;
    if (cell == 27) return LINE_SLOT_BOTTOM;

    if (cell_code[cell] != 0xFF) return cell_code[cell];

    if (next_glyph <= NOTE_SLOT_LAST)
    {
        cell_code[cell] = next_glyph;
        return next_glyph++;
    }
    if (next_glyph == (unsigned char)(NOTE_SLOT_LAST + 1))
    {
        next_glyph = (unsigned char)(NOTE_SLOT_LAST + 2);
        cell_code[cell] = 0;          /* CGRAM slot zero */
        return 0;
    }

    return 0xFF;                       /* chart spacing should avoid this */
}

static void Graphics_Reset(void)
{
    unsigned char slot;
    unsigned char row;
    unsigned char cell;
    unsigned char i;

    for (slot = 0; slot < 8; slot++)
    {
        for (row = 0; row < 8; row++) glyph[slot][row] = 0;
    }
    for (cell = 0; cell < 32; cell++) cell_code[cell] = 0xFF;

    /* One-pixel judgement line at the centre of column 12 on both LCD rows. */
    cell_code[11] = LINE_SLOT_TOP;
    cell_code[27] = LINE_SLOT_BOTTOM;
    for (row = 0; row < 8; row++)
    {
        glyph[LINE_SLOT_TOP][row] = 0x10;
        glyph[LINE_SLOT_BOTTOM][row] = 0x10;
    }

    /* A hit flashes the complete 5x4 area at the judgement line.  Perfect is
     * a solid bar; Great is a broken bar so the two results are distinguishable
     * even without adding text to the four-lane playfield. */
    for (row = 0; row < 4; row++)
    {
        if (hit_flash[row] != 0)
        {
            slot = (unsigned char)((row < 2) ? LINE_SLOT_TOP : LINE_SLOT_BOTTOM);
            cell = (unsigned char)(((row == 0) || (row == 2)) ? 0 : 4);
            for (i = 0; i < 4; i++)
            {
                glyph[slot][cell + i] =
                    (unsigned char)((hit_quality[row] == 2) ? 0x1F : 0x15);
            }
        }
    }
    /* Columns 1..14 are reserved for the playfield.  The rightmost cell of
     * the lower LCD row is therefore a stable, readable hit indicator. */
    if (feedback_timer != 0) cell_code[31] = feedback_char;
    next_glyph = NOTE_SLOT_FIRST;
}

static void Graphics_Invalidate(void)
{
    unsigned char slot;
    unsigned char cell;

    for (slot = 0; slot < 8; slot++) lcd_glyph_hash[slot] = 0xFFFF;
    for (cell = 0; cell < 16; cell++) lcd_cell[cell] = 0xFF;
}

static unsigned char Cell_Tag(unsigned char value)
{
    if (value == ' ') return 0;
    if (value == 'P') return 9;
    if (value == 'G') return 10;
    return (unsigned char)(value + 1);
}

static unsigned int Note_X(unsigned int now, unsigned int target)
{
    unsigned int elapsed = (unsigned int)(now + TRAVEL_TICKS - target);

    return (unsigned int)((elapsed * JUDGE_X) / TRAVEL_TICKS);
}

static void Graphics_Add_Note(unsigned char row, unsigned int x)
{
    unsigned char lane_y;
    unsigned char line;
    unsigned char cell;
    unsigned char slot;
    unsigned char pixel;
    unsigned char i;

    line = (unsigned char)((row < 2) ? 0 : 1);
    lane_y = (unsigned char)(((row == 0) || (row == 2)) ? 0 : 4);

    for (i = 0; i < NOTE_WIDTH; i++)
    {
        pixel = (unsigned char)(x + i);
        if (pixel >= LCD_PIXEL_W) continue;

        cell = (unsigned char)(line * LCD_CELLS + pixel / 5);
        slot = Graphics_Slot(cell);
        if (slot == 0xFF) continue;    /* chart spacing keeps this rare */
        glyph[slot][lane_y + 0] |= Dot_Mask(pixel);
        glyph[slot][lane_y + 1] |= Dot_Mask(pixel);
        glyph[slot][lane_y + 2] |= Dot_Mask(pixel);
        glyph[slot][lane_y + 3] |= Dot_Mask(pixel);
    }
}

static void Game_Render(unsigned int now)
{
    unsigned int i;
    unsigned int target;
    unsigned int x;
    unsigned char slot;
    unsigned char row;
    unsigned int hash;
    unsigned char value;
    unsigned char tag;
    unsigned char old_tag;
    unsigned char packed;
    unsigned char need_display;

    Graphics_Reset();
    for (i = 0; i < chart_count; i++)
    {
        if (Note_Is_Done(i)) continue;
        target = Note_TargetTick(i);
        if (now + TRAVEL_TICKS < target) continue;
        if (now > target + MISS_AFTER_TICKS) continue;

        x = Note_X(now, target);
        Graphics_Add_Note(game_chart[i].row, x);
    }

    if (next_glyph > 8) next_glyph = 8;

    /* If the integer-pixel image did not change, leave the LCD and Timer 0
     * completely alone.  The 16-ms scheduler can therefore run without
     * introducing a periodic audible mute. */
    need_display = 0;
    for (slot = 0; slot < next_glyph; slot++)
    {
        hash = 0;
        for (row = 0; row < 8; row++)
        {
            hash = (unsigned int)((hash << 1) ^ glyph[slot][row] ^ 0x009D);
        }
        if (hash != lcd_glyph_hash[slot]) need_display = 1;
    }
    for (i = 0; i < 32; i++)
    {
        value = (cell_code[i] != 0xFF) ? cell_code[i] : ' ';
        tag = Cell_Tag(value);
        packed = lcd_cell[i >> 1];
        old_tag = (unsigned char)((i & 1) ? (packed >> 4) : (packed & 0x0F));
        if (old_tag != tag) need_display = 1;
    }
    if (!need_display) return;

    Music_DisplayBegin();
    for (slot = 0; slot < next_glyph; slot++)
    {
        hash = 0;
        for (row = 0; row < 8; row++)
        {
            hash = (unsigned int)((hash << 1) ^ glyph[slot][row] ^ 0x009D);
        }
        if (hash != lcd_glyph_hash[slot])
        {
            LCD1602_LoadCharRam(slot, glyph[slot]);
            lcd_glyph_hash[slot] = hash;
        }
    }

    for (i = 0; i < 32; i++)
    {
        value = (cell_code[i] != 0xFF) ? cell_code[i] : ' ';
        tag = Cell_Tag(value);
        packed = lcd_cell[i >> 1];
        old_tag = (unsigned char)((i & 1) ? (packed >> 4) : (packed & 0x0F));
        if (old_tag == tag) continue;
        LCD1602_SetCursor((unsigned char)((i < 16) ? 1 : 2),
                          (unsigned char)((i & 0x0F) + 1));
        LCD1602_WriteData(value);
        if (i & 1) lcd_cell[i >> 1] = (unsigned char)((packed & 0x0F) | (tag << 4));
        else       lcd_cell[i >> 1] = (unsigned char)((packed & 0xF0) | tag);
    }
    Music_DisplayEnd();
}


/* ---- input and judgement ----------------------------------------------- */

static void Try_Hit(unsigned char row, unsigned int now)
{
    unsigned int i;
    unsigned int best = CHART_MAX_COUNT;
    unsigned int best_diff = 0xFFFF;
    unsigned int x;
    unsigned int diff;
    unsigned char kind = 0;
    unsigned char best_kind = 0;
    unsigned int target;

    for (i = 0; i < chart_count; i++)
    {
        if (Note_Is_Done(i) || game_chart[i].row != row) continue;

        target = Note_TargetTick(i);
        if (now + TRAVEL_TICKS < target) continue;
        if (now > target + MISS_AFTER_TICKS)
        {
            Note_Mark_Done(i);
            continue;
        }

        x = Note_X(now, target);
        if (x >= 55 && x <= 57)
        {
            diff = (unsigned int)(x > 56 ? x - 56 : 56 - x);
            kind = 2;                    /* Perfect */
        }
        else if (x == 54 || x == 58)
        {
            diff = 2;
            kind = 1;                    /* Great */
        }
        else
        {
            continue;
        }

        if (diff < best_diff)
        {
            best_diff = diff;
            best = i;
            best_kind = kind;
        }
    }

    if (best >= chart_count) return;

    Note_Mark_Done(best);
    if (best_kind == 2) perfect_hits++;
    else           great_hits++;
    hit_flash[row] = HIT_FLASH_TICKS;
    hit_quality[row] = best_kind;
    feedback_timer = HIT_FLASH_TICKS;
    feedback_char = (unsigned char)((best_kind == 2) ? 'P' : 'G');
    last_frame_tick = (unsigned int)(now - FRAME_TICKS);
}

static void Handle_Pressed(unsigned char pressed, unsigned int now)
{
    /* Top-to-bottom lanes are K4, K3, K2, K1, i.e. right-to-left physically. */
    if (pressed & KEY_MASK_STOP) Try_Hit(0, now);
    if (pressed & KEY_MASK_NEXT) Try_Hit(1, now);
    if (pressed & KEY_MASK_MODE) Try_Hit(2, now);
    if (pressed & KEY_MASK_PREV) Try_Hit(3, now);
}

static void Game_Update_Input(unsigned int now)
{
    unsigned char row;
    unsigned char raw;
    unsigned char pressed;

    if (now == input_last_tick) return;
    input_last_tick = now;

    for (row = 0; row < 4; row++)
    {
        if (hit_flash[row] != 0) hit_flash[row]--;
    }
    if (feedback_timer != 0) feedback_timer--;

    raw = Key_ReadMask();
    if (raw != input_raw)
    {
        input_raw = raw;
        /* Store the actual edge time.  Counting foreground-loop passes is
         * unreliable because a full LCD frame can take several milliseconds. */
        input_age = now;
    }

    if ((unsigned int)(now - input_age) >= INPUT_STABLE_TICKS
        && raw != input_stable)
    {
        pressed = (unsigned char)(raw & (unsigned char)~input_stable);
        input_stable = raw;
        /* Judge from the edge time, not after the debounce delay. */
        if (pressed != 0)
        {
            Handle_Pressed(pressed, input_age);
            /* If the LCD frame delayed the confirmation, also try the
             * confirmation instant.  Note_Mark_Done makes this idempotent,
             * while covering the narrow pixel-wide Great window. */
            if (input_age != now) Handle_Pressed(pressed, now);
        }
    }
}

static void Game_Update_Misses(unsigned int now)
{
    unsigned int i;

    for (i = 0; i < chart_count; i++)
    {
        if (!Note_Is_Done(i) && now > Note_TargetTick(i) + MISS_AFTER_TICKS)
        {
            Note_Mark_Done(i);
        }
    }
}


/* ---- game state transitions -------------------------------------------- */

static void Game_Begin(void)
{
    unsigned int i;
    unsigned char raw;

    game_chart  = (sel == 0) ? CHART0 : CHART1;
    chart_count = (sel == 0) ? CHART0_COUNT : CHART1_COUNT;

    for (i = 0; i < (CHART_MAX_COUNT + 7) / 8; i++) note_done[i] = 0;
    perfect_hits = 0;
    great_hits   = 0;
    final_score  = 0;
    for (i = 0; i < 4; i++)
    {
        hit_flash[i] = 0;
        hit_quality[i] = 0;
    }
    feedback_timer = 0;
    feedback_char = ' ';

    Music_Load(sel);
    ui = UI_GAME;

    /* Draw the empty field and judgement line before the first tone starts. */
    Graphics_Invalidate();
    Graphics_Reset();
    Game_Render(0);

    raw = Key_ReadMask();
    input_raw = raw;
    input_stable = raw;                 /* do not reuse the selection press */
    input_age = 0;
    input_last_tick = 0;
    last_frame_tick = 0;

    Music_Start();
}

static void Game_Finish(void)
{
    unsigned int i;

    for (i = 0; i < chart_count; i++)
    {
        if (!Note_Is_Done(i)) Note_Mark_Done(i);
    }
    final_score = Calculate_Score();
    Music_Halt();
    ui = UI_RESULT;
    Draw_Result();
}

static void Handle_Select_Key(unsigned char key)
{
    if (key == KEY_PREV)
    {
        if (sel > 0)
        {
            sel--;
            Music_Load(sel);
            Draw_Select();
        }
    }
    else if (key == KEY_NEXT)
    {
        if (sel + 1 < SONG_COUNT)
        {
            sel++;
            Music_Load(sel);
            Draw_Select();
        }
    }
    else if (key == KEY_MODE)
    {
        Game_Begin();
    }
}

static void Handle_Result_Key(unsigned char key)
{
    if (key == KEY_MODE || key == KEY_STOP)
    {
        ui = UI_SELECT;
        Draw_Select();
    }
}


/* ---- main --------------------------------------------------------------- */

void main(void)
{
    unsigned char key;
    unsigned int now;

    Delay_ms(100);
    LCD1602_Init();
    Key_Init();
    Music_Init();

    ui  = UI_SELECT;
    sel = 0;
    Music_Load(sel);
    Draw_Select();

    while (1)
    {
        if (ui == UI_SELECT)
        {
            key = Key_Scan();
            if (key != KEY_NONE) Handle_Select_Key(key);
            continue;
        }

        if (ui == UI_RESULT)
        {
            key = Key_Scan();
            if (key != KEY_NONE) Handle_Result_Key(key);
            continue;
        }

        /* Game input is serviced without Key_Scan so two keys can be pressed
         * together and the music timer remains the sole timing reference. */
        /* Timer 1 only advances the clock.  Run the note state machine in the
         * foreground so it never shares the interrupt stack with LCD/input
         * code; Music_Run uses absolute deadlines to preserve the tempo. */
        Music_Run();
        now = Music_ClockTick();
        Game_Update_Input(now);
        Game_Update_Misses(now);

        if (Music_State() == MUS_STOP)
        {
            Game_Finish();
            continue;
        }

        if (now - last_frame_tick >= FRAME_TICKS)
        {
            Game_Render(now);
            last_frame_tick = now;
        }
    }
}
