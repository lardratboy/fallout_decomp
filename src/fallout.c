/*
 * FallOut -- a falling-symbols puzzle game for VGA (mode 13h).
 * (c) 1991 Brad P. Taylor.  Special thanks to Peter Crayne and John Fogh.
 *
 * Source reconstructed from FALLOUT.EXE (Oct 10 1991, LZEXE 0.91 packed,
 * Turbo C++ 1.0, small memory model).
 *
 * Layout notes
 * ------------
 * Functions appear in the same order as in the executable; the hex number in
 * each function's comment is its offset in the unpacked code segment.  Global
 * data is defined in an order that reproduces the original data segment:
 * Turbo C emits initialised globals where they are defined and each function's
 * string literals right after that function, so data definitions are placed
 * between the functions where they originally sat.  DS:xxxx comments give the
 * original offsets.  Uninitialised globals (_BSS) are declared in address
 * order.  A few variables are never referenced; they are kept (as unused_xxxx)
 * so the layout stays identical.
 *
 * The low level graphics/board routines were written in assembly; see gfx.c.
 *
 * Playfield: char board[17][9]; 0 = empty, 1..num_symbols = symbol.  The
 * falling piece is three symbols piece[0..2].  orient == 1 means vertical
 * (piece[2] at (row,col), piece[1] above it, piece[0] above that);
 * orient == 10 means horizontal (piece[0..2] at columns col-1..col+1).
 * A cell is drawn at x = 111 + col*11, y = 7 + row*11.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <dos.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "gfx.h"

/* ------------------------------------------------------------------ types */

struct hiscore {                    /* 14 bytes; table is sorted ascending   */
    char name[8];
    unsigned long score;
    int  slot;                      /* set to the index at start-up, unused  */
};

struct star {                       /* title screen star field               */
    int x, y;
    int speed;
    int color;
    int drawn;
};

struct text_line {                  /* "About" text: x, y, string            */
    int x, y;
    char *text;
};

typedef void (*transition_fn)(unsigned src, unsigned dst);

/* ------------------------------------------------------------- prototypes */

void trans_box_close(unsigned src, unsigned dst);
void trans_box_open(unsigned src, unsigned dst);
void trans_checker_box_close(unsigned src, unsigned dst);
void trans_checker_box_open(unsigned src, unsigned dst);
void trans_checker_bands(unsigned src, unsigned dst);
void trans_bands(unsigned src, unsigned dst);
void set_game_palette(void);
void trans_dissolve(unsigned src, unsigned dst);
int  find_matches(void);
void draw_board(void);
void flash_matches(void);
int  key_waiting(void);
void drop_piece(void);
int  handle_key(int *row, int *col);
void detonate_bomb(int *row, int *col, int redraw);
void rotate_piece(int *row, int *col);
void hard_drop(int *row, int *col);
int  move_left(int *row, int *col);
int  move_right(int *row, int *col);
void place_piece(int row, int col, int moving);
void erase_piece(int row, int col);
void draw_piece(int row, int col);
void new_piece(void);
void init_game(int argc, char *argv[]);
void draw_playfield(void);
int  new_game(void);
void reset_game(void);
void add_score(void);
void level_up(void);
void shutdown(int save);
void draw_background(unsigned seg);
void draw_cheat_mode(unsigned seg);
void show_bombs(unsigned seg);
void show_score(unsigned seg);
void show_level(unsigned seg);
void show_total(unsigned seg);
void show_bonus(unsigned seg);
void load_high_scores(void);
void save_high_scores(void);
void scramble(unsigned char *p, unsigned n);
void check_high_score(void);
void make_bg_tile(int shade);
int  room_for_piece(void);
void game_over_anim(int row, int col);
void tally_bonus(void);
void set_default_colors(void);
void cycle_colors(void);
void make_brick(int light, int face, int dark);
void title_screen(int timeout);
void title_init(void);
void draw_brick_band(int top, int bottom, int color);
void wave_scroll(unsigned seg);
void draw_about_text(unsigned seg);
void draw_tile_strip(unsigned seg);
void scroll_tile_strip(unsigned seg);
void init_stars(void);
void move_stars(unsigned seg);
void main_menu(void);
void draw_main_menu(void);
void difficulty_menu(void);
void draw_score_panel(unsigned seg);
void draw_high_scores(unsigned seg);
void draw_panel(unsigned seg, int x, int y, int w, int h, int unused);
void pause_game(void);
void bounce_tiles(int speed, int count);
void draw_bounce_tile(unsigned seg, int *x, int *y, int *dx, int *dy);
void move_bounce_tile(unsigned seg, int *x, int *y, int *dx, int *dy, int margin);
void make_bounce_tile(void);
void enter_initials(char *name);
void snd_click(void);
void snd_sweep(void);
void snd_short_sweep(void);
void init_font8(void);
void set_text_bg(unsigned char c);
void set_text_color(char c);
void draw_text(unsigned seg, char *s, int x, int y);
void draw_text_bg(unsigned seg, char *s, int x, int y);
void draw_text_centered(unsigned seg, char *s, int y);
void draw_text_centered_shadow(unsigned seg, char *s, int y);
void draw_char(unsigned seg, char c, int x, int y);
void draw_char_bg(unsigned seg, char c, int x, int y);
void set_small_color(char c);
void set_small_color2(char c);
void init_font6(void);
void draw_small_text(unsigned seg, char *s, int x, int y);
void draw_small_char(unsigned seg, char c, int x, int y);

/* data that is defined further down (its position in DGROUP is fixed) */
extern char wave[2000];
extern unsigned char font8_img[39][64];
extern unsigned char *font8[38];
extern unsigned char font6_img[42][36];
extern unsigned char *font6[41];
extern unsigned char *bomb1[27];
extern unsigned char *bomb2[25];

/* ======================================================= initialised data */

#include "data/logo.inc"            /* DS:00A8 unsigned char logo[1216]       */
#include "data/palette.inc"         /* DS:0568 unsigned char game_palette[768]*/
#include "data/tiles.inc"           /* DS:0868 tiles, tile_sets, explode       */

char unused_3b53 = 0;               /* DS:3B53 */

transition_fn transitions[] = {     /* DS:3B54  cycled by new_game()          */
    trans_box_open,
    trans_checker_bands,
    trans_box_close,
    trans_bands,
    trans_checker_box_open,
    trans_box_close,
    trans_checker_box_close,
    0
};

int unused_3b64 = 1;                /* DS:3B64 */

unsigned char col_face   = 1;       /* DS:3B66  panel colours (set at run time)*/
unsigned char col_light  = 9;       /* DS:3B67 */
unsigned char col_dark   = 0;       /* DS:3B68 */
unsigned char col_shadow = 14;      /* DS:3B69 */
unsigned char col_text   = 9;       /* DS:3B6A */
unsigned long total      = 0;       /* DS:3B6B  symbols cleared this game     */
unsigned long score      = 0;       /* DS:3B6F */
unsigned bonus           = 0;       /* DS:3B73 */
unsigned next_level      = 100;     /* DS:3B75  symbols needed for next level */
unsigned num_symbols     = 7;       /* DS:3B77  difficulty: 5..13 symbols     */
int      orient          = 10;      /* DS:3B79  1 = vertical, 10 = horizontal */
unsigned multiplier      = 5;       /* DS:3B7B */
int      unused_3b7d     = 1;       /* DS:3B7D */
unsigned cleared         = 0;       /* DS:3B7F  symbols cleared this level    */
int      sound_on        = 0;       /* DS:3B81 */
unsigned speed           = 2;       /* DS:3B83  'V' / 'S' keys; never read    */
unsigned level           = 0;       /* DS:3B85 */
unsigned tile_set        = 1;       /* DS:3B87  0..9, '+' key                 */
int      bombs           = 1;       /* DS:3B89 */
char     cheat           = 0;       /* DS:3B8B  'B' in the difficulty menu    */
int      bomb_frame      = 0;       /* DS:3B8C */
int      bomb_kind       = 0;       /* DS:3B8E  0 = extra bomb, 1 = detonates */
int      quit_game       = 0;       /* DS:3B90  'A' aborts the current game   */
int      unused_3b92     = 0;       /* DS:3B92  written, never read           */
int      unused_3b94     = 0;       /* DS:3B94  written, never read           */
int      unused_3b96     = 0;       /* DS:3B96  written, never read           */
int      trans_index     = 0;       /* DS:3B98 */
int      color_step      = 2;       /* DS:3B9A */
int      color_base      = 0x7A;    /* DS:3B9C */

/* ============================================= uninitialised data (_BSS) */

