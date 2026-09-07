#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>

#include "nai/io/nai_io.h"
#include "nai/os/nai_socket.h"
#include "nai/os/nai_system.h"
#include "nai/runtime/nai_errno.h"
#include "nai/runtime/nai_string.h"
#include "port_loop.h"

using portimpl::PortLoop;

namespace {

inline PortLoop* ToLoop(nai_evloop_t* loop)
{
    return reinterpret_cast< PortLoop* >(loop);
}

int SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void EnableReuse(int fd)
{
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
}

bool ParseAddress(std::string const& text, sockaddr_storage& storage, socklen_t& length)
{
    std::string host = text;
    std::uint16_t port = 0;

    std::size_t colon = text.rfind(':');
    if (colon != std::string::npos && text.find(':') == colon) {
        host = text.substr(0, colon);
        try {
            port = static_cast< std::uint16_t >(std::stoul(text.substr(colon + 1)));
        } catch (...) {
            return false;
        }
    }
    if (host.empty()) {
        host = "0.0.0.0";
    }

    std::memset(&storage, 0, sizeof(storage));
    if (inet_pton(AF_INET, host.c_str(), &((sockaddr_in*)&storage)->sin_addr) == 1) {
        sockaddr_in* addr = reinterpret_cast< sockaddr_in* >(&storage);
        addr->sin_family  = AF_INET;
        addr->sin_port    = htons(port);
        length            = sizeof(sockaddr_in);
        return true;
    }
    if (inet_pton(AF_INET6, host.c_str(), &((sockaddr_in6*)&storage)->sin6_addr) == 1) {
        sockaddr_in6* addr = reinterpret_cast< sockaddr_in6* >(&storage);
        addr->sin6_family  = AF_INET6;
        addr->sin6_port    = htons(port);
        length             = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

}  // namespace

nai_str_t nai_string_new(char* str)
{
    nai_str_t result{};
    result.base = str;
    result.len  = str == nullptr ? 0 : std::strlen(str);
    return result;
}

nai_fd_t nai_socket(int const domain, int const type, int const protocol)
{
    int fd = ::socket(domain, type, protocol);
    if (fd >= 0) {
        static_cast< void >(SetNonBlocking(fd));
    }
    return fd;
}

nai_int_t nai_sock_set_opt(nai_fd_t const s, int const level, int const optname, const void* const optval,
                           nai_int_t const optlen)
{
    return setsockopt(s, level, optname, optval, static_cast< socklen_t >(optlen));
}

nai_int_t nai_sock_get_sockname(nai_fd_t const s, nai_sockaddr_t* const name, nai_int_t* const namelen)
{
    socklen_t len = static_cast< socklen_t >(*namelen);
    if (getsockname(s, name, &len) != 0) {
        return -1;
    }
    *namelen = static_cast< nai_int_t >(len);
    return 0;
}

nai_int_t nai_sock_get_peername(nai_fd_t const s, nai_sockaddr_t* const name, nai_int_t* const namelen)
{
    socklen_t len = static_cast< socklen_t >(*namelen);
    if (getpeername(s, name, &len) != 0) {
        return -1;
    }
    *namelen = static_cast< nai_int_t >(len);
    return 0;
}

const nai_sockaddr_info_t* nai_sockaddr_info(nai_int_t const af)
{
    static nai_sockaddr_info_t const kInet = {
        sizeof(sockaddr_in),
        offsetof(sockaddr_in, sin_addr),
        sizeof(in_addr),
        offsetof(sockaddr_in, sin_port),
        0};
    static nai_sockaddr_info_t const kInet6 = {
        sizeof(sockaddr_in6),
        offsetof(sockaddr_in6, sin6_addr),
        sizeof(in6_addr),
        offsetof(sockaddr_in6, sin6_port),
        0};
    switch (af) {
        case AF_INET:
            return &kInet;
        case AF_INET6:
            return &kInet6;
        default:
            return nullptr;
    }
}

nai_int_t nai_sockaddr_pton(const char* const src, size_t const len, nai_sockaddr_t* const dst,
                            nai_int_t* const dstlen)
{
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
    if (!ParseAddress(std::string(src, len), storage, length)) {
        errno = EINVAL;
        return -1;
    }
    if (*dstlen < static_cast< nai_int_t >(length)) {
        errno = ENOBUFS;
        return -1;
    }
    std::memcpy(dst, &storage, length);
    *dstlen = static_cast< nai_int_t >(length);
    return 0;
}

int nai_sockaddr_ntop(const nai_sockaddr_t* const src, nai_int_t const srclen, char* const dst,
                      size_t const dstlen, nai_int_t const zone)
{
    (void)srclen;
    (void)zone;
    void const* addr = nullptr;
    int family       = src->sa_family;
    if (family == AF_INET) {
        addr = &reinterpret_cast< sockaddr_in const* >(src)->sin_addr;
    } else if (family == AF_INET6) {
        addr = &reinterpret_cast< sockaddr_in6 const* >(src)->sin6_addr;
    } else {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return inet_ntop(family, addr, dst, dstlen) != nullptr ? 0 : -1;
}

nai_int_t nai_sockaddr_list(const char* const host, const char* const port, nai_socknbuf_t* const buf,
                            size_t const buflen)
{
    (void)buflen;
    sockaddr_storage storage{};
    socklen_t length = sizeof(storage);
    std::string text = host;
    text += ":";
    text += port;
    if (!ParseAddress(text, storage, length)) {
        errno = EINVAL;
        return -1;
    }
    std::memcpy(&buf->addr, &storage, length);
    buf->len = static_cast< nai_int_t >(length);
    return 0;
}

uint16_t nai_htons(uint16_t const v) { return htons(v); }
uint16_t nai_ntohs(uint16_t const v) { return ntohs(v); }
uint32_t nai_htonl(uint32_t const v) { return htonl(v); }

nai_int_t nai_init()
{
    return 0;
}

uint64_t nai_time()
{
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return static_cast< uint64_t >(tv.tv_sec) * 1000000ULL + static_cast< uint64_t >(tv.tv_usec);
}

uint64_t nai_tick(nai_int_t const units)
{
    return units == NAI_TIME_MSEC ? nai_time() / 1000ULL : nai_time();
}

nai_int_t nai_sleep(uint64_t const usec)
{
    timespec ts{};
    ts.tv_sec  = usec / 1000000ULL;
    ts.tv_nsec = (usec % 1000000ULL) * 1000ULL;
    return nanosleep(&ts, nullptr);
}

nai_int_t nai_stream_init(nai_stream_t* const s)
{
    if (s == nullptr) {
        errno = EINVAL;
        return -1;
    }
    s->fd    = NAI_FD_INVALID;
    s->loop  = nullptr;
    s->cb    = nullptr;
    s->mode  = NAI_IO_READ;
    s->owned = 0;
    return 0;
}

nai_int_t nai_stream_set_fd(nai_stream_t* const s, nai_fd_t const fd, nai_int_t const type)
{
    (void)type;
    if (s == nullptr || fd < 0) {
        errno = EINVAL;
        return -1;
    }
    s->fd    = fd;
    s->owned = 1;
    static_cast< void >(SetNonBlocking(fd));
    return 0;
}

nai_int_t nai_stream_inherit(nai_stream_t* const s, nai_server_t* const listen)
{
    (void)listen;
    return s == nullptr ? -1 : 0;
}

nai_int_t nai_stream_open(nai_stream_t* const s, nai_evloop_t* const loop)
{
    if (s == nullptr || loop == nullptr || s->fd < 0) {
        errno = EINVAL;
        return -1;
    }
    s->loop = loop;
    static_cast< void >(SetNonBlocking(s->fd));
    auto* loopImpl = ToLoop(loop);
    if (!loopImpl->AddFd(s->fd, POLLIN, [s](uint32_t const events) {
            if (s->cb != nullptr) {
                s->cb(s, static_cast< int >(events));
            }
        })) {
        return -1;
    }
    return 0;
}

nai_int_t nai_stream_close(nai_stream_t* const s)
{
    if (s == nullptr) {
        return -1;
    }
    if (s->loop != nullptr && s->fd >= 0) {
        static_cast< void >(ToLoop(s->loop)->DelFd(s->fd));
    }
    if (s->fd >= 0 && s->owned) {
        ::close(s->fd);
    }
    s->fd   = NAI_FD_INVALID;
    s->loop = nullptr;
    s->owned = 0;
    return 0;
}

nai_int_t nai_stream_shutdown(nai_stream_t* const s, nai_int_t const how)
{
    if (s == nullptr || s->fd < 0) {
        return 0;
    }
    return ::shutdown(s->fd, how);
}

nai_int_t nai_stream_set_mode(nai_stream_t* const s, nai_int_t const mode)
{
    if (s == nullptr) {
        return -1;
    }
    s->mode = mode;
    if (s->loop != nullptr && s->fd >= 0 && mode == 0) {
        static_cast< void >(ToLoop(s->loop)->DelFd(s->fd));
    }
    return 0;
}

nai_int_t nai_stream_set_opt(nai_stream_t* const s, nai_int_t const opt, intptr_t const value)
{
    if (s == nullptr || s->fd < 0) {
        errno = EINVAL;
        return -1;
    }
    switch (opt) {
        case NAI_IO_NODELAY: {
            int flag = value != 0 ? 1 : 0;
            return setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        }
        default:
            return 0;
    }
}

nai_int_t nai_stream_set_cb(nai_stream_t* const s, nai_stream_cb_f const cb)
{
    if (s == nullptr) {
        return -1;
    }
    s->cb = cb;
    if (s->loop != nullptr && s->fd >= 0) {
        return ToLoop(s->loop)->UpdateFd(s->fd, [s](uint32_t const events) {
                   if (s->cb != nullptr) {
                       s->cb(s, static_cast< int >(events));
                   }
               })
                   ? 0
                   : -1;
    }
    return 0;
}

nai_fd_t nai_stream_get_fd(nai_stream_t const* const s)
{
    return s == nullptr ? NAI_FD_INVALID : s->fd;
}

int64_t nai_stream_read(nai_stream_t* const s, void* const buf, size_t const len)
{
    if (s == nullptr || s->fd < 0) {
        errno = EBADF;
        return -1;
    }
    return ::read(s->fd, buf, len);
}

int64_t nai_stream_write(nai_stream_t* const s, const void* const buf, size_t const len)
{
    if (s == nullptr || s->fd < 0) {
        errno = EBADF;
        return -1;
    }
    return ::write(s->fd, buf, len);
}

int64_t nai_stream_connect(nai_stream_t* const s, nai_evloop_t* const loop, nai_sockaddr_t* const addr,
                           nai_int_t const addrlen)
{
    if (s == nullptr || addr == nullptr) {
        return -1;
    }
    s->loop = loop;
    if (s->fd < 0) {
        s->fd    = ::socket(addr->sa_family, SOCK_STREAM, 0);
        s->owned = 1;
        static_cast< void >(SetNonBlocking(s->fd));
    }
    return ::connect(s->fd, addr, static_cast< socklen_t >(addrlen));
}

nai_int_t nai_server_init(nai_server_t* const s)
{
    if (s == nullptr) {
        return -1;
    }
    std::memset(s, 0, sizeof(*s));
    s->stream.fd = NAI_FD_INVALID;
    s->stream.owned = 1;
    return 0;
}

nai_int_t nai_server_set_opt(nai_server_t* const s, nai_int_t const opt, intptr_t const value)
{
    (void)s;
    (void)opt;
    (void)value;
    return 0;
}

nai_int_t nai_server_set_cb(nai_server_t* const s, nai_stream_cb_f const cb)
{
    if (s == nullptr) {
        return -1;
    }
    s->stream.cb = cb;
    return 0;
}

nai_int_t nai_server_bind(nai_server_t* const s, nai_evloop_t* const loop, nai_sockaddr_t* const addr,
                          nai_int_t const addrlen)
{
    if (s == nullptr || loop == nullptr || addr == nullptr) {
        errno = EINVAL;
        return -1;
    }
    int fd = ::socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    EnableReuse(fd);
    static_cast< void >(SetNonBlocking(fd));
    if (::bind(fd, addr, static_cast< socklen_t >(addrlen)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, SOMAXCONN) != 0) {
        ::close(fd);
        return -1;
    }

    s->stream.fd    = fd;
    s->stream.loop  = loop;
    s->stream.owned = 1;
    if (!ToLoop(loop)->AddFd(fd, POLLIN, [s](uint32_t const events) {
            if (s->stream.cb != nullptr) {
                s->stream.cb(&s->stream, static_cast< int >(events));
            }
        })) {
        ::close(fd);
        s->stream.fd = NAI_FD_INVALID;
        return -1;
    }
    return 0;
}

nai_fd_t nai_server_accept(nai_server_t* const s, nai_sockaddr_t* const addr, nai_int_t* const addrlen)
{
    if (s == nullptr || s->stream.fd < 0) {
        return NAI_FD_INVALID;
    }
    socklen_t len = addrlen == nullptr ? 0 : static_cast< socklen_t >(*addrlen);
    int fd = ::accept(s->stream.fd, addr, &len);
    if (fd < 0) {
        return NAI_FD_INVALID;
    }
    if (addrlen != nullptr) {
        *addrlen = static_cast< nai_int_t >(len);
    }
    static_cast< void >(SetNonBlocking(fd));
    return fd;
}

nai_int_t nai_server_close(nai_server_t* const s)
{
    if (s == nullptr) {
        return -1;
    }
    if (s->stream.loop != nullptr && s->stream.fd >= 0) {
        static_cast< void >(ToLoop(s->stream.loop)->DelFd(s->stream.fd));
    }
    if (s->stream.fd >= 0) {
        ::close(s->stream.fd);
    }
    s->stream.fd   = NAI_FD_INVALID;
    s->stream.loop = nullptr;
    return 0;
}

nai_int_t nai_dgram_init(nai_dgram_t* const d)
{
    if (d == nullptr) {
        return -1;
    }
    d->fd    = NAI_FD_INVALID;
    d->loop  = nullptr;
    d->cb    = nullptr;
    d->mode  = NAI_IO_READ;
    d->owned = 0;
    return 0;
}

nai_int_t nai_dgram_set_opt(nai_dgram_t* const d, nai_int_t const opt, intptr_t const value)
{
    (void)d;
    (void)opt;
    (void)value;
    return 0;
}

nai_int_t nai_dgram_set_cb(nai_dgram_t* const d, nai_dgram_cb_f const cb)
{
    if (d == nullptr) {
        return -1;
    }
    d->cb = cb;
    if (d->loop != nullptr && d->fd >= 0) {
        return ToLoop(d->loop)->UpdateFd(d->fd, [d](uint32_t const events) {
                   if (d->cb != nullptr) {
                       d->cb(d, static_cast< int >(events));
                   }
               })
                   ? 0
                   : -1;
    }
    return 0;
}

nai_int_t nai_dgram_bind(nai_dgram_t* const d, nai_evloop_t* const loop, nai_sockaddr_t* const addr,
                         nai_int_t const addrlen)
{
    if (d == nullptr || loop == nullptr || addr == nullptr) {
        errno = EINVAL;
        return -1;
    }
    int fd = ::socket(addr->sa_family, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    EnableReuse(fd);
    static_cast< void >(SetNonBlocking(fd));
    if (::bind(fd, addr, static_cast< socklen_t >(addrlen)) != 0) {
        ::close(fd);
        return -1;
    }
    d->fd    = fd;
    d->loop  = loop;
    d->owned = 1;
    if (!ToLoop(loop)->AddFd(fd, POLLIN, [d](uint32_t const events) {
            if (d->cb != nullptr) {
                d->cb(d, static_cast< int >(events));
            }
        })) {
        ::close(fd);
        d->fd = NAI_FD_INVALID;
        return -1;
    }
    return 0;
}

nai_int_t nai_dgram_connect(nai_dgram_t* const d, nai_evloop_t* const loop, nai_sockaddr_t* const addr,
                            nai_int_t const addrlen)
{
    if (d == nullptr || addr == nullptr) {
        return -1;
    }
    if (d->fd < 0) {
        d->fd    = ::socket(addr->sa_family, SOCK_DGRAM, 0);
        d->owned = 1;
        static_cast< void >(SetNonBlocking(d->fd));
    }
    d->loop = loop;
    return ::connect(d->fd, addr, static_cast< socklen_t >(addrlen));
}

nai_int_t nai_dgram_close(nai_dgram_t* const d)
{
    if (d == nullptr) {
        return -1;
    }
    if (d->loop != nullptr && d->fd >= 0) {
        static_cast< void >(ToLoop(d->loop)->DelFd(d->fd));
    }
    if (d->fd >= 0 && d->owned) {
        ::close(d->fd);
    }
    d->fd   = NAI_FD_INVALID;
    d->loop = nullptr;
    d->owned = 0;
    return 0;
}

nai_int_t nai_dgram_shutdown(nai_dgram_t* const d, nai_int_t const how)
{
    if (d == nullptr || d->fd < 0) {
        return 0;
    }
    return ::shutdown(d->fd, how);
}

nai_int_t nai_dgram_set_mode(nai_dgram_t* const d, nai_int_t const mode)
{
    if (d == nullptr) {
        return -1;
    }
    d->mode = mode;
    if (d->loop != nullptr && d->fd >= 0 && mode == 0) {
        static_cast< void >(ToLoop(d->loop)->DelFd(d->fd));
    }
    return 0;
}

nai_fd_t nai_dgram_get_fd(nai_dgram_t const* const d)
{
    return d == nullptr ? NAI_FD_INVALID : d->fd;
}

int64_t nai_dgram_recvfrom(nai_dgram_t* const d, void* const buf, size_t const len, int const flags,
                           nai_sockaddr_t* const from, nai_int_t* const fromlen)
{
    if (d == nullptr || d->fd < 0) {
        errno = EBADF;
        return -1;
    }
    socklen_t socklen = fromlen == nullptr ? 0 : static_cast< socklen_t >(*fromlen);
    ssize_t result    = ::recvfrom(d->fd, buf, len, flags, from, fromlen == nullptr ? nullptr : &socklen);
    if (fromlen != nullptr && result >= 0) {
        *fromlen = static_cast< nai_int_t >(socklen);
    }
    return static_cast< int64_t >(result);
}

int64_t nai_dgram_sendto(nai_dgram_t* const d, const void* const buf, size_t const len, int const flags,
                         const nai_sockaddr_t* const to, nai_int_t const tolen)
{
    if (d == nullptr || d->fd < 0) {
        errno = EBADF;
        return -1;
    }
    return ::sendto(d->fd, buf, len, flags, to, static_cast< socklen_t >(tolen));
}
