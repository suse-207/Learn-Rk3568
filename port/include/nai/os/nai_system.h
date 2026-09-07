#pragma once

#include "nai/runtime/nai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAI_TIME_MSEC 0x0
#define NAI_TIME_USEC 0x1

NAI_EXTERN nai_int_t nai_init();
NAI_EXTERN uint64_t nai_time();
NAI_EXTERN uint64_t nai_tick(nai_int_t units);
NAI_EXTERN nai_int_t nai_sleep(uint64_t usec);

#ifdef __cplusplus
}
#endif
