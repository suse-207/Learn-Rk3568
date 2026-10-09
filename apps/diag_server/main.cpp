#include <csignal>
#include <cstring>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "common/config/config.h"
#include "common/log/log.h"
#include "diag/capi_did_adapter.h"
#include "diag/capi_download_adapter.h"
#include "diag/capi_routine_adapter.h"
#include "doip/doip_server.h"
#include "isoft/uds/data_management/diagnostic_data_management.h"
#include "isoft/uds/routine_management/routine_management.h"
#include "isoft/uds/server.h"
#include "isoft/uds/session_management/session_management.h"
#include "isoft/uds/transfer_managment/transfer_managment.h"
#include "net/epoll/epoll_loop.h"
#include "net/tcp/tcp_server.h"
#include "ota/boot_controller.h"
#include "ota/ota_manager.h"
#include "ota/slot_manager.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

namespace {

using isoft::uds::server::DiagnosticDataManagement;
using isoft::uds::server::RoutineManagement;
using isoft::uds::server::Server;
using isoft::uds::server::ServerSetting;
using isoft::uds::server::SessionMangement;
using isoft::uds::server::TransferManagment;

std::atomic<bool> g_running{true};

class DiagApp {
public:
    bool Initialize(const PlatformConfig& cfg) {
        cfg_ = cfg;

        const std::string ota_base = "/tmp/rk3568-diag-ota";
        slots_ = std::make_unique<ota::MockSlotManager>(ota_base, "A");
        boot_ = std::make_unique<ota::MockBootController>(ota_base + "/boot");
        versions_ = std::make_unique<ota::VersionManager>(ota_base + "/version");
        verify_ = ota::make_verify_strategy("sha256");
        ota_ = std::make_unique<ota::OtaManager>(*verify_, *slots_, *boot_, *versions_,
                                                 ota_base + "/staging");

        download_adapter_ = std::make_shared<diag::CapiDownloadAdapter>(cfg_.download_dir);
        did_adapter_ = std::make_shared<diag::CapiDidAdapter>();
        routine_adapter_ = std::make_shared<diag::CapiRoutineAdapter>(ota_.get(), cfg_.download_dir);

        udsServer_ = std::make_shared<Server>();
        if (!InitializeUds()) {
            std::cerr << "initialize CAPI UDS failed\n";
            return false;
        }
        if (!udsServer_->Start()) {
            std::cerr << "start CAPI UDS failed\n";
            return false;
        }

        doip_.set_identity(BuildIdentity());
        doip_.set_functional_addresses(
            std::unordered_set<std::uint16_t>(cfg_.functional_addresses.begin(),
                                              cfg_.functional_addresses.end()));
        doip_.set_uds_server(udsServer_);

        if (!loop_.init(cfg_.max_events)) {
            std::cerr << "epoll init failed\n";
            return false;
        }
        if (!tcp_.start(cfg_.listen_ip, cfg_.listen_port, cfg_.backlog, loop_)) {
            std::cerr << "tcp server start failed\n";
            return false;
        }
        tcp_.set_data_handler([this](int fd, const char* data, std::size_t len) {
            doip_.on_data(fd, data, len);
        });
        tcp_.set_disconnect_handler([this](int fd) { doip_.erase_client(fd); });
        return true;
    }

    bool Run() {
        if (!doip_.start_discovery(cfg_.listen_ip, cfg_.listen_port, loop_)) {
            std::cerr << "DoIP discovery start failed\n";
            return false;
        }

        ::signal(SIGINT, OnSignal);
        ::signal(SIGTERM, OnSignal);

        std::thread watcher([this]() {
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            loop_.stop();
        });

        std::cout << "[diag_server] listening on " << cfg_.listen_ip << ":"
                  << cfg_.listen_port << "\n";
        loop_.run();

        if (watcher.joinable()) {
            watcher.join();
        }

        doip_.stop_discovery();
        tcp_.stop();
        udsServer_->Stop();
        return true;
    }

