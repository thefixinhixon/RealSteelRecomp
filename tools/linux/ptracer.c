// ptracer.c: let any process of this user attach to the game (eu-stack, gdb -p) under
// ptrace_scope=1, without root. See docs/headless.md §6.
// build: gcc -shared -fPIC -o ptracer.so ptracer.c
// use:   LD_PRELOAD=$PWD/ptracer.so tools/linux/headless_run.sh ...
#include <sys/prctl.h>

__attribute__((constructor)) static void allow(void) { prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY); }
