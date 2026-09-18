#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "uds/download_manager.h"
#include "uds/session_manager.h"
#include "uds/uds_types.h"

namespace uds {

class UdsDispatcher {
public:
    using ResponseHandler = std::function<void(std::vector<std::uint8_t>)>;

    UdsDispatcher();
    ~UdsDispatcher();

    void set_version(const std::string& version) { version_ = version; }
    void set_download_dir(const std::string& dir) { download_.set_dir(dir); }

    void Submit(int fd, const std::uint8_t* data, std::size_t len, ResponseHandler handler);
    void erase(int fd) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        sessions_.erase(fd);
        download_.Abort(fd);
    }

private:
    struct Task {
        int fd = -1;
        std::vector<std::uint8_t> request;
        ResponseHandler handler;
    };

    using Handler = std::function<UdsResponse(int fd, const UdsRequest&)>;

    void worker_loop();
    std::vector<std::uint8_t> process(int fd, const std::uint8_t* data, std::size_t len);

    UdsResponse handle_session_control(int fd, const UdsRequest& req);
    UdsResponse handle_read_data(int fd, const UdsRequest& req);
    UdsResponse handle_write_data(int fd, const UdsRequest& req);
    UdsResponse handle_routine_control(int fd, const UdsRequest& req);
    UdsResponse handle_request_download(int fd, const UdsRequest& req);
    UdsResponse handle_transfer_data(int fd, const UdsRequest& req);
    UdsResponse handle_request_transfer_exit(int fd, const UdsRequest& req);

    std::unordered_map<std::uint8_t, Handler> handlers_;
    SessionManager sessions_;
    DownloadManager download_;
    std::string version_ = "1.0.0";

    std::deque<Task> tasks_;
    std::mutex task_mutex_;
    std::mutex state_mutex_;
    std::condition_variable task_cv_;
    std::vector<std::thread> workers_;
    std::atomic_bool running_{true};
};

}  // namespace uds
