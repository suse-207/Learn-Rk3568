#include "uds/session_manager.h"

namespace uds {

Session SessionManager::get(int fd) const {
    const auto it = sessions_.find(fd);
    return it == sessions_.end() ? Session::kDefault : it->second;
}

void SessionManager::set(int fd, Session session) {
    sessions_[fd] = session;
}

void SessionManager::erase(int fd) {
    sessions_.erase(fd);
}

}  // namespace uds
