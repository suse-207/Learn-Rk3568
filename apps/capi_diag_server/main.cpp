#include <csignal>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "isoft/doip/config.h"
#include "isoft/doip/public_def.h"
#include "isoft/doip/public_message.h"
#include "isoft/doip/server.h"
#include "isoft/uds/channel.h"
#include "isoft/uds/common.h"
#include "isoft/uds/message.h"
#include "isoft/uds/result.h"
#include "isoft/uds/server.h"

namespace {

using isoft::doip::DiagMessage;
using isoft::doip::PayloadType;
using isoft::doip::PowerModeResponse;
using isoft::doip::ServerConfig;
using isoft::doip::VehicleIdResponse;
using isoft::uds::server::Address;
using isoft::uds::server::Channel;
using isoft::uds::server::Message;
using isoft::uds::server::SessionMangement;
using isoft::uds::server::Server;
using isoft::uds::server::ServerSetting;
using isoft::uds::server::SessionControlInstanceConfig;
using isoft::uds::server::SessionModel;
using isoft::uds::Result;

std::uint16_t const kServerLogicalAddress{0x0E80U};
std::uint16_t const kTesterLogicalAddress{0x0E00U};
char const kVin[]{"RK3568CAPIUDS0001"};

class DiagChannel final : public Channel
{
public:
    DiagChannel(isoft::doip::Server* doipServer,
                std::int32_t const fd,
                std::uint16_t const sa,
                std::string localIp,
                std::uint16_t const localPort,
                std::string peerIp,
                std::uint16_t const peerPort)
        : doipServer_{doipServer}
        , fd_{fd}
        , sa_{sa}
        , localIp_{std::move(localIp)}
        , localPort_{localPort}
        , peerIp_{std::move(peerIp)}
        , peerPort_{peerPort}
    {
    }

    ~DiagChannel() override = default;

    Result< bool > Respond(std::shared_ptr< Message >& response) override
    {
        if (!response || fd_ < 0) {
            return Result< bool >::FromValue(false);
        }
        DiagMessage diag;
        diag.sourceAddress_ = response->GetSA();
        diag.targetAddress_ = response->GetTA();
        diag.uds_           = response->GetBody();

        ara::core::Vector< std::uint8_t > body;
        diag.Serialize(body);
        bool const sent = doipServer_->SendDoIPMessage(fd_, PayloadType::kDiagMessage, body);
        return Result< bool >::FromValue(sent);
    }

    Result< void > Respond(std::vector< std::shared_ptr< Message > >& responses) override
    {
        for (auto& response : responses) {
            static_cast< void >(Respond(response));
        }
        return {};
    }

    bool ReestablishAfterRestarted(std::uint16_t const /*ta*/) override { return true; }
    isoft::uds::server::ChannelIdentifier GetIdentifier() override { return static_cast< std::uint64_t >(fd_); }
    size_t GetMaxPayloadLength() override { return 4096; }
    std::string GetLocalIp() override { return localIp_; }
    std::uint16_t GetLocalPort() override { return localPort_; }
    std::string GetRemoteIp() override { return peerIp_; }
    std::uint16_t GetRemotePort() override { return peerPort_; }

    void Close() noexcept { fd_ = -1; }

private:
    isoft::doip::Server* doipServer_;
    std::int32_t fd_;
    std::uint16_t sa_;
    std::string localIp_;
    std::uint16_t localPort_;
    std::string peerIp_;
    std::uint16_t peerPort_;
};

class DemoApp final
{
public:
    DemoApp()
        : udsServer_{std::make_shared< Server >()}
        , doipServer_{nullptr}
    {
    }

    bool Initialize()
    {
        if (!InitializeUds()) {
            std::cerr << "UDS initialize failed" << std::endl;
            return false;
        }

        ServerConfig config;
        config.logicAdress_                   = kServerLogicalAddress;
        config.serverIP_                      = "0.0.0.0";
        config.tcpPort_                       = 13400;
        config.udpPort_                       = 13400;
        config.doipProtocolVersion_           = 0x02U;
        config.vehicleAnnouncementCount       = 3U;
        config.vehicleAnnouncementInterval    = 500U;
        config.vehicleAnnouncementWait        = 500U;
        config.generalInactivityTimer         = 300U;
        config.initialInactivityTimer         = 2U;
        config.aliveCheckTimer                = 500U;
        config.testerMaxConnections           = 10U;
        config.requestMaxBytes                = 65535U;
        config.routingActivationSourceAdressCheckBegin = 0x0E00U;
        config.routingActivationSourceAdressCheckEnd   = 0x0FFFU;
        config.diagAddressRadius_.push_back({kServerLogicalAddress, kServerLogicalAddress});

        doipServer_ = std::make_unique< isoft::doip::Server >(
            config, [this](std::int32_t const fd, PayloadType const type,
                           ara::core::Vector< std::uint8_t > const& message) { OnDoipRequest(fd, type, message); });
        if (!doipServer_) {
            return false;
        }
        return true;
    }

    bool Start()
    {
        if (!udsServer_->Start()) {
            std::cerr << "UDS start failed" << std::endl;
            return false;
        }
        auto result = doipServer_->Start(
            [this](std::int32_t const fd, std::uint16_t const sa, ara::core::String const& localIp,
                   std::uint16_t const localPort, ara::core::String const& peerIp, std::uint16_t const peerPort,
                   bool const state) { OnConnection(fd, sa, localIp, localPort, peerIp, peerPort, state); });
        if (!result.HasValue()) {
            std::cerr << "DoIP start failed" << std::endl;
            return false;
        }

        // One initial broadcast burst so UDP discovery tools see the entity.
        isoft::doip::VehicleIdResponse announcement;
        FillVehicleId(announcement);
        ara::core::Vector< std::uint8_t > body;
        announcement.Serialize(body);
        doipServer_->BroadcastAnnouncement(body);
        return true;
    }

