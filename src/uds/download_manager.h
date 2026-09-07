#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>

namespace uds {

struct DownloadSession {
    std::string path;
    std::uint32_t expected_size = 0;
    std::uint32_t received = 0;
    std::uint8_t last_counter = 0;
    std::ofstream file;
};

class DownloadManager {
public:
    void set_dir(const std::string& dir) { dir_ = dir; }

    bool Start(int fd, std::uint32_t size);
    bool WriteBlock(int fd, const std::uint8_t* data, std::size_t len, std::uint8_t counter);
    bool Finish(int fd);
    void Abort(int fd);
    const DownloadSession* Get(int fd) const;

private:
    std::string dir_ = "/tmp";
    std::unordered_map<int, DownloadSession> sessions_;
};

}  // namespace uds
