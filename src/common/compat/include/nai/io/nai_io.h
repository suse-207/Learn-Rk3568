#pragma once

#include <stdint.h>

#include "nai/io/nai_event.h"
#include "nai/os/nai_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAI_IO_READ 0x01
#define NAI_IO_WRITE 0x02
#define NAI_IO_READWRITE 0x03

#define NAI_IO_MODE 1
#define NAI_IO_BLOCKING 2
#define NAI_IO_RECVTIMEO 3
#define NAI_IO_SENDTIMEO 4
#define NAI_IO_RECVBUF 5
#define NAI_IO_SENDBUF 6
#define NAI_IO_LINGER 7
#define NAI_IO_NOPUSH 8
#define NAI_IO_NODELAY 9
#define NAI_IO_REUSEADDR 10
#define NAI_IO_REUSEPORT 11
#define NAI_IO_SENDFILE 12
#define NAI_IO_LOADFILE 13

typedef struct nai_stream_s nai_stream_t;
typedef struct nai_dgram_s nai_dgram_t;
typedef struct nai_server_s nai_server_t;

typedef int (*nai_stream_cb_f)(nai_stream_t*, int);
typedef int (*nai_dgram_cb_f)(nai_dgram_t*, int);

struct nai_stream_s {
    nai_fd_t fd;
    nai_evloop_t* loop;
    nai_stream_cb_f cb;
    int mode;
    int owned;
};

struct nai_server_s {
    nai_stream_t stream;  // must stay first: owner detection uses container_of
    nai_int_t mode;
};

struct nai_dgram_s {
    nai_fd_t fd;
    nai_evloop_t* loop;
    nai_dgram_cb_f cb;
    int mode;
    int owned;
};

NAI_EXTERN nai_int_t nai_stream_init(nai_stream_t* s);
NAI_EXTERN nai_int_t nai_stream_set_fd(nai_stream_t* s, nai_fd_t fd, nai_int_t type);
NAI_EXTERN nai_int_t nai_stream_inherit(nai_stream_t* s, nai_server_t* listen);
NAI_EXTERN nai_int_t nai_stream_open(nai_stream_t* s, nai_evloop_t* loop);
NAI_EXTERN nai_int_t nai_stream_close(nai_stream_t* s);
NAI_EXTERN nai_int_t nai_stream_shutdown(nai_stream_t* s, nai_int_t how);
NAI_EXTERN nai_int_t nai_stream_set_mode(nai_stream_t* s, nai_int_t mode);
NAI_EXTERN nai_int_t nai_stream_set_opt(nai_stream_t* s, nai_int_t opt, intptr_t value);
NAI_EXTERN nai_int_t nai_stream_set_cb(nai_stream_t* s, nai_stream_cb_f cb);
NAI_EXTERN nai_fd_t nai_stream_get_fd(nai_stream_t const* s);
NAI_EXTERN int64_t nai_stream_read(nai_stream_t* s, void* buf, size_t len);
NAI_EXTERN int64_t nai_stream_write(nai_stream_t* s, const void* buf, size_t len);
NAI_EXTERN int64_t nai_stream_connect(nai_stream_t* s, nai_evloop_t* loop, nai_sockaddr_t* addr, nai_int_t addrlen);

NAI_EXTERN nai_int_t nai_server_init(nai_server_t* s);
NAI_EXTERN nai_int_t nai_server_set_opt(nai_server_t* s, nai_int_t opt, intptr_t value);
NAI_EXTERN nai_int_t nai_server_set_cb(nai_server_t* s, nai_stream_cb_f cb);
NAI_EXTERN nai_int_t nai_server_bind(nai_server_t* s, nai_evloop_t* loop, nai_sockaddr_t* addr, nai_int_t addrlen);
NAI_EXTERN nai_fd_t nai_server_accept(nai_server_t* s, nai_sockaddr_t* addr, nai_int_t* addrlen);
NAI_EXTERN nai_int_t nai_server_close(nai_server_t* s);

NAI_EXTERN nai_int_t nai_dgram_init(nai_dgram_t* d);
NAI_EXTERN nai_int_t nai_dgram_set_opt(nai_dgram_t* d, nai_int_t opt, intptr_t value);
NAI_EXTERN nai_int_t nai_dgram_set_cb(nai_dgram_t* d, nai_dgram_cb_f cb);
NAI_EXTERN nai_int_t nai_dgram_bind(nai_dgram_t* d, nai_evloop_t* loop, nai_sockaddr_t* addr, nai_int_t addrlen);
NAI_EXTERN nai_int_t nai_dgram_connect(nai_dgram_t* d, nai_evloop_t* loop, nai_sockaddr_t* addr, nai_int_t addrlen);
NAI_EXTERN nai_int_t nai_dgram_close(nai_dgram_t* d);
NAI_EXTERN nai_int_t nai_dgram_shutdown(nai_dgram_t* d, nai_int_t how);
NAI_EXTERN nai_int_t nai_dgram_set_mode(nai_dgram_t* d, nai_int_t mode);
NAI_EXTERN nai_fd_t nai_dgram_get_fd(nai_dgram_t const* d);
NAI_EXTERN int64_t nai_dgram_recvfrom(nai_dgram_t* d, void* buf, size_t len, int flags, nai_sockaddr_t* from,
                                      nai_int_t* fromlen);
NAI_EXTERN int64_t nai_dgram_sendto(nai_dgram_t* d, const void* buf, size_t len, int flags,
                                    const nai_sockaddr_t* to, nai_int_t tolen);

#ifdef __cplusplus
}
#endif
