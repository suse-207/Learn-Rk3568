#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "isoft/uds/channel.h"
#include "isoft/uds/result.h"

namespace doip {

class DoipServer;

class DoipChannel final : public isoft::uds::server::Channel {
public:
    DoipChannel(DoipServer* server,
                int fd,
                std::uint16_t sa,
                std::string local_ip,
                std::uint16_t local_port,
                std::string peer_ip,
                std::uint16_t peer_port);

    ~DoipChannel() override = default;

    isoft::uds::Result<bool> Respond(
        std::shared_ptr<isoft::uds::server::Message>& response) override;

    isoft::uds::Result<void> Respond(
        std::vector<std::shared_ptr<isoft::uds::server::Message>>& responses) override;

    bool ReestablishAfterRestarted(std::uint16_t ta) override;
    isoft::uds::server::ChannelIdentifier GetIdentifier() override;
    std::size_t GetMaxPayloadLength() override;
    std::string GetLocalIp() override;
    std::uint16_t GetLocalPort() override;
    std::string GetRemoteIp() override;
    std::uint16_t GetRemotePort() override;

    void Close() noexcept { fd_ = -1; }

private:
    DoipServer* server_;
    int fd_;
    std::uint16_t sa_;
    std::string local_ip_;
    std::uint16_t local_port_;
    std::string peer_ip_;
    std::uint16_t peer_port_;
};

}  // namespace doip
