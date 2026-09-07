#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "uds/download_manager.h"
#include "uds/session_manager.h"
#include "uds/uds_types.h"

namespace uds {

class UdsDispatcher {
public:
    UdsDispatcher();

    void set_version(const std::string& version) { version_ = version; }
    void set_download_dir(const std::string& dir) { download_.set_dir(dir); }

    std::vector<std::uint8_t> Handle(int fd, const std::uint8_t* data, std::size_t len);
    void erase(int fd) {
        sessions_.erase(fd);
        download_.Abort(fd);
    }

private:
    using Handler = std::function<UdsResponse(int fd, const UdsRequest&)>;

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
};

}  // namespace uds
