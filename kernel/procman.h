#ifndef PROCMAN_H
#define PROCMAN_H

#include <stdint.h>

typedef uint8_t ProcManPid;
#define ProcManPidMax 64

void procManInit(void);
void procManQuit(void);

ProcManPid procManProcessNew(const char *programPath); // Returns ProcManPidMax on failure

#endif
