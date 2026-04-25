#ifndef DAEMON_H
#define DAEMON_H

#include "types.h"

void daemon_init(void);
void daemon_poll(int prompt_idle);

#endif