    void Stop()
    {
        if (doipServer_) {
            static_cast< void >(doipServer_->Stop());
        }
        udsServer_->Stop();
    }

private:
    bool InitializeUds()
    {
        ServerSetting setting;
        setting.maxNumberOfRequestCorrectlyReceivedResponsePending = 0U;
        setting.maxParallelRequests                                = 4U;
        setting.physicalAddress                                    = kServerLogicalAddress;
        if (!udsServer_->Initialize(setting)) {
            return false;
        }

        SessionMangement sessionConfig;
        sessionConfig.sessionConfigTable.insert(SessionModel{"DefaultSession", 0x01U, 50U, 5000U});
        sessionConfig.sessionConfigTable.insert(SessionModel{"ExtendedSession", 0x02U, 50U, 5000U});
        sessionConfig.sessionConfigTable.insert(SessionModel{"ProgrammingSession", 0x03U, 50U, 5000U});
        sessionConfig.sessionControlInstanceTable.insert(SessionControlInstanceConfig{0x01U, {}, {0x01U}, {}});
        sessionConfig.sessionControlInstanceTable.insert(
            SessionControlInstanceConfig{0x02U, {}, {0x01U, 0x02U}, {}});
        sessionConfig.sessionControlInstanceTable.insert(
            SessionControlInstanceConfig{0x03U, {}, {0x01U, 0x03U}, {}});
        if (!udsServer_->Initialize(sessionConfig)) {
            return false;
        }
        return true;
    }

    static void FillVehicleId(isoft::doip::VehicleIdResponse& response)
    {
        std::memset(response.vin_.data(), 0, response.vin_.size());
        std::memcpy(response.vin_.data(), kVin, 17);
        response.serverLogicalAddress_ = kServerLogicalAddress;
        response.eid_                  = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
        response.gid_                  = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
        response.furtherAction_        = 0x00U;
        response.vinGidStatus_         = 0x00U;
    }

    void OnConnection(std::int32_t const fd,
                      std::uint16_t const sa,
                      ara::core::String const& localIp,
                      std::uint16_t const localPort,
                      ara::core::String const& peerIp,
                      std::uint16_t const peerPort,
                      bool const state)
    {
        if (state) {
            auto channel = std::make_shared< DiagChannel >(
                doipServer_.get(), fd, sa, std::string{localIp.c_str()}, localPort, std::string{peerIp.c_str()},
                peerPort);
            channels_[fd] = std::move(channel);
            std::cout << "[diag] TCP activated fd=" << fd << " sa=0x" << std::hex << sa << std::dec
                      << " peer=" << peerIp.c_str() << ":" << peerPort << std::endl;
        } else {
            auto it = channels_.find(fd);
            if (it != channels_.end()) {
                static_cast< void >(it->second);
                it->second.reset();
            }
            channels_.erase(fd);
            std::cout << "[diag] TCP closed fd=" << fd << std::endl;
        }
    }

    void OnDoipRequest(std::int32_t const fd,
                       PayloadType const type,
                       ara::core::Vector< std::uint8_t > const& message)
    {
        switch (type) {
            case PayloadType::kVehicleIdRequest:
            case PayloadType::kVehicleIdRequestWithEid:
            case PayloadType::kVehicleIdRequestWithVin: {
                VehicleIdResponse response;
                FillVehicleId(response);
                ara::core::Vector< std::uint8_t > body;
                response.Serialize(body);
                doipServer_->SendDoIPMessage(fd, PayloadType::kVehicleAnnoucementIdResponse, body);
                return;
            }
            case PayloadType::kDiagPowerModeInfoRequest: {
                PowerModeResponse response;
                response.powerMode_ = isoft::doip::PowerMode::kReady;
                ara::core::Vector< std::uint8_t > body;
                response.Serialize(body);
                doipServer_->SendDoIPMessage(fd, PayloadType::kDiagPowerModeInfoResponse, body);
                return;
            }
            case PayloadType::kDiagMessage:
                break;
            default:
                return;
        }

        DiagMessage diag;
        if (!diag.Deserialize(message)) {
            return;
        }
        auto it = channels_.find(fd);
        if (it == channels_.end()) {
            return;
        }

        auto request       = std::make_shared< Message >(diag.sourceAddress_, diag.targetAddress_);
        request->SetTaType(diag.targetAddress_ == kServerLogicalAddress
                               ? isoft::uds::server::TargetAddressType::kPhysical
                               : isoft::uds::server::TargetAddressType::kFunctional);
        request->GetBody() = std::vector< std::uint8_t >(diag.uds_.begin(), diag.uds_.end());
        auto indicated = udsServer_->Indicate(request, 0U, it->second);
        if (!indicated.HasValue()) {
            std::cout << "[diag] UDS Indicate refused fd=" << fd << std::endl;
        }
        udsServer_->HandleMessage(request, 0U, it->second);
    }

private:
    std::shared_ptr< Server > udsServer_;
    std::unique_ptr< isoft::doip::Server > doipServer_;
    std::map< std::int32_t, std::shared_ptr< DiagChannel > > channels_;
};

std::unique_ptr< DemoApp > g_app;

void OnSignal(int) { g_app->Stop(); }

}  // namespace

int main()
{
    auto app = std::make_unique< DemoApp >();
    if (!app->Initialize() || !app->Start()) {
        return 1;
    }
    g_app = std::move(app);

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    std::cout << "[main] isoft DoIP+UDS server listening on 13400 (Ctrl-C to stop)" << std::endl;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(3600));
    }
    return 0;
}