int  piece_row;                     /* DS:741A  where the piece was drawn     */
int  piece_col;                     /* DS:741C */
unsigned save_seg;                  /* DS:741E  64000-byte screen buffer      */
int  bomb_row;                      /* DS:7420 */
int  sym_count[256];                /* DS:7422  only [0..9] are used          */
unsigned char bg_tile[20][20];      /* DS:7622 */
int  bomb_col;                      /* DS:77B2 */
int  bomb_active;                   /* DS:77B4 */
char collapse_tmp[17];              /* DS:77B6 */
char marks[17][9];                  /* DS:77C7 */
char piece[3];                      /* DS:7860 */
char board[17][9];                  /* DS:7863 */
char shell_cmd[100];                /* DS:78FC  ESC "boss key" command        */
unsigned back_seg;                  /* DS:7960  64000-byte screen buffer      */
transition_fn transition;           /* DS:7962 */
int  bomb_x;                        /* DS:7964 */
int  bomb_y;                        /* DS:7966 */
struct hiscore hiscore[10];         /* DS:7968 */
int  strip_pos;                     /* DS:79F4 */
struct star stars[200];             /* DS:79F6 */
int  strip_scroll;                  /* DS:81C6 */
unsigned char unused_81c8[400];     /* DS:81C8 */
unsigned char brick[6][10];         /* DS:8358 */
unsigned char unused_8394[4];       /* DS:8394 */
unsigned char bounce_img[20][20];   /* DS:8398 */
unsigned char *font8_map[256];      /* DS:8528 */
unsigned char small_color;          /* DS:8728 */
unsigned char small_color2;         /* DS:8729 */
unsigned char *font6_map[256];      /* DS:872A */

/* ======================================================= screen wipes */

/* 0239: close a rectangle in from the screen edges, two rings per tick. */
void trans_box_close(unsigned src, unsigned dst)
{
    int i;
    unsigned long t;

    for (i = 20; i > 0; i--) {
        t = get_ticks();
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (i + 19) * 5, (20 - i) * 8, (i + 19) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, 8, i * 10);
        copy_rect(dst, src, (i + 19) * 8, (20 - i) * 5, (i + 19) * 8, (20 - i) * 5, 8, i * 10);
        i--;
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (i + 19) * 5, (20 - i) * 8, (i + 19) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, 8, i * 10);
        copy_rect(dst, src, (i + 19) * 8, (20 - i) * 5, (i + 19) * 8, (20 - i) * 5, 8, i * 10);
        while (get_ticks() == t)
            ;
    }
}

/* 049D: open a rectangle out from the centre, two rings per tick. */
void trans_box_open(unsigned src, unsigned dst)
{
    int i;
    unsigned long t;

    for (i = 1; i <= 20; i++) {
        t = get_ticks();
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (i + 19) * 5, (20 - i) * 8, (i + 19) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, 8, i * 10);
        copy_rect(dst, src, (i + 19) * 8, (20 - i) * 5, (i + 19) * 8, (20 - i) * 5, 8, i * 10);
        i++;
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (i + 19) * 5, (20 - i) * 8, (i + 19) * 5, i * 16, 5);
        copy_rect(dst, src, (20 - i) * 8, (20 - i) * 5, (20 - i) * 8, (20 - i) * 5, 8, i * 10);
        copy_rect(dst, src, (i + 19) * 8, (20 - i) * 5, (i + 19) * 8, (20 - i) * 5, 8, i * 10);
        while (get_ticks() == t)
            ;
    }
}

