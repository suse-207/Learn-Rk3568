#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "net/epoll/epoll_loop.h"

struct Client {
    int fd = -1;
    std::string ip;
    std::uint16_t port = 0;
};

class TcpServer {
public:
    using DataHandler = std::function<void(int fd, const char* data, std::size_t len)>;
    using DisconnectHandler = std::function<void(int fd)>;

    bool start(const std::string& ip, std::uint16_t port, int backlog, EpollLoop& loop);
    void stop();

    int conn_count() const { return static_cast<int>(clients_.size()); }
    void set_data_handler(DataHandler handler) { data_handler_ = std::move(handler); }
    void set_disconnect_handler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }

private:
    void on_accept(std::uint32_t events);
    void on_read(int fd, std::uint32_t events);
    void close_client(int fd);

    int listen_fd_ = -1;
    EpollLoop* loop_ = nullptr;
    DataHandler data_handler_;
    DisconnectHandler disconnect_handler_;
    std::unordered_map<int, Client> clients_;
};
