/* Key scripts (the tests/ .keys files): lines of "TICK token...", '#' comments.
   Tokens: a word of characters, 0xNN (a raw byte), or a name: F1 F10 UP
   LEFT PAD5 RIGHT DOWN (extended: 0 then the scan code), ENTER ESC BS TAB
   SPACE. */
#ifndef KEYSCRIPT_H
#define KEYSCRIPT_H

struct script_key {
    unsigned long tick;
    unsigned char b;
};

/* returns the number of bytes loaded into *keys (malloc'd), or -1 */
int keyscript_load(const char *path, struct script_key **keys);

#endif
