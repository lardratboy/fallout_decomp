#ifndef REPLAY_H
#define REPLAY_H
/* argv: SCRIPT TRACE SEED [MAX_TICKS] (the arguments of tools/emu's
   native_harness, without the program name) */
int replay_main(int argc, char *argv[]);
#endif
