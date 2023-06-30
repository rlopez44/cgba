#ifndef CGBA_DEBUG_H
#define CGBA_DEBUG_H

#include <stdio.h>
#include "cgba/cpu.h"

/* Log register and instruction pipeline contents
 * to the given stream. If NULL is provided for the
 * stream, output is written to stdout.
 */
void log_cpu_state(arm7tdmi *cpu, FILE *fptr);

#endif /* CGBA_DEBUG_H */
