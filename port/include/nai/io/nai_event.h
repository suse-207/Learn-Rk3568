#pragma once

#include "nai/runtime/nai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nai_evnode_s nai_evnode_t;
typedef struct nai_evloop_s nai_evloop_t;

#define NAI_EV_READ 0x01
#define NAI_EV_WRITE 0x02
#define NAI_EV_EXCEPT 0x04
#define NAI_EV_TIMEOUT 0x08
#define NAI_EV_ERROR 0x10
#define NAI_EV_NOTIFY 0x20
#define NAI_EV_MASK 0x7f

#define NAI_EV_VALUE_MASK 0xffffff00u
#define NAI_EV_VALUE_SHIFT 8
#define nai_ev_notify_from(c) (NAI_EV_NOTIFY | ((uint32_t)(c) << NAI_EV_VALUE_SHIFT))
#define nai_ev_notify_code(e) ((nai_int_t)((e) & NAI_EV_VALUE_MASK) >> NAI_EV_VALUE_SHIFT)

#ifdef __cplusplus
}
#endif
