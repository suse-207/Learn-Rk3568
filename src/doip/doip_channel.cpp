#include "doip/doip_channel.h"

#include "doip/doip_server.h"

namespace doip {

DoipChannel::DoipChannel(DoipServer* server,
                         int fd,
                         std::uint16_t sa,
                         std::string local_ip,
                         std::uint16_t local_port,
                         std::string peer_ip,
                         std::uint16_t peer_port)
    : server_(server),
      fd_(fd),
      sa_(sa),
      local_ip_(std::move(local_ip)),
      local_port_(local_port),
      peer_ip_(std::move(peer_ip)),
      peer_port_(peer_port) {}

isoft::uds::Result<bool> DoipChannel::Respond(
    std::shared_ptr<isoft::uds::server::Message>& response) {
    if (!response || fd_ < 0 || server_ == nullptr) {
        return isoft::uds::Result<bool>::FromValue(false);
    }

    const bool sent = server_->SendDiagMessage(fd_,
                                               response->GetSA(),
                                               response->GetTA(),
                                               response->GetBody());
    return isoft::uds::Result<bool>::FromValue(sent);
}

isoft::uds::Result<void> DoipChannel::Respond(
    std::vector<std::shared_ptr<isoft::uds::server::Message>>& responses) {
    for (auto& response : responses) {
        static_cast<void>(Respond(response));
    }
    return {};
}

bool DoipChannel::ReestablishAfterRestarted(std::uint16_t /*ta*/) {
    return true;
}

isoft::uds::server::ChannelIdentifier DoipChannel::GetIdentifier() {
    return static_cast<isoft::uds::server::ChannelIdentifier>(fd_);
}

std::size_t DoipChannel::GetMaxPayloadLength() {
    return 4096U;
}

std::string DoipChannel::GetLocalIp() {
    return local_ip_;
}

std::uint16_t DoipChannel::GetLocalPort() {
    return local_port_;
}

std::string DoipChannel::GetRemoteIp() {
    return peer_ip_;
}

std::uint16_t DoipChannel::GetRemotePort() {
    return peer_port_;
}

}  // namespace doip
