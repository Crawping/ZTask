#ifndef ZTASK_TIMER_H
#define ZTASK_TIMER_H

#include <stdint.h>

int ztask_timeout(uint32_t handle, int time, int session);
void ztask_updatetime(void);
uint32_t ztask_starttime(void);
uint64_t ztask_thread_time(void);	// for profile, in micro second

void ztask_timer_init(void);

#endif
