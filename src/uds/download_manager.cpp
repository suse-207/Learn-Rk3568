#include "uds/download_manager.h"

#include <utility>

#include "common/log/log.h"

namespace uds {

bool DownloadManager::Start(int fd, std::uint32_t size) {
    if (size == 0) {
        return false;
    }

    DownloadSession session;
    session.path = dir_ + "/ota_download.bin";
    session.expected_size = size;
    session.file.open(session.path, std::ios::binary | std::ios::trunc);
    if (!session.file.is_open()) {
        LOG_ERROR("open download file failed: %s", session.path.c_str());
        return false;
    }

    sessions_[fd] = std::move(session);
    LOG_INFO("fd=%d download start size=%u -> %s", fd, size, sessions_[fd].path.c_str());
    return true;
}

bool DownloadManager::WriteBlock(int fd, const std::uint8_t* data, std::size_t len,
                                 std::uint8_t counter) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) {
        return false;
    }
    auto& session = it->second;

    const std::uint8_t expected = static_cast<std::uint8_t>(session.last_counter + 1);
    if (counter != expected) {
        LOG_WARN("fd=%d wrong block counter: got %u expect %u", fd, counter, expected);
        return false;
    }

    session.file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    session.received += static_cast<std::uint32_t>(len);
    session.last_counter = counter;
    return true;
}

bool DownloadManager::Finish(int fd) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) {
        return false;
    }
    auto& session = it->second;
    session.file.flush();
    session.file.close();

    const bool ok = session.received == session.expected_size;
    LOG_INFO("fd=%d download finish received=%u expected=%u ok=%d",
             fd, session.received, session.expected_size, ok);
    sessions_.erase(it);
    return ok;
}

void DownloadManager::Abort(int fd) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) {
        return;
    }
    it->second.file.close();
    sessions_.erase(it);
}

const DownloadSession* DownloadManager::Get(int fd) const {
    const auto it = sessions_.find(fd);
    return it == sessions_.end() ? nullptr : &it->second;
}

}  // namespace uds
