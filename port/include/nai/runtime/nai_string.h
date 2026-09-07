#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nai_str_s {
    char* base;
    size_t len;
} nai_str_t;

#define nai_strconst(c) nai_string_new((char*)(c))
nai_str_t nai_string_new(char* str);

#ifdef __cplusplus
}
#endif
