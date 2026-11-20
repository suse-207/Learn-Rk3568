// Regression test: concurrent requests on the same method.
//
// Before the session-matching fix, com::proxy::BindHandle::SendRequest registered
// its response handler keyed by (service, instance, method). Two overlapping calls
// on the same method therefore overwrote each other: one caller received the other
// caller's payload, and the loser waited until its timeout. A timed-out call also
// unregistered the handler unconditionally, so it could take the handler away from
// a second in-flight request.
//
// This program starts a service and then drives four scenarios against it:
//   seq    - two sequential calls on one method          (must both succeed)
//   diff   - two concurrent calls on different methods   (must both succeed)
//   same   - two concurrent calls on the same method     (must both succeed)
//   steal  - one call times out while another is in flight (only the first fails)
//
// The server answers method 0x0001 with the u32 id it received and method 0x0002
// with id + 1000, both after 300 ms.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "com/bind_runtime_factory.h"
#include "com/proxy.h"
#include "com/runtime.h"
#include "com/skeleton.h"

namespace {

constexpr std::uint16_t kService = 0x6100;
constexpr std::uint16_t kInstance = 0x0001;
constexpr std::uint16_t kMethodEcho = 0x0001;
constexpr std::uint16_t kMethodEchoOffset = 0x0002;
constexpr int kServerDelayMs = 300;

std::string g_scenario = "same";

void PutU32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

bool GetU32(std::vector<std::uint8_t> const &in, std::size_t offset, std::uint32_t &value) {
    if (in.size() < offset + 4) {
        return false;
    }
    value = (static_cast<std::uint32_t>(in[offset]) << 24) |
            (static_cast<std::uint32_t>(in[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(in[offset + 2]) << 8) |
            static_cast<std::uint32_t>(in[offset + 3]);
    return true;
}

long long Since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count();
}

void HandleEcho(std::vector<std::uint8_t> const &request, std::vector<std::uint8_t> &response,
                std::uint32_t offset, std::string const &tag,
                std::chrono::steady_clock::time_point start) {
    std::uint32_t id = 0;
    if (!GetU32(request, 0, id)) {
        id = 0xFFFFFFFFu;
    }
    std::cout << "[server] " << tag << " enter id=" << id << " t=" << Since(start) << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(kServerDelayMs));
    PutU32(response, id + offset);
    std::cout << "[server] " << tag << " leave id=" << id << " t=" << Since(start) << std::endl;
}

int RunServer() {
    auto start = std::chrono::steady_clock::now();

    auto *runtime = com::Runtime::Get();
    runtime->Init();
    auto created = com::CreateBindRuntime("vsomeip", "cc-server");
    if (!created || !runtime->RegisterBindRuntime(std::move(created))) {
        std::cout << "[server] init failed" << std::endl;
        return 1;
    }
    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (!bindRuntime) {
        std::cout << "[server] no bind runtime" << std::endl;
        return 1;
    }

    com::skeleton::ServiceSkeleton serviceSkeleton(kService, kInstance);
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(serviceSkeleton, kInstance, bindSkeletons);

    for (auto &bind : bindSkeletons) {
        bind->RegisterMethodHandler(
            kMethodEcho,
            [start](std::vector<std::uint8_t> const &request, std::vector<std::uint8_t> &response) {
                HandleEcho(request, response, 0u, "m1", start);
            });
        bind->RegisterMethodHandler(
            kMethodEchoOffset,
            [start](std::vector<std::uint8_t> const &request, std::vector<std::uint8_t> &response) {
                HandleEcho(request, response, 1000u, "m2", start);
            });
        bind->Offer();
    }
    std::cout << "[server] offered binds=" << bindSkeletons.size() << std::endl;

    std::thread stopper([runtime] {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        runtime->Stop();
    });
    runtime->Start();
    stopper.join();

    for (auto &bind : bindSkeletons) {
        bind->StopOffer();
    }
    runtime->Stop();
    runtime->Deinit();
    std::cout << "[server] stopped" << std::endl;
    return 0;
}

int RunClient() {
    auto start = std::chrono::steady_clock::now();

    auto *runtime = com::Runtime::Get();
    runtime->Init();
    auto created = com::CreateBindRuntime("vsomeip", "cc-client");
    if (!created || !runtime->RegisterBindRuntime(std::move(created))) {
        std::cout << "[client] init failed" << std::endl;
        return 1;
    }
    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (!bindRuntime) {
        std::cout << "[client] no bind runtime" << std::endl;
        return 1;
    }

    std::atomic<bool> started{false};
    com::FindServiceHandle findHandle{kService, kInstance};

    bindRuntime->RegisterFindServiceHandle(
        findHandle,
        [&started, runtime, start](com::ServiceHandleContainer<std::shared_ptr<com::proxy::BindHandle>> const &handles) {
            if (started.exchange(true)) {
                return;
            }
            auto copied = handles;
            std::thread([copied, runtime, start]() {
                for (auto const &handle : copied) {
                    if (!handle || !handle->IsValid()) {
                        std::cout << "[client] invalid handle" << std::endl;
                        continue;
                    }

                    std::atomic<int> ready{0};
                    auto run = [&](int callerId, std::uint16_t method, bool waitForPair, int timeoutMs) {
                        ready.fetch_add(1);
                        if (waitForPair) {
                            while (ready.load() < 2) {
                                std::this_thread::yield();
                            }
                        }

                        std::vector<std::uint8_t> request;
                        PutU32(request, static_cast<std::uint32_t>(callerId));

                        auto t0 = std::chrono::steady_clock::now();
                        auto result = handle->SendRequest(
                            method, request, std::chrono::milliseconds(timeoutMs));
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - t0).count();

                        std::uint32_t expected = static_cast<std::uint32_t>(callerId) +
                                                 (method == kMethodEchoOffset ? 1000u : 0u);
                        std::cout << "[client] " << g_scenario << " m=" << method
                                  << " id=" << callerId;
                        if (result) {
                            std::uint32_t echo = 0xBEEFu;
                            GetU32(result.Value(), 0, echo);
                            std::cout << " OK elapsed=" << elapsed << "ms echo=" << echo
                                      << (echo == expected ? " MATCH" : " MISMATCH")
                                      << std::endl;
                        } else {
                            std::cout << " FAIL elapsed=" << elapsed
                                      << "ms errc=" << result.Error().Value() << std::endl;
                        }
                    };

                    if (g_scenario == "seq") {
                        run(1, kMethodEcho, false, 3000);
                        run(2, kMethodEcho, false, 3000);
                    } else if (g_scenario == "diff") {
                        std::thread first(run, 1, kMethodEcho, true, 3000);
                        std::thread second(run, 2, kMethodEchoOffset, true, 3000);
                        first.join();
                        second.join();
                    } else if (g_scenario == "steal") {
                        // The first call gives up before the server answers; it must
                        // not take the second call's response handler with it.
                        std::thread first(run, 1, kMethodEcho, true, 200);
                        std::thread second(run, 2, kMethodEcho, true, 3000);
                        first.join();
                        second.join();
                    } else {
                        std::thread first(run, 1, kMethodEcho, true, 3000);
                        std::thread second(run, 2, kMethodEcho, true, 3000);
                        first.join();
                        second.join();
                    }
                }
                std::cout << "[client] done t="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start).count()
                          << "ms" << std::endl;
                runtime->Stop();
            }).detach();
        });

    std::thread stopper([runtime] {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        runtime->Stop();
    });
    runtime->Start();
    stopper.join();
    runtime->Stop();
    runtime->Deinit();
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    std::string mode = (argc > 1) ? argv[1] : "client";
    if (argc > 2) {
        g_scenario = argv[2];
    }
    return (mode == "server") ? RunServer() : RunClient();
}
