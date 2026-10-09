/* Unit tests for the parts of the realtime platform that the replay parity
   test cannot reach: the key map, the BIOS clock conversion and the
   speaker. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keymap.h"
#include "rtclock.h"
#include "speaker.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* keystroke for (key, shifted, mod); -1/-1 if none */
static void key(SDL_Keycode k, SDL_Keycode shifted, SDL_Keymod mod, int *a, int *s)
{
    if (!keymap_translate(k, shifted, mod, a, s))
        *a = *s = -1;
}

#define EXPECT_KEY(k, sh, mod, ea, es) do { int a, s; key(k, sh, mod, &a, &s); \
    if (a != (ea) || s != (es)) { printf("FAIL %s:%d: %s -> (%d,%02x), want (%d,%02x)\n", \
        __FILE__, __LINE__, #k, a, s, (ea), (es)); failures++; } } while (0)

static void test_keymap(void)
{
    /* letters: Shift xor CapsLock; only uppercase B enables cheat mode */
    EXPECT_KEY(SDLK_B, SDLK_B, SDL_KMOD_NONE, 'b', 0);
    EXPECT_KEY(SDLK_B, SDLK_B, SDL_KMOD_LSHIFT, 'B', 0);
    EXPECT_KEY(SDLK_B, SDLK_B, SDL_KMOD_CAPS, 'B', 0);
    EXPECT_KEY(SDLK_B, SDLK_B, SDL_KMOD_CAPS | SDL_KMOD_RSHIFT, 'b', 0);
    EXPECT_KEY(SDLK_C, SDLK_C, SDL_KMOD_LCTRL, 3, 0);
    /* digits and the symbols the game reads */
    EXPECT_KEY(SDLK_7, SDLK_7, SDL_KMOD_NONE, '7', 0);
    EXPECT_KEY(SDLK_EQUALS, SDLK_PLUS, SDL_KMOD_LSHIFT, '+', 0);
    EXPECT_KEY(SDLK_MINUS, SDLK_MINUS, SDL_KMOD_NONE, '-', 0);
    EXPECT_KEY(SDLK_SPACE, SDLK_SPACE, SDL_KMOD_NONE, ' ', 0);
    /* keypad: digits whatever NumLock says */
    EXPECT_KEY(SDLK_KP_5, SDLK_KP_5, SDL_KMOD_NONE, '5', 0);
    EXPECT_KEY(SDLK_KP_2, SDLK_KP_2, SDL_KMOD_NUM, '2', 0);
    EXPECT_KEY(SDLK_KP_0, SDLK_KP_0, SDL_KMOD_NONE, '0', 0);
    EXPECT_KEY(SDLK_KP_9, SDLK_KP_9, SDL_KMOD_NONE, '9', 0);
    EXPECT_KEY(SDLK_KP_PLUS, SDLK_KP_PLUS, SDL_KMOD_NONE, '+', 0);
    EXPECT_KEY(SDLK_KP_ENTER, SDLK_KP_ENTER, SDL_KMOD_NONE, 13, 0);
    /* control keys */
    EXPECT_KEY(SDLK_RETURN, SDLK_RETURN, SDL_KMOD_NONE, 13, 0);
    EXPECT_KEY(SDLK_ESCAPE, SDLK_ESCAPE, SDL_KMOD_NONE, 27, 0);
    EXPECT_KEY(SDLK_BACKSPACE, SDLK_BACKSPACE, SDL_KMOD_NONE, 8, 0);
    EXPECT_KEY(SDLK_TAB, SDLK_TAB, SDL_KMOD_NONE, 9, 0);
    /* extended keys: 0 + scan code */
    EXPECT_KEY(SDLK_UP, SDLK_UP, SDL_KMOD_NONE, 0, 0x48);
    EXPECT_KEY(SDLK_LEFT, SDLK_LEFT, SDL_KMOD_NONE, 0, 0x4B);
    EXPECT_KEY(SDLK_RIGHT, SDLK_RIGHT, SDL_KMOD_NONE, 0, 0x4D);
    EXPECT_KEY(SDLK_DOWN, SDLK_DOWN, SDL_KMOD_NONE, 0, 0x50);
    EXPECT_KEY(SDLK_F1, SDLK_F1, SDL_KMOD_NONE, 0, 0x3B);
    EXPECT_KEY(SDLK_F10, SDLK_F10, SDL_KMOD_NONE, 0, 0x44);
    /* nothing above 0x7F, nothing for unmapped keys */
    EXPECT_KEY(SDLK_E, 0xE9, SDL_KMOD_NONE, 'e', 0);
    EXPECT_KEY(0xE9, 0xE9, SDL_KMOD_NONE, -1, -1);
    EXPECT_KEY(SDLK_F11, SDLK_F11, SDL_KMOD_NONE, -1, -1);
    EXPECT_KEY(SDLK_LSHIFT, SDLK_LSHIFT, SDL_KMOD_LSHIFT, -1, -1);
}

static void test_clock(void)
{
    uint64_t t;

    CHECK(ticks_at_us(0) == 0);
    CHECK(ticks_at_us(1000000) == 18);                  /* 18.2065 Hz */
    CHECK(ticks_at_us(3600ull * 1000000) == 65543);     /* one hour   */
    CHECK(ticks_at_us(86400ull * 1000000) == 1573042);  /* one day (the BIOS
                                                           rolls over at 0x1800B0) */
    for (t = 1; t < 2000000; t += 997) {
        CHECK(ticks_at_us(us_of_tick(t)) == t);
        CHECK(ticks_at_us(us_of_tick(t) - 1) == t - 1);
    }
}

/* Render the unfiltered speaker at one sample per PIT clock up to `to` and
   compare with `want` ('1' high, '0' low, one character per clock). */
static void expect_wave(struct speaker *s, uint64_t to, const char *want, int line)
{
    float out[256];
    char got[257];
    size_t i, n = speaker_render(s, to, out, 256);

    for (i = 0; i < n; i++)
        got[i] = out[i] == 1.0f ? '1' : out[i] == 0.0f ? '0' : '?';
    got[n] = 0;
    if (strcmp(got, want) != 0) {
        printf("FAIL %s:%d: wave\n  got  %s\n  want %s\n", __FILE__, line, got, want);
        failures++;
    }
}
#define EXPECT_WAVE(s, to, want) expect_wave(s, to, want, __LINE__)

static void test_speaker_wave(void)
{
    struct speaker s;

    /* mode 3 starts high; an odd count is high one clock longer */
    speaker_init(&s, PIT_HZ, 1.0f, 0);
    EXPECT_WAVE(&s, 3, "000");
    speaker_write(&s, 10);
    EXPECT_WAVE(&s, 26, "11111000001111100000111");
    speaker_write(&s, 0);                   /* nosound(): low at once */
    CHECK(speaker_off_since(&s) == 26);
    EXPECT_WAVE(&s, 30, "0000");
    speaker_write(&s, 7);
    CHECK(speaker_off_since(&s) == UINT64_MAX);
    EXPECT_WAVE(&s, 46, "1111000111100011");

    /* sound() while on: the new count loads at the end of the half-cycle,
       during a high half ... */
    speaker_init(&s, PIT_HZ, 1.0f, 0);
    speaker_write(&s, 10);
    EXPECT_WAVE(&s, 12, "111110000011");
    speaker_write(&s, 20);
    EXPECT_WAVE(&s, 13, "1");
    speaker_write(&s, 4);                   /* the last count written wins */
    EXPECT_WAVE(&s, 27, "11001100110011");
    /* ... and during a low half */
    speaker_write(&s, 10);
    EXPECT_WAVE(&s, 40, "0011111000001");
    /* nosound() drops a count that has not loaded yet */
    speaker_write(&s, 20);
    EXPECT_WAVE(&s, 41, "1");
    speaker_write(&s, 0);
    EXPECT_WAVE(&s, 50, "000000000");
    speaker_write(&s, 6);
    EXPECT_WAVE(&s, 58, "11100011");
}

static void test_speaker_area(void)
{
    struct speaker s;
    float out[4];
    double spc = PIT_HZ / 1000.0;               /* PIT clocks per sample */

    /* a 100-clock pulse in sample 0, then one across samples 1 and 2 */
    speaker_init(&s, 1000, 1.0f, 0);
    speaker_write(&s, 60000);
    CHECK(speaker_render(&s, 100, out, 4) == 0);
    speaker_write(&s, 0);
    CHECK(speaker_render(&s, 2300, out, 4) == 1);
    speaker_write(&s, 60000);
    CHECK(speaker_render(&s, 2500, out + 1, 3) == 1);
    speaker_write(&s, 0);
    CHECK(speaker_render(&s, 3580, out + 2, 2) == 1);    /* sample 2 ends at 3579.5 */
    CHECK(fabs(out[0] - 100 / spc) < 1e-6);
    CHECK(fabs(out[1] - (2 * spc - 2300) / spc) < 1e-6);
    CHECK(fabs(out[2] - (2500 - 2 * spc) / spc) < 1e-6);
}

static void test_speaker_tone(void)
{
    static float out[48000];
    struct speaker s;
    double mean = 0, peak = 0;
    int i, crossings = 0;

    /* sound(1000) for a second at 48 kHz: 1000 Hz, no DC offset */
    speaker_init(&s, 48000, 1.0f, 1);
    speaker_write(&s, 1193);
    CHECK(speaker_render(&s, PIT_HZ, out, 48000) == 48000);
    for (i = 4800; i < 48000; i++) {
        mean += out[i];
        peak = fmax(peak, fabs(out[i]));
        crossings += (out[i] >= 0) != (out[i - 1] >= 0);
    }
    CHECK(fabs(mean / 43200) < 0.01);
    CHECK(peak > 0.45 && peak < 0.6);
    CHECK(abs(crossings - 1800) <= 2);
    /* off: silent within 30 ms (audio.c relies on this) */
    speaker_write(&s, 0);
    CHECK(speaker_render(&s, 2 * PIT_HZ, out, 48000) == 48000);
    for (i = 1440; i < 2400; i++)
        CHECK(fabs(out[i]) < 1e-3);
}

int main(void)
{
    test_keymap();
    test_clock();
    test_speaker_wave();
    test_speaker_area();
    test_speaker_tone();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}
