/*
 * main.c -- FallOut port entry point.
 *
 *   fallout [options]          play (see rt.c, or --help)
 *   fallout --harness SCRIPT TRACE SEED [MAX_TICKS]
 *       deterministic replay, same arguments, environment and trace format
 *       as tools/emu's native_harness (used by the differential tests)
 *
 * Note: the game is always started with argc == 1.  Its argv[1] would become
 * the boss-key command, and that string sits right after board[] in memory:
 * the original reads it when rotating a piece on the bottom row.
 */
#include <string.h>
#include <SDL3/SDL_main.h>
#include "replay.h"
#include "rt.h"

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "--harness") == 0)
        return replay_main(argc - 2, argv + 2);
    return rt_main(argc, argv);
}
