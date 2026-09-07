#pragma once

#include <unordered_map>

#include "uds/uds_types.h"

namespace uds {

class SessionManager {
public:
    Session get(int fd) const;
    void set(int fd, Session session);
    void erase(int fd);

private:
    std::unordered_map<int, Session> sessions_;
};

}  // namespace uds
