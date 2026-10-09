/* Key script loader, same format as tools/emu/harness.c and run_original.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keyscript.h"

static struct script_key *buf;
static int n, cap;

static void add_key(unsigned long tick, int b)
{
    if (n == cap) {
        cap = cap ? cap * 2 : 1024;
        buf = realloc(buf, (size_t)cap * sizeof *buf);
        if (!buf) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    buf[n].tick = tick;
    buf[n++].b = (unsigned char)b;
}

int keyscript_load(const char *path, struct script_key **keys)
{
    static const struct { const char *name; int ext, code; } named[] = {
        {"F1", 1, 0x3B}, {"F10", 1, 0x44}, {"UP", 1, 0x48}, {"LEFT", 1, 0x4B},
        {"PAD5", 1, 0x4C}, {"RIGHT", 1, 0x4D}, {"DOWN", 1, 0x50},
        {"ENTER", 0, 13}, {"ESC", 0, 27}, {"BS", 0, 8}, {"TAB", 0, 9},
        {"SPACE", 0, 32}, {0, 0, 0}
    };
    char line[1024], *p, *tok;
    FILE *f = fopen(path, "r");

    if (!f) {
        perror(path);
        return -1;
    }
    buf = 0;
    n = cap = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long tick;
        if ((p = strchr(line, '#')) != 0)
            *p = 0;
        if ((tok = strtok(line, " \t\r\n")) == 0)
            continue;
        tick = strtoul(tok, 0, 10);
        while ((tok = strtok(0, " \t\r\n")) != 0) {
            int i, done = 0;
            for (i = 0; named[i].name; i++)
                if (strcmp(tok, named[i].name) == 0) {
                    if (named[i].ext)
                        add_key(tick, 0);
                    add_key(tick, named[i].code);
                    done = 1;
                    break;
                }
            if (done)
                continue;
            if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
                add_key(tick, (int)strtol(tok, 0, 16));
            else
                for (p = tok; *p; p++)
                    add_key(tick, (unsigned char)*p);
        }
    }
    fclose(f);
    *keys = buf;
    return n;
}
