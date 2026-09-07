#include "net/tcp/tcp_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include "common/log/log.h"

namespace {

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

bool TcpServer::start(const std::string& ip, std::uint16_t port, int backlog, EpollLoop& loop) {
    loop_ = &loop;
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    const int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, backlog) != 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    set_nonblocking(listen_fd_);
    loop.add(listen_fd_, EPOLLIN, [this](std::uint32_t events) { on_accept(events); });
    return true;
}

void TcpServer::stop() {
    if (listen_fd_ >= 0) {
        loop_->del(listen_fd_);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    for (const auto& kv : clients_) {
        ::close(kv.second.fd);
    }
    clients_.clear();
}

void TcpServer::on_accept(std::uint32_t) {
    for (;;) {
        struct sockaddr_in cli {};
        socklen_t clen = sizeof(cli);
        const int fd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&cli), &clen);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        set_nonblocking(fd);
        Client client;
        client.fd = fd;
        client.ip = ::inet_ntoa(cli.sin_addr);
        client.port = ntohs(cli.sin_port);
        clients_[fd] = client;

        loop_->add(fd, EPOLLIN | EPOLLRDHUP, [this, fd](std::uint32_t events) { on_read(fd, events); });
        LOG_INFO("client connected fd=%d %s:%u", fd, client.ip.c_str(), client.port);
    }
}

void TcpServer::on_read(int fd, std::uint32_t events) {
    if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        close_client(fd);
        return;
    }

    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (data_handler_) {
                data_handler_(fd, buf, static_cast<std::size_t>(n));
            }
        } else if (n == 0) {
            close_client(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            close_client(fd);
            return;
        }
    }
}

void TcpServer::close_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    LOG_INFO("client closed fd=%d %s:%u", fd, it->second.ip.c_str(), it->second.port);
    loop_->del(fd);
    ::close(fd);
    clients_.erase(it);
    if (disconnect_handler_) {
        disconnect_handler_(fd);
    }
}