/* 0702: copy half the 20x20 squares in a checkerboard, then close a box. */
void trans_checker_box_close(unsigned src, unsigned dst)
{
    int y, x;

    for (y = 0; y < 10; y += 2)
        for (x = 0; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    for (y = 1; y < 10; y += 2)
        for (x = 1; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    trans_box_close(src, dst);
}

/* 07AC */
void trans_checker_box_open(unsigned src, unsigned dst)
{
    int y, x;

    for (y = 0; y < 10; y += 2)
        for (x = 0; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    for (y = 1; y < 10; y += 2)
        for (x = 1; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    trans_box_open(src, dst);
}

/* 0856 */
void trans_checker_bands(unsigned src, unsigned dst)
{
    int y, x;

    for (y = 0; y < 10; y += 2)
        for (x = 0; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    for (y = 1; y < 10; y += 2)
        for (x = 1; x < 16; x += 2)
            copy_rect(dst, src, x * 20, y * 20, x * 20, y * 20, 20, 20);
    trans_bands(src, dst);
}

/* 0900: fill 80x50 cells with 5-pixel strips, alternate cells top-down and
   bottom-up, one strip per tick. */
void trans_bands(unsigned src, unsigned dst)
{
    int y, i;
    unsigned long t;

    for (i = 0; i < 10; i++) {
        y = i * 5;
        t = get_ticks();
        copy_rect(dst, src,   0, y,       0, y,       80, 5);
        copy_rect(dst, src, 160, y,     160, y,       80, 5);
        copy_rect(dst, src,  80, y + 50,  80, y + 50, 80, 5);
        copy_rect(dst, src, 240, y + 50, 240, y + 50, 80, 5);
        copy_rect(dst, src,   0, y + 100,   0, y + 100, 80, 5);
        copy_rect(dst, src, 160, y + 100, 160, y + 100, 80, 5);
        copy_rect(dst, src,  80, y + 150,  80, y + 150, 80, 5);
        copy_rect(dst, src, 240, y + 150, 240, y + 150, 80, 5);
        y = (9 - i) * 5;
        copy_rect(dst, src,  80, y,      80, y,       80, 5);
        copy_rect(dst, src, 240, y,     240, y,       80, 5);
        copy_rect(dst, src,   0, y + 50,   0, y + 50, 80, 5);
        copy_rect(dst, src, 160, y + 50, 160, y + 50, 80, 5);
        copy_rect(dst, src,  80, y + 100,  80, y + 100, 80, 5);
        copy_rect(dst, src, 240, y + 100, 240, y + 100, 80, 5);
        copy_rect(dst, src,   0, y + 150,   0, y + 150, 80, 5);
        copy_rect(dst, src, 160, y + 150, 160, y + 150, 80, 5);
        while (get_ticks() == t)
            ;
    }
}

/* 0B84 */
void set_game_palette(void)
{
    set_palette(game_palette);
}

/* 0B91 */
void trans_dissolve(unsigned src, unsigned dst)
{
    dissolve7(src, dst);
}

/* ============================================================== the game */

/* 0BA1 */
int main(int argc, char *argv[])
{
    int r, j;
    unsigned char chain, mult;

    init_game(argc, argv);
    for (;;) {
        new_game();
        do {
            chain = 0;
            mult = 2;
            drop_piece();
            while (find_matches()) {
                if (chain > 0) {                /* chain reaction bonus */
                    bonus += mult + 9;
                    show_bonus(SCREEN_SEG);
                }
                flash_matches();
                add_score();
                board_collapse((char *)board, (char *)marks, collapse_tmp);
                show_score(SCREEN_SEG);
                show_total(SCREEN_SEG);
                draw_board();
                if (board_count((char *)board) == 0) {  /* cleared the board */
                    bonus += 1000;
                    show_bonus(SCREEN_SEG);
                    snd_sweep();
                }
                snd_click();
                if (cleared >= next_level) {
                    level_up();
                    snd_sweep();
                }
                chain++;
                mult <<= 1;
            }

            /* about 1 in 200 pieces (1 in 10 in cheat mode) brings a bomb */
            if (cheat)
                r = (long)rand() * 10 / 32767L + 15;
            else
                r = (long)rand() * 200 / 32767L + 21;
            if (r == 22) {
                draw_board();
                /* nine random columns are tried; the last one wins */
                for (r = 0; r < 9; r++) {
                    bomb_col = (long)rand() * 6 / 32767L;
                    for (j = 0; board[j + 1][bomb_col] == 0 && j < 16; j++)
                        ;
                    bomb_row = j;
                    bomb_x = bomb_col * 11 + 111;
                    bomb_y = bomb_row * 11 + 7;
                    if (bomb_row > 5)
                        bomb_active = 1;
                }
                bomb_kind = (long)rand() * 2 / 32767L;
            } else
                bomb_active = 0;
        } while (room_for_piece());

        show_score(SCREEN_SEG);
        show_total(SCREEN_SEG);
        if (quit_game == 0) {
            game_over_anim(2, 4);
            tally_bonus();
            check_high_score();
        }
    }
}

/* 0D6F: mark all runs of three; returns the number of marked cells. */
int find_matches(void)
{
    board_clear((char *)marks);
    board_find_matches((char *)board, (char *)marks);
    return board_count((char *)marks);
}

/* 0D93 */
void draw_board(void)
{
    int col, row;
    int c, y, x;

    for (y = 7, row = 0; row < 17; row++, y += 11)
        for (x = 111, col = 0; col < 9; col++, x += 11) {
            c = board[row][col];
            if (c != 0)
                blit(SCREEN_SEG, tile_sets[tile_set][--c], x, y, 11, 11);
            else
                fill_rect(SCREEN_SEG, x, y, 11, 11, 0);
        }
}

/* 0E29: play the explosion frames over all marked cells for 4 ticks. */
void flash_matches(void)
{
    int col, row;
    int n, f, y, x;
    unsigned long t;

    t = get_ticks();
    for (n = 0; get_ticks() < t + 4; n++) {
        if ((n & 1) == 0) {
            for (f = 4; f >= 0; f--)
                for (y = 7, row = 0; row < 17; row++, y += 11)
                    for (x = 111, col = 0; col < 9; col++, x += 11)
                        if (marks[row][col])
                            blit(SCREEN_SEG, explode[f], x, y, 11, 11);
        } else {
            for (f = 0; f < 5; f++)
                for (y = 7, row = 0; row < 17; row++, y += 11)
                    for (x = 111, col = 0; col < 9; col++, x += 11)
                        if (marks[row][col])
                            blit(SCREEN_SEG, explode[f], x, y, 11, 11);
        }
    }
}

/* 0F41 */
int key_waiting(void)
{
    return kbhit();
}

/* 0F4B: drop one new piece until it lands, then settle it on the board. */
void drop_piece(void)
{
    int event, landed;
    int row, col, wait;
    unsigned long t, anim_t;

    new_piece();
    orient = 1;
    landed = 0;
    row = 2;
    col = 4;
    place_piece(2, 4, 0);
    wait = 11 - level;
    if (wait < 0)
        wait = 0;
    t = get_ticks();
    anim_t = get_ticks();

    do {
        event = 0;
        while (event == 0) {
            if (key_waiting())
                event = 1;
            if (get_ticks() > t + wait)
                event = 2;
            if (bomb_active && get_ticks() != anim_t) {
                switch (bomb_kind) {
                case 0:
                default:
                    blit(SCREEN_SEG, bomb2[bomb_frame++], bomb_x, bomb_y, 11, 11);
                    break;
                case 1:
                    blit(SCREEN_SEG, bomb1[bomb_frame++], bomb_x, bomb_y, 11, 11);
                    break;
                }
                if (bomb_frame > 20)
                    bomb_frame = 0;
                anim_t = get_ticks();
            }
        }

        switch (event) {
        case 1:
            landed = handle_key(&row, &col);
            break;
        case 2:
            if (orient == 1) {
                if (row > 15 || board[row + 1][col] != 0)
                    landed = 1;
                else {
                    row++;
                    place_piece(row, col, 1);
                    t = get_ticks();
                }
            } else {
                if (row > 15 || board[row + 1][col - 1] != 0 ||
                    board[row + 1][col] != 0 || board[row + 1][col + 1] != 0)
                    landed = 1;
                else {
                    row++;
                    place_piece(row, col, 1);
                    t = get_ticks();
                }
            }
            break;
        }
    } while (landed == 0);

    if (orient == 1) {
        board[row][col]     = piece[2];
        board[row - 1][col] = piece[1];
        board[row - 2][col] = piece[0];
    } else {
        board[row][col + 1] = piece[2];
        board[row][col]     = piece[1];
        board[row][col - 1] = piece[0];
    }
    board_collapse((char *)board, (char *)marks, collapse_tmp);
    draw_board();

    if (bomb_active) {
        if (orient == 1) {
            if (col == bomb_col) {
                if (bomb_kind == 0)
                    blit(SCREEN_SEG, explode[0], bomb_x, bomb_y, 11, 11);
                switch (bomb_kind) {
                case 0:
                default:                        /* collect a bomb */
                    bomb_active = 0;
                    bombs++;
                    show_bombs(SCREEN_SEG);
                    snd_sweep();
                    break;
                case 1:                         /* it goes off at once */
                    snd_sweep();
                    score += 500;
                    show_score(SCREEN_SEG);
                    detonate_bomb(&row, &col, 0);
                    break;
                }
                draw_board();
            }
        } else if (col - 1 <= bomb_col && col + 1 >= bomb_col) {
            if (bomb_kind == 0)
                blit(SCREEN_SEG, explode[0], bomb_x, bomb_y, 11, 11);
            switch (bomb_kind) {
            case 0:
            default:
                bomb_active = 0;
                bombs++;
                show_bombs(SCREEN_SEG);
                snd_sweep();
                break;
            case 1:
                snd_sweep();
                score += 500;
                show_score(SCREEN_SEG);
                detonate_bomb(&row, &col, 0);
                break;
            }
            draw_board();
        }
    }
}

/* 12FD: returns non-zero when the piece has been dropped (or the game aborted). */
int handle_key(int *row, int *col)
{
    int t2, t1, t0, ret, key;

    key = getkey();
    ret = 0;
    switch (key) {
    case 0:                                     /* extended key */
        switch (getkey()) {
        case 0x44:                              /* F10: quick quit */
            shutdown(0);
            exit(0);
            break;
        case 0x48:                              /* Up */
            rotate_piece(row, col);
            break;
        case 0x4B:                              /* Left */
            move_left(row, col);
            break;
        case 0x4C:                              /* keypad 5: swap */
            t2 = piece[2];
            t1 = piece[1];
            t0 = piece[0];
            piece[0] = t2;
            piece[1] = t0;
            piece[2] = t1;
            place_piece(*row, *col, 1);
            break;
        case 0x4D:                              /* Right */
            move_right(row, col);
            break;
        case 0x50:                              /* Down: drop */
            hard_drop(row, col);
            ret = 2;
            break;
        }
        break;
    case 'A':
    case 'a':                                   /* abort this game */
        ret = 2;
        quit_game = 1;
        break;
    case '\t':
    case '8':
    case 'I':
    case 'i':
        rotate_piece(row, col);
        break;
    case 27:                                    /* ESC: boss key */
        copy_seg(SCREEN_SEG, save_seg, 32000);
        set_video_mode(3);
        system(shell_cmd);
        set_video_mode(0x13);
        set_game_palette();
        copy_seg(save_seg, SCREEN_SEG, 32000);
        break;
    case 'P':
    case 'p':
        copy_seg(SCREEN_SEG, save_seg, 32000);
        pause_game();
        (*transition)(save_seg, SCREEN_SEG);
        delay(200);
        break;
    case '+':                                   /* next symbol set */
        tile_set++;
        if (tile_set > 9)
            tile_set = 0;
        draw_board();
        place_piece(*row, *col, 1);
        break;
    case 'V':
    case 'v':
        if (speed < 20)
            speed++;
        break;
    case 'S':
    case 's':
        sound_on ^= 1;
        speed = 2;
        break;
    case '\r':
    case ' ':
    case '2':
        hard_drop(row, col);
        ret = 2;
        break;
    case '4':
    case 'J':
    case 'j':
        move_left(row, col);
        break;
    case '6':
    case 'L':
    case 'l':
        move_right(row, col);
        break;
    case '5':
    case 'K':
    case 'k':                                   /* swap */
        t2 = piece[2];
        t1 = piece[1];
        t0 = piece[0];
        piece[0] = t2;
        piece[1] = t0;
        piece[2] = t1;
        place_piece(*row, *col, 1);
        break;
    case 'B':
    case 'b':                                   /* use a bomb */
        if (bombs != 0) {
            if (bomb_active) {
                bomb_active = 0;
                fill_rect(SCREEN_SEG, bomb_x, bomb_y, 11, 11, 0);
            }
            detonate_bomb(row, col, 1);
            bombs--;
            show_bombs(SCREEN_SEG);
        }
        break;
    }
    return ret;
}

/* 15C4: blow up every symbol of the most common kind on the board. */
void detonate_bomb(int *row, int *col, int redraw)
{
    int i, j;
    unsigned char chain, mult, best, c;

    best = 1;
    for (i = 0; i < 10; i++)
        sym_count[i] = 0;
    for (i = 0; i < 17; i++)
        for (j = 0; j < 9; j++) {
            c = board[i][j];
            if (c != 0)
                sym_count[c - 1]++;
        }
    for (i = 0; i < 10; i++)
        if (sym_count[i] > sym_count[best - 1])
            best = i + 1;

    board_clear((char *)marks);
    for (i = 0; i < 17; i++)
        for (j = 0; j < 9; j++)
            if (board[i][j] == best)
                marks[i][j] = 1;

    if (board_count((char *)marks)) {
        snd_short_sweep();
        add_score();
        flash_matches();
        show_score(SCREEN_SEG);
        show_total(SCREEN_SEG);
        if (cleared >= next_level) {
            level_up();
            snd_sweep();
        }
        board_collapse((char *)board, (char *)marks, collapse_tmp);
        draw_board();

        chain = 0;
        mult = 2;
        while (find_matches()) {
            if (chain > 0) {
                bonus += mult + 9;
                show_bonus(SCREEN_SEG);
            }
            flash_matches();
            add_score();
            board_collapse((char *)board, (char *)marks, collapse_tmp);
            show_score(SCREEN_SEG);
            show_total(SCREEN_SEG);
            draw_board();
            if (board_count((char *)board) == 0) {
                bonus += 1000;
                show_bonus(SCREEN_SEG);
                snd_sweep();
            }
            snd_click();
            if (cleared >= next_level) {
                level_up();
                snd_sweep();
            }
            chain++;
            mult <<= 1;
        }
        if (redraw)
            place_piece(*row, *col, 1);
        board_clear((char *)marks);
    }
}

/* 179D: vertical <-> horizontal. */
void rotate_piece(int *row, int *col)
{
    char t;

    if (orient == 1) {
        if (*col > 0 && *col < 8 &&
            board[*row - 1][*col - 1] == 0 &&
            board[*row - 1][*col + 1] == 0 &&
            board[*row][*col - 1] == 0 &&
            board[*row][*col + 1] == 0) {
            erase_piece(*row, *col);
            (*row)--;
            orient = 10;
            place_piece(*row, *col, 1);
        }
    } else if (board[*row + 1][*col] == 0) {
        erase_piece(*row, *col);
        (*row)++;
        orient = 1;
        t = piece[2];
        piece[2] = piece[0];
        piece[0] = t;
        place_piece(*row, *col, 1);
    }
}

/* 1877: drop the piece as far as it will go.  Note that for a vertical piece
   the search starts at the top of the column, not at the piece. */
void hard_drop(int *row, int *col)
{
    int r, hit;

    if (orient == 1) {
        for (r = 0; board[r + 1][*col] == 0 && r < 16; r++)
            ;
        *row = r;
        place_piece(*row, *col, 1);
    } else {
        hit = 0;
        for (r = *row; hit == 0 && r < 17; r++) {
            if (board[r][*col - 1])
                hit = 1;
            if (board[r][*col])
                hit = 1;
            if (board[r][*col + 1])
                hit = 1;
        }
        if (hit)
            *row = r - 2;
        else
            *row = r - 1;
        place_piece(*row, *col, 1);
    }
}

/* 194D */
int move_left(int *row, int *col)
{
    if (orient == 1) {
        if (board[*row][*col - 1] == 0 && *col > 0) {
            (*col)--;
            place_piece(*row, *col, 1);
            return 1;
        }
    } else {
        if (board[*row][*col - 2] == 0 && *col > 1) {
            (*col)--;
            place_piece(*row, *col, 1);
            return 1;
        }
    }
    return 0;
}

/* 19C5 */
int move_right(int *row, int *col)
{
    if (orient == 1) {
        if (board[*row][*col + 1] == 0 && *col < 8) {
            (*col)++;
            place_piece(*row, *col, 1);
            return 1;
        }
    } else {
        if (board[*row][*col + 2] == 0 && *col < 7) {
            (*col)++;
            place_piece(*row, *col, 1);
            return 1;
        }
    }
    return 0;
}

/* 1A3D: draw the piece at (row,col); if moving, erase it at its old place. */
void place_piece(int row, int col, int moving)
{
    if (moving == 0) {
        piece_row = row;
        piece_col = col;
        erase_piece(row, col);
        draw_piece(row, col);
    } else {
        erase_piece(piece_row, piece_col);
        draw_piece(row, col);
        piece_row = row;
        piece_col = col;
    }
}

/* 1A86 */
void erase_piece(int row, int col)
{
    if (orient == 1)
        fill_rect(SCREEN_SEG, col * 11 + 111, (row + (row == 1) - 2) * 11 + 7, 11, 33, 0);
    else
        fill_rect(SCREEN_SEG, (col - 1) * 11 + 111, row * 11 + 7, 33, 11, 0);
}

/* 1B05 */
void draw_piece(int row, int col)
{
    if (orient == 1) {
        blit(SCREEN_SEG, tile_sets[tile_set][piece[2] - 1], col * 11 + 111, row * 11 + 7, 11, 11);
        blit(SCREEN_SEG, tile_sets[tile_set][piece[1] - 1], col * 11 + 111, (row - 1) * 11 + 7, 11, 11);
        blit(SCREEN_SEG, tile_sets[tile_set][piece[0] - 1], col * 11 + 111, (row - 2) * 11 + 7, 11, 11);
    } else {
        blit(SCREEN_SEG, tile_sets[tile_set][piece[2] - 1], (col + 1) * 11 + 111, row * 11 + 7, 11, 11);
        blit(SCREEN_SEG, tile_sets[tile_set][piece[1] - 1], col * 11 + 111, row * 11 + 7, 11, 11);
        blit(SCREEN_SEG, tile_sets[tile_set][piece[0] - 1], (col - 1) * 11 + 111, row * 11 + 7, 11, 11);
    }
}

/* 1C8F */
void new_piece(void)
{
    piece[0] = (long)rand() * num_symbols / 32767L + 1;
    piece[1] = (long)rand() * num_symbols / 32767L + 1;
    piece[2] = (long)rand() * num_symbols / 32767L + 1;
}

/* 1CF7 */
void init_game(int argc, char *argv[])
{
    str_copy(shell_cmd, "PAUSE");
    if (argc > 1)
        str_copy(shell_cmd, argv[1]);

    if (vga_present() < 7) {
        dos_puts("A Color VGA or MCGA video card is required to run FallOut\n\n$");
        exit(-1);
    }
    if (allocmem(4000, &save_seg) != -1) {
        dos_puts("Not enough MEMORY for FallOut to start.$");
        dos_puts("You need at least 256k! Program Terminated.\n$");
        exit(-1);
    }
    if (allocmem(4000, &back_seg) != -1) {
        dos_puts("Not enough MEMORY for FallOut to start.$");
        dos_puts("You need at least 256k! Program Terminated.\n$");
        exit(-1);
    }
    init_font8();
    init_font6();
    sound(1);
    delay(10);
    nosound();
    transition = trans_dissolve;
    srand((unsigned)time(NULL));
    set_text_color(0);
    load_high_scores();
    set_video_mode(0x13);
    set_default_colors();
    set_game_palette();
    draw_playfield();
    (*transition)(back_seg, SCREEN_SEG);
}

/* 1DDE: background, empty well, score panel and high scores into back_seg. */
void draw_playfield(void)
{
    draw_background(back_seg);
    fill_rect(back_seg, 111, 6, 99, 188, 0);
    draw_box(back_seg, 110, 6, 101, 189, 0x41);
    draw_score_panel(back_seg);
    draw_high_scores(back_seg);
}

/* 1E36 */
int new_game(void)
{
    reset_game();
    main_menu();
    cycle_colors();
    draw_playfield();
    transition = transitions[trans_index++];
    if (transition == 0) {
        trans_index = 0;
        transition = transitions[trans_index++];
    }
    if (cheat)
        draw_cheat_mode(back_seg);
    (*transition)(back_seg, SCREEN_SEG);
    return 0;
}

/* 1E97 */
void reset_game(void)
{
    score = 0;
    total = 0;
    next_level = 100;
    bonus = 0;
    cleared = 0;
    sound_on = 1;
    speed = 2;
    level = 0;
    board_clear((char *)board);
    board_clear((char *)marks);
    bomb_active = 0;
    quit_game = 0;
}

/* 1EF4 */
void add_score(void)
{
    int n;

    n = board_count((char *)marks);
    cleared += n;
    total += n;
    score += n * multiplier / 2;
}

/* 1F25 */
void level_up(void)
{
    cleared = 0;
    next_level = 100;
    score += level * 100;
    level++;
    multiplier += 2;
    show_level(SCREEN_SEG);
}

/* 1F58 */
void shutdown(int save)
{
    freemem(save_seg);
    freemem(back_seg);
    set_video_mode(3);
    if (save)
        save_high_scores();
}

/* 1F7E: tile the screen with 20x20 bevelled squares, darker towards the top. */
void draw_background(unsigned seg)
{
    int y, x;
    int n;

    n = 0;
    for (y = 0; y < 200; y += 20) {
        make_bg_tile(n++);
        for (x = 0; x < 320; x += 20)
            blit(seg, (unsigned char *)bg_tile, x, y, 20, 20);
    }
}

/* 1FCC */
void draw_cheat_mode(unsigned seg)
{
    tint_rect(seg, 223, 176, 90, 15, 0x41);
    draw_panel(seg, 225, 174, 90, 15, col_face);
    draw_text_bg(seg, "CHEAT MODE", 232, 178);
}

/* 2021 */
void show_bombs(unsigned seg)
{
    char buf[8];

    sprintf(buf, "%05d", bombs);
    draw_text_bg(seg, buf, 270, 77);
}

/* 2052 */
void show_score(unsigned seg)
{
    char buf[8];

    if (score > 60000L)
        str_copy(buf, "-WOW-");
    else
        sprintf(buf, "%05lu", score);
    set_text_bg(col_face);
    set_text_color(0);
    draw_text_bg(seg, buf, 270, 37);
}

/* 20B8 */
void show_level(unsigned seg)
{
    char buf[8];

    sprintf(buf, "%05u", level + 1);
    set_text_bg(col_face);
    set_text_color(0);
    draw_text_bg(seg, buf, 270, 47);
}

/* 20FB */
void show_total(unsigned seg)
{
    char buf[8];

    if (total > 60000L)
        str_copy(buf, "-WOW-");
    else
        sprintf(buf, "%05lu", total);
    set_text_bg(col_face);
    set_text_color(0);
    draw_text_bg(seg, buf, 270, 57);
}

/* 2161 */
void show_bonus(unsigned seg)
{
    char buf[8];

    if (bonus > 60000u)
        str_copy(buf, "-WOW-");
    else
        sprintf(buf, "%05u", bonus);
    set_text_bg(col_face);
    set_text_color(0);
    draw_text_bg(seg, buf, 270, 67);
}

/* 21BA: default table, then FALLOUT.HIS if present. */
void load_high_scores(void)
{
    int i, fd;

    for (i = 0; i < 10; i++) {
        if (i == 9)
            hiscore[i].score = (i + 1) * 800;
        else
            hiscore[i].score = (i + 1) * 500;
        hiscore[i].slot = i;
    }
    str_copy(hiscore[9].name, "BRAD");
    str_copy(hiscore[8].name, "- - -");
    str_copy(hiscore[7].name, "PETE");
    str_copy(hiscore[6].name, "JOHN");
    str_copy(hiscore[5].name, "TONY");
    str_copy(hiscore[4].name, "MIKE");
    str_copy(hiscore[3].name, "VINCE");
    str_copy(hiscore[2].name, "FRED");
    str_copy(hiscore[1].name, "ANDY");
    str_copy(hiscore[0].name, "- - -");

    fd = open("FALLOUT.HIS", O_BINARY);
    if (fd != -1) {
        dos_read(fd, hiscore, sizeof hiscore);
        close(fd);
        scramble((unsigned char *)hiscore, sizeof hiscore);
    }
}

/* 22D4 */
void save_high_scores(void)
{
    int fd;

    scramble((unsigned char *)hiscore, sizeof hiscore);
    fd = creat("FALLOUT.HIS", S_IREAD | S_IWRITE);
    if (fd != -1) {
        dos_write(fd, hiscore, sizeof hiscore);
        close(fd);
    }
}

/* 2310: the high score file is XORed with 2, 4, 6, ... */
void scramble(unsigned char *p, unsigned n)
{
    unsigned i;
    unsigned char k;

    k = 0;
    for (i = 0; i < n; i++)
        *p++ ^= (k += 2);
}

/* 2331 */
void check_high_score(void)
{
    int i, slot;

    if (cheat)
        return;
    slot = -1;
    for (i = 0; i < 10; i++)
        if (hiscore[i].score < score)
            slot = i;
    if (slot > -1) {
        for (i = 0; i < slot; i++) {
            hiscore[i].score = hiscore[i + 1].score;
            str_copy(hiscore[i].name, hiscore[i + 1].name);
        }
        hiscore[slot].score = score;
        enter_initials(hiscore[slot].name);
        copy_seg(SCREEN_SEG, save_seg, 32000);
        draw_high_scores(save_seg);
        dissolve9(save_seg, SCREEN_SEG);
    }
}

/* 2413: build a 20x20 bevelled background tile (only 19x19 is drawn). */
void make_bg_tile(int shade)
{
    int i, j;

    shade -= 10;
    for (i = 0; i < 19; i++)
        for (j = 0; j < 19; j++)
            bg_tile[i][j] = col_face + shade;
    for (j = 0; j < 19; j++)
        bg_tile[j][18] = col_light + shade;
    for (j = 0; j < 19; j++)
        bg_tile[0][j] = col_light + shade;
    for (j = 1; j < 19; j++)
        bg_tile[j][0] = col_dark + shade;
    for (j = 0; j < 18; j++)
        bg_tile[18][j] = col_dark + shade;
}

/* 24AA */
int room_for_piece(void)
{
    if (quit_game)
        return 0;
    if (board[0][4] || board[2][4] || board[1][4])
        return 0;
    else
        return 1;
}

/* 24DA: squeeze the next piece into the top of the full well. */
void game_over_anim(int row, int col)
{
    if (board[row - 2][col] == 0) {
        board[row - 2][col] = piece[2];
        draw_board();
        delay(20);
        board[row - 2][col] = 0;
        if (board[row - 1][col] == 0) {
            board[row - 2][col] = piece[1];
            board[row - 1][col] = piece[2];
            draw_board();
            delay(20);
            board[row - 2][col] = board[row - 1][col] = 0;
        }
    }
}

/* 2587: count levels and total into the bonus, then the bonus into the score. */
void tally_bonus(void)
{
    int unused;     /* declared but never used in the original */

    while (level != 0) {
        bonus += (num_symbols < 7) ? num_symbols * 50 : 1000;
        show_bonus(SCREEN_SEG);
        level--;
        show_level(SCREEN_SEG);
        nosound();
        if (sound_on)
            sound(60);
        delay(1);
        if (sound_on)
            sound(100);
        delay(1);
        if (sound_on)
            sound(150);
        delay(1);
        nosound();
        delay(250);
    }
    while (total != 0) {
        bonus++;
        show_bonus(SCREEN_SEG);
        total--;
        show_total(SCREEN_SEG);
        nosound();
        if (sound_on)
            sound((total & 3) * 250);
        delay(1);
        nosound();
    }
    while (bonus != 0) {
        bonus--;
        score++;
        if ((bonus & 0xF) == 0) {
            show_bonus(SCREEN_SEG);
            show_score(SCREEN_SEG);
            if (sound_on)
                sound(score % 5 * 25);
            delay(0);
            nosound();
        }
    }
    show_bonus(SCREEN_SEG);
    show_score(SCREEN_SEG);
}

/* 26E8 */
void set_default_colors(void)
{
    int c = 0x7A;

    col_face   = c;
    col_light  = c - 32;
    col_dark   = c + 32;
    col_text   = c - 32;
    col_shadow = c - 42;
}

/* 2711: every new game uses the next shade, bouncing between 0x7A and 0x8E. */
void cycle_colors(void)
{
    col_face   = color_base;
    col_light  = color_base - 32;
    col_dark   = color_base + 32;
    col_text   = color_base - 32;
    col_shadow = color_base - 42;
    color_base += color_step;
    if (color_base <= 0x7A || color_base >= 0x8E)
        color_step = -color_step;
}

/* 275A: build a 10x6 brick with a light top/left and dark bottom/right edge. */
void make_brick(int light, int face, int dark)
{
    int i, j;

    for (i = 0; i < 6; i++)
        for (j = 0; j < 10; j++)
            brick[i][j] = face;
    for (j = 0; j < 6; j++)
        brick[j][0] = light;
    for (j = 0; j < 10; j++)
        brick[0][j] = light;
    for (j = 1; j < 6; j++)
        brick[j][9] = dark;
    for (j = 1; j < 9; j++)
        brick[5][j] = dark;
}

/* ========================================================= title screen */

int band_width   = 10;              /* DS:3D14  width of a wave column        */
int num_stars    = 40;              /* DS:3D16  F1 on the title adds more     */
int slow_machine = 0;               /* DS:3D18  skip stars/strip if slow      */

#include "data/bricks.inc"          /* DS:3D1A brick_img, bricks, brick_pattern*/

int wave_pos = 0;                   /* DS:3F7A */
int wave_dir = 1;                   /* DS:3F7C */

struct text_line about_left[] = {   /* DS:3F7E */
    {   5,  45, "    #Controls$" },
    {   5,  54, "LEFT...4,LEFT,J" },
    {   5,  62, "RIGHT..6,RIGHT,L" },
    {   5,  70, "DROP...2,DOWN" },
    {   5,  78, "SWAP...5" },
    {   5,  86, "ROTATE.8,UP,TAB" },
    {   5,  94, "BOMB...B" },
    {   5, 102, "CHANGE.PLUS" },
    {   5, 110, "PAUSE..P" },
    {   5, 118, "SOUND..S TOGGLES" },
    {   5, 126, "F10....QUICK QUIT" },
    {   5, 134, "ESC....BOSS KEY" },
    {   5, 142, "  Programming." },
    {   5, 150, " Brad P. Taylor" },
    {   5, 158, "Special thanks to" },
    {   5, 166, "  Peter Crayne" },
    {   5, 174, "   John Fogh" },
    {   0,   0, 0 }
};

struct text_line about_right[] = {  /* DS:3FEA */
    { 120,  45, "          #Description$" },
    { 120,  54, "The object of the game is simple:" },
    { 120,  62, "guide the falling piece so that" },
    { 120,  70, "it forms a horizontal, vertical" },
    { 120,  78, "or diagonal line of 3 or more of" },
    { 120,  86, "the same symbol. Once one of" },
    { 120,  94, "these lines is formed, the " },
    { 120, 102, "symbols explode and the symbols" },
    { 120, 110, "above fall into the empty places." },
    { 120, 118, "The falling piece can be rotated," },
    { 120, 126, "swapped or slid into place. When" },
    { 120, 134, "there is no room for a new piece" },
    { 120, 142, "to enter the playfield then the" },
    { 120, 150, "game is over. If you like this" },
    { 120, 158, "game send 10 dollars to Brad P." },
    { 120, 166, "Taylor, P.O. BOX 1755 Rohnert " },
    /* the original data has one extra NUL after this string */
    { 120, 174, "Park California, Zipcode 94927.\0" },
    {   0,   0, 0 }
};

/* 27DF: the animated title / credits screen.  With timeout != 0 it returns
   after 1500 ticks (attract mode), otherwise on the first key. */
void title_screen(int timeout)
{
    int done;
    int unused;
    unsigned long wave_t, strip_t, star_t, end_t;

    star_t = get_ticks();
    title_init();
    draw_brick_band(0, 40, 128);
    draw_brick_band(40, 80, 112);
    done = 0;
    unused = 0;
    wave_t = get_ticks();
    wave_scroll(SCREEN_SEG);
    if (get_ticks() - star_t > 32)       /* one wave frame took > 32 ticks */
        slow_machine = 1;
    else
        slow_machine = 0;
    strip_t = get_ticks();
    star_t = get_ticks();
    if (timeout)
        end_t = get_ticks() + 1500;

    do {
        if (timeout && get_ticks() > end_t)
            done = 1;
        if (kbhit()) {
            if (getkey() == 0) {
                switch (getkey()) {
                case 0x3B:                      /* F1: more stars */
                    if (num_stars < 200)
                        num_stars++;
                    break;
                case 0x44:                      /* F10 */
                    shutdown(0);
                    exit(0);
                    break;
                }
            } else
                done = 1;
        }
        /* the tick counter wraps at midnight */
        if (get_ticks() < wave_t)
            wave_t = get_ticks();
        if (get_ticks() < strip_t)
            strip_t = get_ticks();
        if (get_ticks() < star_t)
            star_t = get_ticks();

        if (get_ticks() > wave_t) {
            wave_scroll(SCREEN_SEG);
            wave_t = get_ticks();
        } else if (slow_machine == 0) {
            if (get_ticks() > strip_t) {
                scroll_tile_strip(SCREEN_SEG);
                strip_t = get_ticks();
            } else if (get_ticks() > star_t) {
                move_stars(SCREEN_SEG);
                star_t = get_ticks();
            }
        }
    } while (done == 0);
    trans_bands(save_seg, SCREEN_SEG);
}

/* 299E */
void title_init(void)
{
    init_stars();
    fill_rect(back_seg, 0, 0, 160, 200, 0);
    fill_rect(back_seg, 160, 0, 160, 200, 0);
    draw_brick_band(0, 40, 128);
    draw_brick_band(40, 80, 112);
    wave_scroll(back_seg);
    fill_rect(back_seg, 0, 40, 160, 40, 0);
    fill_rect(back_seg, 160, 40, 160, 40, 0);
    draw_about_text(back_seg);
    move_stars(back_seg);
    draw_tile_strip(back_seg);
    trans_bands(back_seg, SCREEN_SEG);
    strip_pos = 0;
    strip_scroll = 0;
}

/* 2A72: a 40-line band of 10x10 bricks into back_seg (top..bottom), with
   shaded and raised bricks chosen by brick_pattern. */
void draw_brick_band(int top, int bottom, int color)
{
    int y, x;
    int n, k;

    n = 0;
    for (y = top; y < bottom; y += 10) {
        k = n++ % 4;
        for (x = 0; x < 320; x += 10)
            blit(back_seg, bricks[k++ % 4], x, y, 10, 10);
    }
    n = 0;
    for (y = top + 6; top + 36 > y; y += 6)
        for (x = 2; x < 320; x += 10)
            if (brick_pattern[n++])
                tint_rect(back_seg, x, y, 10, 6, 0x41);
    k = color;
    n = 0;
    for (y = top + 4; top + 34 > y; y += 6) {
        make_brick(k - 32, k, k + 32);
        for (x = 0; x < 320; x += 10) {
            if (brick_pattern[n])
                blit(back_seg, (unsigned char *)brick, x, y, 10, 6);
            n++;
        }
        k++;
    }
}

/* 2B93: copy the brick band (y 40..80 of back_seg) to y 0..40 of seg with
   each column shifted by the wave table. */
void wave_scroll(unsigned seg)
{
    int x, off;
    int h;

    for (x = off = 0; x < 320; x += band_width) {
        h = wave[(wave_pos + off) % 1900];
        off += wave_dir;
        if (wave_pos + off > 1900 || wave_pos + off < 1)
            wave_dir = -wave_dir;
        copy_rect(seg, back_seg, x, 0, x, 0, band_width, 40 - h);
        copy_rect(seg, back_seg, x, 40 - h, x, 80 - h, band_width, h);
    }
    wave_pos += wave_dir;
}

/* 2C35 */
void draw_about_text(unsigned seg)
{
    int i;

    set_small_color(0x11);
    for (i = 0; about_left[i].text != 0; i++)
        draw_small_text(seg, about_left[i].text, about_left[i].x, about_left[i].y);
    for (i = 0; about_right[i].text != 0; i++)
        draw_small_text(seg, about_right[i].text, about_right[i].x, about_right[i].y);
}

/* 2CCE: a strip of every gem tile along the bottom of the title screen. */
void draw_tile_strip(unsigned seg)
{
    int n, x;
    int i;

    x = 6;
    for (n = i = 0; i < 28; n++, i++, x += 11)
        blit(seg, tile_sets[n / 10 % 10][n % 10], x, 186, 11, 11);
}

/* 2D2E: scroll the tile strip one pixel; redraw it every 11 pixels. */
void scroll_tile_strip(unsigned seg)
{
    int n, x;
    int i;

    n = strip_pos % 100;
    if (strip_scroll % 11 == 0) {
        for (x = i = 0; i < 29; n++, i++, x += 11)
            blit(back_seg, tile_sets[n / 10 % 10][n % 10], x, 186, 11, 11);
        strip_pos++;
    }
    copy_rect(seg, back_seg, 6, 186, strip_scroll, 186, 308, 11);
    strip_scroll++;
    strip_scroll %= 11;
}

/* 2DDE */
void init_stars(void)
{
    int i;

    for (i = 0; i < 200; i++) {
        stars[i].drawn = 0;
        stars[i].x = (long)rand() * 319 / 32767L;
        stars[i].y = (long)rand() * 137 / 32767L + 42;
        stars[i].color = (long)rand() * 16 / 32767L + 64;
        stars[i].speed = (stars[i].color - 63) / 4 + 1;
    }
}

/* 2EA9: move the stars right; they are only drawn over black pixels. */
void move_stars(unsigned seg)
{
    struct star *s;
    int i;

    s = stars;
    for (i = 0; i < num_stars; i++, s++) {
        if (s->drawn) {
            put_pixel(seg, s->x, s->y, 0);
            s->drawn = 0;
        }
        s->x = s->x + s->speed;
        s->x %= 320;
        if (get_pixel(seg, s->x, s->y) == 0) {
            put_pixel(seg, s->x, s->y, s->color);
            s->drawn = 1;
        }
    }
}

/* ============================================================ menus */

/* 2F18 */
void main_menu(void)
{
    char key, done;
    unsigned long timeout;

    copy_seg(SCREEN_SEG, save_seg, 32000);
    tint_rect(save_seg, 83, 33, 150, 120, 0x41);
    draw_main_menu();
    set_text_color(0);
    done = 0;
    unused_3b92 = 0;

    while (done == 0) {
        key = 0;
        timeout = get_ticks() + 1500;
        do {
            if (kbhit()) {
                key = toupper(getkey());
                if (key == 0 && getkey() == 0x44) {
                    shutdown(0);
                    exit(0);
                }
            }
            if (get_ticks() > timeout) {        /* attract mode */
                title_screen(1);
                timeout = get_ticks() + 1500;
            }
        } while (key != '1' && key != '2' && key != '3' && key != '4' &&
                 key != '5' && key != '6' && key != '7' &&
                 key != 'D' && key != 'A' && key != 'Q');

        switch (key) {
        case '1':
            level = 0;
            score = 0;
            bonus = 0;
            next_level = 100;
            if (num_symbols >= 6)
                multiplier = num_symbols;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '2':
            level = 1;
            score = 0;
            bonus = 500;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 200;
            if (num_symbols >= 6)
                multiplier = num_symbols + 1;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '3':
            level = 3;
            score = 0;
            bonus = 1000;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 300;
            if (num_symbols >= 6)
                multiplier = num_symbols + 2;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '4':
            level = 4;
            score = 0;
            bonus = 1000;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 400;
            if (num_symbols >= 6)
                multiplier = num_symbols + 3;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '5':
            level = 5;
            score = 0;
            bonus = 500;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 500;
            if (num_symbols >= 6)
                multiplier = num_symbols + 3;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '6':
            level = 6;
            score = 0;
            bonus = 250;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 600;
            if (num_symbols >= 6)
                multiplier = num_symbols + 3;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case '7':
            level = 7;
            score = 0;
            bonus = 0;
            show_score(back_seg);
            show_level(back_seg);
            next_level = 700;
            if (num_symbols >= 6)
                multiplier = num_symbols + 3;
            else
                multiplier = num_symbols / 2;
            done = 1;
            break;
        case 'D':
            difficulty_menu();
            draw_main_menu();
            break;
        case 'A':
            title_screen(0);
            break;
        case 'Q':
            shutdown(1);
            exit(0);
            break;
        }
    }

    if (num_symbols >= 6) {
        if (level == 2)                         /* (level 2 is never chosen) */
            bombs = 1;
        else if (level >= 3)
            bombs = 2;
        else
            bombs = 0;
    }
    unused_3b94 = 0;
    unused_3b96 = 0;
}

/* 32C8: text is drawn twice: a black copy offset by (-1,+1) as a shadow. */
void draw_main_menu(void)
{
    draw_panel(save_seg, 85, 31, 150, 120, col_face);
    set_text_color(0);
    draw_text(save_seg, "   CHOOSE LEVEL",    87, 35);
    draw_text(save_seg, "  1 - BEGINNER",     81, 49);
    draw_text(save_seg, "  2 - FAMILIAR",     81, 59);
    draw_text(save_seg, "  3 - GETTING GOOD", 81, 69);
    draw_text(save_seg, "  4 - LOOK OUT",     81, 79);
    draw_text(save_seg, "  5 - PRETTY GOOD",  81, 89);
    draw_text(save_seg, "  6 - GOOD LUCK",    81, 99);
    draw_text(save_seg, "  7 - YEAH RIGHT",   81, 109);
    draw_text(save_seg, "  D - DIFFICULTY",   81, 119);
    draw_text(save_seg, "  A - ABOUT GAME",   81, 129);
    draw_text(save_seg, "  Q - QUIT GAME",    81, 139);
    set_text_color(col_shadow);
    draw_text(save_seg, "   CHOOSE LEVEL",    88, 34);
    set_text_color(col_text);
    draw_text(save_seg, "  1 - BEGINNER",     82, 48);
    draw_text(save_seg, "  2 - FAMILIAR",     82, 58);
    draw_text(save_seg, "  3 - GETTING GOOD", 82, 68);
    draw_text(save_seg, "  4 - LOOK OUT",     82, 78);
    draw_text(save_seg, "  5 - PRETTY GOOD",  82, 88);
    draw_text(save_seg, "  6 - GOOD LUCK",    82, 98);
    draw_text(save_seg, "  7 - YEAH RIGHT",   82, 108);
    draw_text(save_seg, "  D - DIFFICULTY",   82, 118);
    draw_text(save_seg, "  A - ABOUT GAME",   82, 128);
    draw_text(save_seg, "  Q - QUIT GAME",    82, 138);
    dissolve9(save_seg, SCREEN_SEG);
}

/* 34F9: choose 5..13 symbols.  'B' here switches on cheat mode. */
void difficulty_menu(void)
{
    int key, n;

    draw_panel(save_seg, 85, 31, 150, 120, col_face);
    set_text_color(0);
    draw_text_centered_shadow(save_seg, "SELECT DIFFICULTY", 34);
    draw_text_centered_shadow(save_seg, "1 - IN TRAINING ", 52);
    draw_text_centered_shadow(save_seg, "2 - BILL OR TED ", 62);
    draw_text_centered_shadow(save_seg, "3 - AVERAGE DUDE", 72);
    draw_text_centered_shadow(save_seg, "4 - AVID GAMER  ", 82);
    draw_text_centered_shadow(save_seg, "5 - GAME FANATIC", 92);
    draw_text_centered_shadow(save_seg, "6 - MENTAL GIANT", 102);
    draw_text_centered_shadow(save_seg, "7 - C PROGRAMMER", 112);
    draw_text_centered_shadow(save_seg, "8 - R. SCIENTIST", 122);
    draw_text_centered_shadow(save_seg, "9 - WONDER STUD ", 132);
    set_text_color(col_shadow);
    draw_text_centered(save_seg, "SELECT DIFFICULTY", 34);
    set_text_color(col_text);
    draw_text_centered(save_seg, "SELECT DIFFICULTY", 34);
    draw_text_centered(save_seg, "1 - IN TRAINING ", 52);
    draw_text_centered(save_seg, "2 - BILL OR TED ", 62);
    draw_text_centered(save_seg, "3 - AVERAGE DUDE", 72);
    draw_text_centered(save_seg, "4 - AVID GAMER  ", 82);
    draw_text_centered(save_seg, "5 - GAME FANATIC", 92);
    draw_text_centered(save_seg, "6 - MENTAL GIANT", 102);
    draw_text_centered(save_seg, "7 - C PROGRAMMER", 112);
    draw_text_centered(save_seg, "8 - R. SCIENTIST", 122);
    draw_text_centered(save_seg, "9 - WONDER STUD ", 132);
    /* highlight the current setting */
    swap_color_rect(save_seg, 85, (num_symbols - 5) * 10 + 52, 140, 10, col_text, col_shadow);
    dissolve7(save_seg, SCREEN_SEG);
    set_text_color(0);

    key = 0;
    do {
        if (kbhit()) {
            key = getkey();
            if (key == 'B') {
                cheat = 1;
                snd_click();
                snd_sweep();
                snd_click();
            }
            if (key == 27)
                break;
            else if (key == 0 && getkey() == 0x44) {
                shutdown(0);
                exit(0);
            }
        }
    } while (key > '9' || key < '1');

    if (key != 27) {
        n = key - 44;                           /* '1' -> 5 symbols */
        /* NB: the original moves the highlight 20 pixels too low here */
        swap_color_rect(SCREEN_SEG, 85, (num_symbols - 5) * 10 + 72, 140, 10, col_shadow, col_text);
        swap_color_rect(SCREEN_SEG, 85, (n - 5) * 10 + 72, 140, 10, col_text, col_shadow);
        num_symbols = n;
    }
}

/* 37BB */
void draw_score_panel(unsigned seg)
{
    tint_rect(seg, 215, 7, 98, 85, 0x41);
    draw_panel(seg, 217, 5, 98, 85, col_face);
    set_text_color(0);
    draw_mask(seg, logo, 227, 13, 81, 15, 0);
    draw_sprite(seg, logo, 226, 12, 81, 15);
    draw_text(seg, "SCORE", 221, 38);
    draw_text(seg, "LEVEL", 221, 48);
    draw_text(seg, "TOTAL", 221, 58);
    draw_text(seg, "BONUS", 221, 68);
    draw_text(seg, "BOMBS", 221, 78);
    set_text_color(col_shadow);
    set_text_color(col_text);
    draw_text(seg, "SCORE", 222, 37);
    draw_text(seg, "LEVEL", 222, 47);
    draw_text(seg, "TOTAL", 222, 57);
    draw_text(seg, "BONUS", 222, 67);
    draw_text(seg, "BOMBS", 222, 77);
    show_score(seg);
    show_level(seg);
    show_total(seg);
    show_bonus(seg);
    show_bombs(seg);
    set_text_color(0);
}

/* 392F: the table is stored lowest first, so it is drawn from slot 9 down. */
void draw_high_scores(unsigned seg)
{
    int i;
    char buf[20];

    tint_rect(seg, 6, 7, 100, 124, 0x41);
    draw_panel(seg, 4, 5, 100, 124, col_face);
    set_text_color(0);
    draw_text(seg, " HIGH SCORES", 3, 10);
    set_text_color(col_shadow);
    draw_text(seg, " HIGH SCORES", 4, 9);
    set_text_color(0);
    for (i = 0; i < 10; i++)
        draw_text(seg, hiscore[9 - i].name, 9, i * 10 + 27);
    for (i = 0; i < 10; i++) {
        if (hiscore[9 - i].score > 60000L)
            str_copy(buf, "-WOW-");
        else
            sprintf(buf, "%05lu", hiscore[9 - i].score);
        draw_text(seg, buf, 58, i * 10 + 26);
    }
    set_text_color(col_text);
    for (i = 0; i < 10; i++)
        draw_text(seg, hiscore[9 - i].name, 10, i * 10 + 26);
    set_text_color(0);
}

/* 3A95: bevelled panel.  Every caller passes a sixth argument (a colour)
   which the original function never used. */
void draw_panel(unsigned seg, int x, int y, int w, int h, int unused)
{
    fill_rect(seg, x, y, w - 1, h - 1, col_face);
    fill_rect(seg, x, y, w - 1, 1, col_light);
    fill_rect(seg, x + w - 1, y, 1, h - 1, col_light);
    fill_rect(seg, x, y + 1, 1, h - 1, col_dark);
    fill_rect(seg, x, y + h - 1, w, 1, col_dark);
}

/* ============================================================ pause */

int bounce_speed = 5;               /* DS:4698 */
int bounce_count = 10;              /* DS:469A */
int bounce_tick  = 0;               /* DS:469C */
int bt_face  = 0x70;                /* DS:469E */
int bt_light = 0x50;                /* DS:46A0 */
int bt_dark  = 0x90;                /* DS:46A2 */
int bt_shade = 1;                   /* DS:46A4 */
int bt_step  = 1;                   /* DS:46A6 */

/* 3B34: P pauses with bouncing tiles; each pause looks a little different. */
void pause_game(void)
{
    bounce_tiles(bounce_speed, bounce_count);
    bounce_speed = (bounce_speed == 5) ? 10 : 5;
    bounce_count++;
    if (bounce_count > 15)
        bounce_count = 8;
}

/* 3B69 */
void bounce_tiles(int speed, int count)
{
    int i;
    int unused;
    char n;
    int y[15], dx[15], dy[15], x[15];

    n = count;
    for (i = 0; i < n; i++) {
        x[i] = i * 20;
        y[i] = i * 5;
        dx[i] = speed;
        dy[i] = speed;
    }
    unused = 1;
    make_bounce_tile();
    while (!kbhit()) {
        for (i = 0; i < n; i++) {
            draw_bounce_tile(SCREEN_SEG, &x[i], &y[i], &dx[i], &dy[i]);
            move_bounce_tile(SCREEN_SEG, &x[i], &y[i], &dx[i], &dy[i], speed);
        }
        bounce_tick++;
        if (bounce_tick > 2) {
            bounce_tick = 0;
            make_bounce_tile();
        }
    }
    if (getkey() == 0 && getkey() == 0x44) {
        shutdown(0);
        exit(0);
    }
}

/* 3C7F (always draws on the screen, whatever seg says) */
void draw_bounce_tile(unsigned seg, int *x, int *y, int *dx, int *dy)
{
    blit(SCREEN_SEG, (unsigned char *)bounce_img, *x, *y, 20, 20);
}

/* 3CA4 */
void move_bounce_tile(unsigned seg, int *x, int *y, int *dx, int *dy, int margin)
{
    *x += *dx;
    if (*x > 295 || *x < 5)
        *dx = -*dx;
    *y += *dy;
    if (*y > 180 - margin || *y < margin)
        *dy = -*dy;
}

/* 3CF5: rebuild the bouncing tile, pulsing its brightness. */
void make_bounce_tile(void)
{
    int i, j;

    for (i = 0; i < 19; i++)
        for (j = 0; j < 19; j++)
            bounce_img[i][j] = bt_face + bt_shade;
    for (j = 0; j < 19; j++)
        bounce_img[j][18] = bt_light + bt_shade;
    for (j = 0; j < 19; j++)
        bounce_img[0][j] = bt_light + bt_shade;
    for (j = 1; j < 19; j++)
        bounce_img[j][0] = bt_dark + bt_shade;
    for (j = 0; j < 18; j++)
        bounce_img[18][j] = bt_dark + bt_shade;
    bt_shade += bt_step;
    if (bt_shade >= 31 || bt_shade <= 0)
        bt_step = -bt_step;
}

/* 3DAD: name entry for a new high score (up to 5 characters). */
void enter_initials(char *name)
{
    int n, done;
    char c;
    char buf[6];

    copy_seg(SCREEN_SEG, back_seg, 32000);
    copy_seg(SCREEN_SEG, save_seg, 32000);
    tint_rect(save_seg, 79, 67, 158, 40, 0x41);
    draw_panel(save_seg, 81, 65, 158, 40, col_face);
    set_text_color(0);
    draw_text_centered_shadow(save_seg, "TOP SCORE", 69);
    draw_text_centered_shadow(save_seg, "ENTER YOUR INITIALS", 80);
    set_text_color(col_shadow);
    draw_text_centered(save_seg, "TOP SCORE", 69);
    set_text_color(col_text);
    draw_text_centered(save_seg, "ENTER YOUR INITIALS", 80);
    fill_rect(save_seg, 140, 91, 40, 9, 0);
    draw_text_centered(save_seg, name, 92);
    dissolve7(save_seg, SCREEN_SEG);

    if (kbhit())                                /* flush the keyboard */
        do
            getkey();
        while (kbhit());

    str_copy(buf, name);
    n = done = 0;
    while (done == 0) {
        c = getkey();
        switch (c) {
        case 0:
            if (getkey() == 0x44) {
                shutdown(0);
                exit(0);
            }
            break;
        case '\r':
            done = 1;
            break;
        case '\b':
            if (n > 0) {
                buf[--n] = 0;
                fill_rect(SCREEN_SEG, 140, 91, 40, 9, 0);
                draw_text_centered(SCREEN_SEG, buf, 92);
            } else {
                str_copy(buf, "     ");
                fill_rect(SCREEN_SEG, 140, 91, 40, 9, 0);
            }
            break;
        case 27:
            str_copy(buf, "     ");
            fill_rect(SCREEN_SEG, 140, 91, 40, 9, 0);
            n = 0;
            break;
        default:
            if (n < 5 && (isalnum(c) || c == ' ' || c == '-')) {
                if (n == 0)
                    fill_rect(SCREEN_SEG, 140, 91, 40, 9, 0);
                buf[n] = toupper(c);
                buf[++n] = 0;
                fill_rect(SCREEN_SEG, 140, 91, 40, 9, 0);
                draw_text_centered(SCREEN_SEG, buf, 92);
            }
            break;
        }
    }
    dissolve7(back_seg, SCREEN_SEG);
    str_copy(name, buf);
    set_text_color(0);
}

#include "data/wave.inc"            /* DS:46F0 char wave[2000]                */

/* ============================================================ sound */

/* 4071 */
void snd_click(void)
{
    if (sound_on) {
        nosound();
        sound(250);
        delay(5);
        sound(1000);
        delay(10);
        nosound();
    }
}

/* 40A3 */
void snd_sweep(void)
{
    int f;

    if (sound_on) {
        for (f = 1000; f > 0; f -= 90) {
            sound(f);
            delay(10);
        }
        nosound();
    }
}

/* 40CD */
void snd_short_sweep(void)
{
    int f;

    if (sound_on) {
        for (f = 1000; f > 0; f -= 250) {
            sound(f);
            delay(5);
        }
        nosound();
    }
}

/* ============================================================ text */

unsigned char text_color = 25;      /* DS:4EC0 */
int  text_bg    = 0;                /* DS:4EC1 */

#include "data/font8.inc"           /* DS:4EC3 font8_img[39][64], font8[38]   */

char unused_58cf = 0;               /* DS:58CF */

#include "data/font6.inc"           /* DS:58D0 font6_img[42][36], font6[41]   */
#include "data/bomb.inc"            /* DS:5F0A bomb1_img, bomb1, bomb2_img, bomb2 */

/* 40F8: map characters to 8x8 glyphs; anything unknown is blank. */
void init_font8(void)
{
    unsigned i;

    for (i = 0; i < 256; i++)
        font8_map[i] = font8_img[37];
    for (i = 'A'; i <= 'Z'; i++)
        font8_map[i] = font8[i - 'A'];
    for (i = 'a'; i <= 'z'; i++)
        font8_map[i] = font8[i - 'a'];
    for (i = '0'; i <= '9'; i++)
        font8_map[i] = font8[i + 26 - '0'];
    font8_map['-'] = font8_img[36];
    font8_map['.'] = font8_img[38];
}

/* 417D */
void set_text_bg(unsigned char c)
{
    text_bg = c;
}

/* 418A */
void set_text_color(char c)
{
    text_color = c;
}

/* 4195: 8x8 text, transparent background. */
void draw_text(unsigned seg, char *s, int x, int y)
{
    while (*s != 0) {
        draw_char(seg, *s++, x, y);
        x += 8;
    }
}

/* 41C2: 8x8 text on a solid text_bg background. */
void draw_text_bg(unsigned seg, char *s, int x, int y)
{
    while (*s != 0) {
        draw_char_bg(seg, *s++, x, y);
        x += 8;
    }
}

/* 41EF */
void draw_text_centered(unsigned seg, char *s, int y)
{
    draw_text(seg, s, (320 - (int)strlen(s) * 8) >> 1, y);
}

/* 4218: the drop shadow for draw_text_centered(): one left, one down. */
void draw_text_centered_shadow(unsigned seg, char *s, int y)
{
    draw_text(seg, s, ((320 - (int)strlen(s) * 8) >> 1) - 1, y + 1);
}

/* 4244 */
void draw_char(unsigned seg, char c, int x, int y)
{
    draw_mask(seg, font8_map[c], x, y, 8, 8, text_color);
}

/* 4272 */
void draw_char_bg(unsigned seg, char c, int x, int y)
{
    draw_mask2(seg, font8_map[c], x, y, 8, 8, text_color, text_bg);
}

/* 42A4 */
void set_small_color(char c)
{
    small_color = c;
}

/* 42AF: never called. */
void set_small_color2(char c)
{
    small_color2 = c;
}

/* 42BA: map characters to 6x6 glyphs. */
void init_font6(void)
{
    int i, j;

    for (i = 0; i < 256; i++)
        font6_map[i] = font6_img[41];
    for (j = 0, i = 'A'; i <= 'Z'; j++, i++)
        font6_map[i] = font6[j];
    for (j = 0, i = 'a'; i <= 'z'; j++, i++)
        font6_map[i] = font6[j];
    for (j = 26, i = '0'; i <= '9'; j++, i++)
        font6_map[i] = font6[j];
    font6_map[':'] = font6[36];
    font6_map['.'] = font6[37];
    font6_map[','] = font6[38];
    font6_map['#'] = font6[39];
    font6_map['$'] = font6[40];
}

/* 4351: 6x6 text (title screen). */
void draw_small_text(unsigned seg, char *s, int x, int y)
{
    while (*s != 0) {
        draw_small_char(seg, *s++, x, y);
        x += 6;
    }
}

/* 437E */
void draw_small_char(unsigned seg, char c, int x, int y)
{
    draw_mask(seg, font6_map[c], x + 1, y, 6, 6, small_color);
}
