#pragma once

#include <errno.h>

#ifndef NAI_EAGAIN
#define NAI_EAGAIN EAGAIN
#endif
#ifndef NAI_EINPROGRESS
#define NAI_EINPROGRESS EINPROGRESS
#endif

#ifndef nai_errno
#define nai_errno errno
#endif
