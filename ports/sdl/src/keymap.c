/*
 * keymap.c -- SDL key presses to DOS keystrokes.
 *
 * Keys the game reads: digits, letters (case matters: only an uppercase B
 * turns on cheat mode in the difficulty menu), '+', '-', space, Enter, Esc,
 * Backspace, Tab, the arrows, F1 and F10.  Keypad digits always give digits
 * (the game treats 2/4/5/6/8 like Down/Left/swap/Right/Up), as with NumLock
 * on.
 */
#include "keymap.h"

int keymap_translate(SDL_Keycode key, SDL_Keycode shifted, SDL_Keymod mod,
                     int *ascii, int *scan)
{
    int a = -1, s = 0;

    switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: a = 13; break;
    case SDLK_ESCAPE:    a = 27; break;
    case SDLK_BACKSPACE: a = 8; break;
    case SDLK_TAB:
        if (mod & SDL_KMOD_SHIFT)
            a = 0, s = 0x0F;
        else
            a = 9;
        break;
    case SDLK_UP:       a = 0; s = 0x48; break;
    case SDLK_LEFT:     a = 0; s = 0x4B; break;
    case SDLK_RIGHT:    a = 0; s = 0x4D; break;
    case SDLK_DOWN:     a = 0; s = 0x50; break;
    case SDLK_HOME:     a = 0; s = 0x47; break;
    case SDLK_END:      a = 0; s = 0x4F; break;
    case SDLK_PAGEUP:   a = 0; s = 0x49; break;
    case SDLK_PAGEDOWN: a = 0; s = 0x51; break;
    case SDLK_INSERT:   a = 0; s = 0x52; break;
    case SDLK_DELETE:   a = 0; s = 0x53; break;
    case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5:
    case SDLK_F6: case SDLK_F7: case SDLK_F8: case SDLK_F9: case SDLK_F10:
        a = 0;
        s = 0x3B + (int)(key - SDLK_F1);
        break;
    case SDLK_KP_0: a = '0'; break;
    case SDLK_KP_1: case SDLK_KP_2: case SDLK_KP_3: case SDLK_KP_4:
    case SDLK_KP_5: case SDLK_KP_6: case SDLK_KP_7: case SDLK_KP_8:
    case SDLK_KP_9:
        a = '1' + (int)(key - SDLK_KP_1);
        break;
    case SDLK_KP_PLUS:     a = '+'; break;
    case SDLK_KP_MINUS:    a = '-'; break;
    case SDLK_KP_MULTIPLY: a = '*'; break;
    case SDLK_KP_DIVIDE:   a = '/'; break;
    case SDLK_KP_PERIOD:   a = '.'; break;
    default:
        if (key >= SDLK_A && key <= SDLK_Z) {
            if (mod & SDL_KMOD_CTRL)
                a = (int)(key - SDLK_A) + 1;            /* ^A .. ^Z */
            else if (!(mod & SDL_KMOD_SHIFT) != !(mod & SDL_KMOD_CAPS))
                a = (int)(key - SDLK_A) + 'A';
            else
                a = (int)(key - SDLK_A) + 'a';
        } else if (!(mod & SDL_KMOD_CTRL) && shifted >= 32 && shifted < 127) {
            a = (int)shifted;
        }
        break;
    }
    if (a < 0)
        return 0;
    *ascii = a;
    *scan = s;
    return 1;
}
