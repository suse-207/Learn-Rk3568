#include <vsomeip/vsomeip.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

class SomeipClient;

namespace {

constexpr vsomeip::service_t kServiceId = 0x1234;
constexpr vsomeip::instance_t kInstanceId = 0x5678;
constexpr vsomeip::method_t kMethodId = 0x0001;

SomeipClient* g_client = nullptr;

}  // namespace

class SomeipClient {
public:
    SomeipClient()
        : app_(vsomeip::runtime::get()->create_application("someip-client")) {}

    bool init() {
        if (!app_->init()) {
            std::cerr << "vsomeip client init failed\n";
            return false;
        }

        app_->register_state_handler(
            [this](vsomeip::state_type_e state) { on_state(state); });
        app_->register_message_handler(
            kServiceId, kInstanceId, kMethodId,
            [this](const std::shared_ptr<vsomeip::message>& response) {
                on_message(response);
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
        app_->clear_all_handler();
        app_->release_service(kServiceId, kInstanceId);
        app_->stop();
    }

private:
    void on_state(vsomeip::state_type_e state) {
        if (state == vsomeip::state_type_e::ST_REGISTERED) {
            app_->request_service(kServiceId, kInstanceId);
        }
    }

    void on_availability(vsomeip::service_t, vsomeip::instance_t, bool available) {
        if (available && !sent_) {
            sent_ = true;
            send_request();
        }
    }

    void on_message(const std::shared_ptr<vsomeip::message>& response) {
        const auto payload = response->get_payload();
        if (payload != nullptr) {
            const auto data = payload->get_data();
            const std::size_t length = payload->get_length();
            std::cout << "someip-client response: "
                      << std::string(data, data + length) << "\n";
        }
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop();
        }).detach();
    }

    void send_request() {
        auto request = vsomeip::runtime::get()->create_request();
        request->set_service(kServiceId);
        request->set_instance(kInstanceId);
        request->set_method(kMethodId);
        request->set_interface_version(1);
        request->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        request->set_reliable(true);
        app_->send(request);
        std::cout << "someip-client sent get_version request\n";
    }

    std::shared_ptr<vsomeip::application> app_;
    std::atomic<bool> sent_{false};
};

void handle_signal(int) {
    if (g_client != nullptr) {
        g_client->stop();
    }
}

int main() {
    SomeipClient client;
    g_client = &client;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!client.init()) {
        return 1;
    }

    client.start();
    return 0;
}
