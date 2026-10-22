#ifndef FYND_H
#define FYND_H
#include <stdio.h>

/* Borrows argv and distinct writable streams; retains nothing and never closes
 * streams or changes signal dispositions. Returns 0 complete, 1 incomplete,
 * or 2 invalid invocation. Concurrent calls must not share mutable inputs. */
int fynd_main(int argc, char **argv, FILE *out, FILE *err);
#endif
