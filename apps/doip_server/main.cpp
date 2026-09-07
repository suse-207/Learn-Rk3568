#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "common/config/config.h"
#include "common/log/log.h"
#include "doip/doip_server.h"
#include "net/epoll/epoll_loop.h"
#include "net/tcp/tcp_server.h"

namespace {
volatile sig_atomic_t g_running = 1;
}

static void on_signal(int) {
    g_running = 0;
}

int main(int argc, char** argv) {
    std::string cfg_path = "config/platform.json";
    if (argc > 1) {
        cfg_path = argv[1];
    }

    PlatformConfig cfg;
    if (!load_config(cfg_path, cfg)) {
        LOG_ERROR("load config failed: %s", cfg_path.c_str());
        return 1;
    }
    LOG_INFO("config: listen %s:%u backlog=%d max_events=%d",
             cfg.listen_ip.c_str(), cfg.listen_port, cfg.backlog, cfg.max_events);

    EpollLoop loop;
    if (!loop.init(cfg.max_events)) {
        LOG_ERROR("epoll init failed");
        return 1;
    }

    TcpServer server;
    if (!server.start(cfg.listen_ip, cfg.listen_port, cfg.backlog, loop)) {
        LOG_ERROR("tcp server start failed on %s:%u", cfg.listen_ip.c_str(), cfg.listen_port);
        return 1;
    }

    doip::DoipServer doip;
    doip::VehicleIdentity identity{};
    const char* vin = "RK3568PLATFORM001";
    std::memcpy(identity.vin.data(), vin, 17);
    identity.logical_address = 0x0E80;
    identity.eid = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    identity.gid = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
    identity.node_type = 0x01;
    identity.max_open_sockets = 1;
    identity.max_data_size = 4096;
    identity.power_mode = 0x01;
    identity.announcement_addr = cfg.announcement_addr;
    identity.announcement_interval = std::chrono::milliseconds(cfg.announcement_interval_ms);
    identity.initial_inactivity_timeout = std::chrono::milliseconds(cfg.initial_inactivity_ms);
    identity.general_inactivity_timeout = std::chrono::milliseconds(cfg.general_inactivity_ms);
    identity.alive_check_timeout = std::chrono::milliseconds(cfg.alive_check_timeout_ms);
    doip.set_identity(identity);
    doip.set_version("1.0.0");
    doip.set_download_dir(cfg.download_dir);
    server.set_data_handler(
        [&doip](int fd, const char* data, std::size_t len) { doip.on_data(fd, data, len); });
    server.set_disconnect_handler([&doip](int fd) { doip.erase_client(fd); });

    if (!doip.start_discovery(cfg.listen_ip, cfg.listen_port, loop)) {
        LOG_ERROR("DoIP discovery start failed");
        return 1;
    }

    LOG_INFO("DoIP server (M2) listening on %s:%u (Ctrl-C to stop)",
             cfg.listen_ip.c_str(), cfg.listen_port);

    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);

    // 信号处理函数里只置标志位，这里用一个监视线程负责停止事件循环
    std::thread watcher([&loop]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        loop.stop();
    });

    loop.run();

    if (watcher.joinable()) {
        watcher.join();
    }
    doip.stop_discovery();
    server.stop();
    LOG_INFO("server stopped");
    return 0;
}
