#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "nai/runtime/nai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sockaddr nai_sockaddr_t;
typedef struct sockaddr_in nai_sockaddr_in4_t;
typedef struct sockaddr_in6 nai_sockaddr_in6_t;

#ifndef NAI_SOCK_RW
#define NAI_SOCK_RW 0x03
#endif

#ifndef NAI_FD_TYPE_SOCK
#define NAI_FD_TYPE_SOCK 0
#endif
#ifndef NAI_ADDR_ZONE
#define NAI_ADDR_ZONE 0
#endif

typedef struct nai_sockaddr_info_s nai_sockaddr_info_t;
struct nai_sockaddr_info_s {
    nai_int_t len;
    int32_t addr_off;
    int32_t addr_len;
    int32_t port_off;
    int32_t zone_off;
};

#ifndef _NAI_TYPEDEF_SOCKNBUF_T
#define _NAI_TYPEDEF_SOCKNBUF_T
typedef struct nai_socknbuf_s nai_socknbuf_t;
#endif

struct nai_socknbuf_s {
    nai_int_t len;
    union {
        nai_sockaddr_t addr;
        sockaddr_in addr_in4;
        sockaddr_in6 addr_in6;
        uint8_t storage[128];
    };
};

#define nai_sockaddr_addr_ptr(i, a) ((uint8_t*)((uint8_t*)(a) + (i)->addr_off))
#define nai_sockaddr_port_ptr(i, a) ((uint16_t*)((uint8_t*)(a) + (i)->port_off))
#define nai_sockaddr_get_port(i, a) ((i)->port_off ? ntohs(*nai_sockaddr_port_ptr(i, a)) : 0)

NAI_EXTERN nai_fd_t nai_socket(int domain, int type, int protocol);
NAI_EXTERN nai_int_t nai_sock_set_opt(nai_fd_t s, int level, int optname, const void* optval, nai_int_t optlen);
NAI_EXTERN nai_int_t nai_sock_get_sockname(nai_fd_t s, nai_sockaddr_t* name, nai_int_t* namelen);
NAI_EXTERN nai_int_t nai_sock_get_peername(nai_fd_t s, nai_sockaddr_t* name, nai_int_t* namelen);
NAI_EXTERN nai_int_t nai_sockaddr_pton(const char* src, size_t len, nai_sockaddr_t* dst, nai_int_t* dstlen);
NAI_EXTERN nai_int_t nai_sockaddr_ntop(const nai_sockaddr_t* src, nai_int_t srclen, char* dst, size_t dstlen,
                                       nai_int_t zone);
NAI_EXTERN nai_int_t nai_sockaddr_list(const char* host, const char* port, nai_socknbuf_t* buf, size_t buflen);
NAI_EXTERN const nai_sockaddr_info_t* nai_sockaddr_info(nai_int_t af);
NAI_EXTERN uint16_t nai_htons(uint16_t v);
NAI_EXTERN uint16_t nai_ntohs(uint16_t v);
NAI_EXTERN uint32_t nai_htonl(uint32_t v);

#ifdef __cplusplus
}
#endif
