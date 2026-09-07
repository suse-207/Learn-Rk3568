#include <vsomeip/vsomeip.hpp>

#include <csignal>
#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <vector>

class SomeipService;

namespace {

constexpr vsomeip::service_t kServiceId = 0x1234;
constexpr vsomeip::instance_t kInstanceId = 0x5678;
constexpr vsomeip::method_t kMethodId = 0x0001;
constexpr vsomeip::event_t kEventId = 0x8778;
constexpr vsomeip::eventgroup_t kEventGroupId = 0x4465;

SomeipService* g_service = nullptr;

}  // namespace

class SomeipService {
public:
    explicit SomeipService(std::string version)
        : version_(std::move(version)),
          app_(vsomeip::runtime::get()->create_application("someip-service")) {}

    bool init() {
        if (!app_->init()) {
            std::cerr << "vsomeip application init failed\n";
            return false;
        }

        app_->register_state_handler(
            [this](vsomeip::state_type_e state) { on_state(state); });
        app_->register_message_handler(
            kServiceId, kInstanceId, kMethodId,
            [this](const std::shared_ptr<vsomeip::message>& request) {
                on_message(request);
            });
        app_->register_availability_handler(
            kServiceId, kInstanceId,
            [this](vsomeip::service_t service, vsomeip::instance_t instance,
                   bool available) {
                on_availability(service, instance, available);
            });
        return true;
    }

    void start() {
        app_->start();
    }

    void stop() {
        app_->stop_offer_service(kServiceId, kInstanceId);
        app_->clear_all_handler();
        app_->stop();
    }

private:
    void on_state(vsomeip::state_type_e state) {
        std::cout << "someip-service state=" << static_cast<int>(state) << "\n";
        if (state == vsomeip::state_type_e::ST_REGISTERED) {
            app_->offer_service(kServiceId, kInstanceId);

            std::set<vsomeip::eventgroup_t> groups{kEventGroupId};
            app_->offer_event(
                kServiceId, kInstanceId, kEventId, groups,
                vsomeip::event_type_e::ET_EVENT, std::chrono::milliseconds::zero(),
                false, true, nullptr, vsomeip::reliability_type_e::RT_RELIABLE);
        }
    }

    void on_availability(vsomeip::service_t, vsomeip::instance_t, bool available) {
        if (available) {
            notify_version();
        }
    }

    void on_message(const std::shared_ptr<vsomeip::message>& request) {
        std::cout << "someip-service recv method=0x"
                  << std::hex << request->get_method() << std::dec << "\n";

        auto response = vsomeip::runtime::get()->create_response(request);
        response->set_payload(make_payload(version_));
        app_->send(response);
    }

    void notify_version() {
        app_->notify(kServiceId, kInstanceId, kEventId, make_payload(version_));
    }

    std::shared_ptr<vsomeip::payload> make_payload(const std::string& text) const {
        auto payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> data(text.begin(), text.end());
        payload->set_data(data);
        return payload;
    }

    std::string version_;
    std::shared_ptr<vsomeip::application> app_;
};

void handle_signal(int) {
    if (g_service != nullptr) {
        g_service->stop();
    }
}

int main(int argc, char** argv) {
    std::string version = "1.0.0";
    if (argc > 1) {
        version = argv[1];
    }

    SomeipService service(version);
    g_service = &service;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!service.init()) {
        return 1;
    }

    service.start();
    return 0;
}
