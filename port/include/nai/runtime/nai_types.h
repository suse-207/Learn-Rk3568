#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include "nai/nai_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t nai_int_t;
typedef uint32_t nai_uint_t;
typedef int32_t nai_fd_t;

#ifndef NAI_FD_INVALID
#define NAI_FD_INVALID ((nai_fd_t)-1)
#endif

typedef uint16_t nai_htons_t;

#ifdef __cplusplus
}
#endif