    void Stop() {
        g_running = false;
    }

private:
    bool InitializeUds() {
        ServerSetting setting;
        setting.maxNumberOfRequestCorrectlyReceivedResponsePending = 0U;
        setting.maxParallelRequests = 4U;
        setting.physicalAddress = cfg_.logical_address;
        setting.functionAddressTable = cfg_.functional_addresses;
        if (!udsServer_->Initialize(setting)) {
            return false;
        }

        SessionMangement sessionConfig;
        sessionConfig.sessionConfigTable.insert(
            isoft::uds::server::SessionModel{"DefaultSession", 0x01U, 50U, 5000U});
        sessionConfig.sessionConfigTable.insert(
            isoft::uds::server::SessionModel{"ExtendedSession", 0x02U, 50U, 5000U});
        sessionConfig.sessionConfigTable.insert(
            isoft::uds::server::SessionModel{"ProgrammingSession", 0x03U, 50U, 5000U});
        sessionConfig.sessionControlInstanceTable.insert(
            isoft::uds::server::SessionControlInstanceConfig{0x01U, {}, {0x01U}, {}});
        sessionConfig.sessionControlInstanceTable.insert(
            isoft::uds::server::SessionControlInstanceConfig{0x02U, {}, {0x01U, 0x02U}, {}});
        sessionConfig.sessionControlInstanceTable.insert(
            isoft::uds::server::SessionControlInstanceConfig{0x03U, {}, {0x01U, 0x03U}, {}});
        if (!udsServer_->Initialize(sessionConfig)) {
            return false;
        }

        DiagnosticDataManagement dataConfig;
        isoft::uds::server::DiagnosticDataModel didModel;
        didModel.id = 0xF190U;
        didModel.nSize = 17U;
        didModel.readType = isoft::uds::server::DiagnosticDataReadMethod::kUseReadMothod;
        dataConfig.didManager.staticData.push_back(didModel);
        dataConfig.didManager.dataInterfacePtr = did_adapter_;

        isoft::uds::server::ServiceX22Model x22;
        isoft::uds::server::ReadDiagnosticDataByIdentifier readDid;
        readDid.id = 0xF190U;
        readDid.accessPermissionSession = {0x01U, 0x02U, 0x03U};
        readDid.accessPermissionEnvCondition = -1;
        x22.table.push_back(readDid);
        x22.maxDidToRead = 1U;
        x22.checkPerSourceId = false;
        dataConfig.service.serviceX22 = std::make_shared<isoft::uds::server::ServiceX22Model>(x22);

        isoft::uds::server::ServiceX2EModel x2e;
        isoft::uds::server::WriteDiagnosticDataByIdentifier writeDid;
        writeDid.id = 0xF190U;
        writeDid.dataSize = 17U;
        writeDid.accessPermissionSession = {0x01U, 0x02U, 0x03U};
        writeDid.accessPermissionEnvCondition = -1;
        x2e.table.push_back(writeDid);
        dataConfig.service.serviceX2E = std::make_shared<isoft::uds::server::ServiceX2EModel>(x2e);

        if (!udsServer_->Initialize(dataConfig)) {
            return false;
        }

        TransferManagment transferConfig;
        auto download = std::make_shared<isoft::uds::server::RequestDownload>();
        download->maxNumberOfBlockLength = cfg_.max_data_size;
        download->interfacePtr = download_adapter_;
        download->accessPermissionSession = {0x01U, 0x02U, 0x03U};
        download->accessPermissionEnvCondition = -1;
        download->p4ServerMax_0x36 = 5000U;
        download->p4ServerMax_0x37 = 5000U;
        transferConfig.requestDownload = std::move(download);
        if (!udsServer_->Initialize(transferConfig)) {
            return false;
        }

        RoutineManagement routineConfig;
        isoft::uds::server::RoutineControlInstanceConfig routine;
        routine.routine.id = 0x1100U;
        routine.routine.routineInfo = -1;
        routine.routine.startP4ServerMax = 10000U;
        routine.sessionPermission = {0x01U, 0x02U, 0x03U};
        routineConfig.routineInstanceTable.insert(routine);
        routineConfig.interfacePtr = routine_adapter_;
        if (!udsServer_->Initialize(routineConfig)) {
            return false;
        }

        return true;
    }

    doip::VehicleIdentity BuildIdentity() const {
        doip::VehicleIdentity identity{};
        const char* vin = "RK3568PLATFORM001";
        std::memcpy(identity.vin.data(), vin, 17);
        identity.logical_address = cfg_.logical_address;
        identity.eid = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
        identity.gid = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x01}};
        identity.node_type = 0x01;
        identity.max_open_sockets = cfg_.max_open_sockets;
        identity.max_data_size = cfg_.max_data_size;
        identity.power_mode = 0x01;
        identity.announcement_addr = cfg_.announcement_addr;
        identity.announcement_interval = std::chrono::milliseconds(cfg_.announcement_interval_ms);
        identity.initial_inactivity_timeout = std::chrono::milliseconds(cfg_.initial_inactivity_ms);
        identity.general_inactivity_timeout = std::chrono::milliseconds(cfg_.general_inactivity_ms);
        identity.alive_check_timeout = std::chrono::milliseconds(cfg_.alive_check_timeout_ms);
        return identity;
    }

    static void OnSignal(int) {
        g_running = false;
    }

    PlatformConfig cfg_;
    EpollLoop loop_;
    TcpServer tcp_;
    doip::DoipServer doip_;
    std::shared_ptr<Server> udsServer_;

    std::shared_ptr<diag::CapiDownloadAdapter> download_adapter_;
    std::shared_ptr<diag::CapiDidAdapter> did_adapter_;
    std::shared_ptr<diag::CapiRoutineAdapter> routine_adapter_;

    std::unique_ptr<ota::MockSlotManager> slots_;
    std::unique_ptr<ota::MockBootController> boot_;
    std::unique_ptr<ota::VersionManager> versions_;
    std::unique_ptr<ota::VerifyStrategy> verify_;
    std::unique_ptr<ota::OtaManager> ota_;
};

}  // namespace

int main(int argc, char** argv) {
    std::string cfg_path = "config/platform.json";
    if (argc > 1) {
        cfg_path = argv[1];
    }

    PlatformConfig cfg;
    if (!load_config(cfg_path, cfg)) {
        std::cerr << "load config failed: " << cfg_path << "\n";
        return 1;
    }

    DiagApp app;
    if (!app.Initialize(cfg)) {
        return 1;
    }
    if (!app.Run()) {
        return 1;
    }
    return 0;
}
