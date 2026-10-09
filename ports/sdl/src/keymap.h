#ifndef KEYMAP_H
#define KEYMAP_H

#include <SDL3/SDL_keycode.h>

/*
 * The DOS keystroke (as INT 21h/07h delivers it) for a key press.
 *   key      the event's keycode (unmodified, current layout)
 *   shifted  the same key with Shift applied, from SDL_GetKeyFromScancode()
 *   mod      modifier state
 * Returns 1 and sets *ascii / *scan: an ASCII key gives (ascii, 0); an
 * extended key gives (0, scan code), which DOS returns as two bytes.
 * Returns 0 for keys the game never sees.  Only bytes below 0x80 are ever
 * produced: the game indexes a font table with a signed char.
 */
int keymap_translate(SDL_Keycode key, SDL_Keycode shifted, SDL_Keymod mod,
                     int *ascii, int *scan);

#endif
