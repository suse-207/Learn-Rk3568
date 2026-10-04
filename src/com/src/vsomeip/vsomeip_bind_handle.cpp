// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_handle.cpp
/// @brief      vsomeip binding handle implementation

#include "com/vsomeip/vsomeip_bind_handle.h"
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>
#include <chrono>
#include <mutex>
#include <condition_variable>

namespace com
{
    namespace vsomeip_binding
    {

        VsomeipBindHandle::VsomeipBindHandle(
            std::shared_ptr<vsomeip_v3::application> vsomeipApp,
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId) noexcept
            : vsomeipApp_(vsomeipApp), serviceId_(serviceId), instanceId_(instanceId)
        {
            if (vsomeipApp_)
            {
                // Request the service
                vsomeipApp_->request_service(
                    static_cast<vsomeip_v3::service_t>(serviceId_),
                    static_cast<vsomeip_v3::instance_t>(instanceId_));

                valid_ = true;
            }
        }

        VsomeipBindHandle::~VsomeipBindHandle() noexcept
        {
            if (vsomeipApp_ && valid_)
            {
                // Release the service
                vsomeipApp_->release_service(
                    static_cast<vsomeip_v3::service_t>(serviceId_),
                    static_cast<vsomeip_v3::instance_t>(instanceId_));
            }
        }

        bool VsomeipBindHandle::IsValid() const noexcept
        {
            return valid_ && vsomeipApp_ &&
                   vsomeipApp_->is_available(
                       static_cast<vsomeip_v3::service_t>(serviceId_),
                       static_cast<vsomeip_v3::instance_t>(instanceId_));
        }

        char const *VsomeipBindHandle::GetBindRuntimeName() const noexcept
        {
            return "vsomeip";
        }

        Result<std::vector<uint8_t>> VsomeipBindHandle::SendRequest(
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData,
            Duration timeout) noexcept
        {
            if (!IsValid())
            {
                return Result<std::vector<uint8_t>>::FromError(ComErrc::kInvalidHandle);
            }

            // Create request message
            auto request = vsomeip_v3::runtime::get()->create_request();
            if (!request)
            {
                return Result<std::vector<uint8_t>>::FromError(ComErrc::kInternalError);
            }

            request->set_service(static_cast<vsomeip_v3::service_t>(serviceId_));
            request->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId_));
            request->set_method(static_cast<vsomeip_v3::method_t>(methodId));

            // Set payload
            auto payload = vsomeip_v3::runtime::get()->create_payload();
            if (!payload)
            {
                return Result<std::vector<uint8_t>>::FromError(ComErrc::kInternalError);
            }
            payload->set_data(requestData);
            request->set_payload(payload);

            // Shared state so the handler stays valid even if we time out and return
            // before vsomeip invokes it.
            struct State
            {
                std::mutex mtx;
                std::condition_variable cv;
                bool done{false};
                bool success{false};
                std::vector<uint8_t> data;
            };
            auto state = std::make_shared<State>();

            // This vsomeip build has no per-request send callback, so the response is
            // received through a method-level handler. Unregister it before signaling
            // so a later response cannot invoke it again.
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [app = vsomeipApp_, serviceId = serviceId_, instanceId = instanceId_,
                 methodId, state](std::shared_ptr<vsomeip_v3::message> const &response)
                {
                    app->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(serviceId),
                        static_cast<vsomeip_v3::instance_t>(instanceId),
                        static_cast<vsomeip_v3::method_t>(methodId));

                    if (response && response->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                    {
                        auto respPayload = response->get_payload();
                        if (respPayload)
                        {
                            state->data.assign(respPayload->get_data(),
                                               respPayload->get_data() + respPayload->get_length());
                            state->success = true;
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->mtx);
                        state->done = true;
                    }
                    state->cv.notify_one();
                });

            vsomeipApp_->send(request);

            // Wait for the response with timeout.
            {
                std::unique_lock<std::mutex> lock(state->mtx);
                if (!state->cv.wait_for(lock, timeout, [&] { return state->done; }))
                {
                    vsomeipApp_->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(serviceId_),
                        static_cast<vsomeip_v3::instance_t>(instanceId_),
                        static_cast<vsomeip_v3::method_t>(methodId));
                    return Result<std::vector<uint8_t>>::FromError(ComErrc::kTimeout);
                }
            }

            if (state->success)
            {
                return Result<std::vector<uint8_t>>::FromValue(std::move(state->data));
            }
            return Result<std::vector<uint8_t>>::FromError(ComErrc::kCommunicationError);
        }

        Future<std::vector<uint8_t>> VsomeipBindHandle::SendRequestAsync(
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData) noexcept
        {
            // Promise/Future pair; the Promise is captured by value below, so it lives
            // exactly as long as the pending request and is settled once by vsomeip.
            auto promisePtr = std::make_shared<ara::core::Promise<std::vector<uint8_t>>>();
            auto future = promisePtr->get_future();

            if (!IsValid())
            {
                promisePtr->SetError(MakeErrorCode(ComErrc::kInvalidHandle));
                return future;
            }

            // Create request message
            auto request = vsomeip_v3::runtime::get()->create_request();
            if (!request)
            {
                promisePtr->SetError(MakeErrorCode(ComErrc::kInternalError));
                return future;
            }

            request->set_service(static_cast<vsomeip_v3::service_t>(serviceId_));
            request->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId_));
            request->set_method(static_cast<vsomeip_v3::method_t>(methodId));

            // Set payload
            auto payload = vsomeip_v3::runtime::get()->create_payload();
            if (!payload)
            {
                promisePtr->SetError(MakeErrorCode(ComErrc::kInternalError));
                return future;
            }
            payload->set_data(requestData);
            request->set_payload(payload);

            // This vsomeip build has no per-request send callback; receive the response
            // through a method-level handler and settle the Promise once.
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [app = vsomeipApp_, serviceId = serviceId_, instanceId = instanceId_,
                 methodId, promisePtr](std::shared_ptr<vsomeip_v3::message> const &response)
                {
                    app->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(serviceId),
                        static_cast<vsomeip_v3::instance_t>(instanceId),
                        static_cast<vsomeip_v3::method_t>(methodId));

                    if (response && response->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                    {
                        auto respPayload = response->get_payload();
                        if (respPayload)
                        {
                            promisePtr->set_value(
                                std::vector<uint8_t>(respPayload->get_data(),
                                                     respPayload->get_data() + respPayload->get_length()));
                            return;
                        }
                    }
                    promisePtr->SetError(MakeErrorCode(ComErrc::kCommunicationError));
                });

            vsomeipApp_->send(request);

            return future;
        }

        void VsomeipBindHandle::SubscribeEvent(
            EventIdentifier eventId,
            EventGroupIdentifier eventGroupId,
            std::function<void(std::vector<uint8_t> const &)> handler) noexcept
        {
            if (!IsValid() || !handler)
            {
                return;
            }

            // Request the event. The event id and its owning event group id are distinct.
            vsomeipApp_->request_event(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventId),
                {static_cast<vsomeip_v3::eventgroup_t>(eventGroupId)},
                vsomeip_v3::event_type_e::ET_EVENT);

            // Register the notification handler keyed by the event id.
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventId),
                [handler](std::shared_ptr<vsomeip_v3::message> const &notification)
                {
                    if (notification && notification->get_message_type() == vsomeip_v3::message_type_e::MT_NOTIFICATION)
                    {
                        auto payload = notification->get_payload();
                        if (payload)
                        {
                            handler(std::vector<uint8_t>(payload->get_data(),
                                                         payload->get_data() + payload->get_length()));
                        }
                    }
                });

            // Subscribe to the event group.
            vsomeipApp_->subscribe(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::eventgroup_t>(eventGroupId));
        }

        void VsomeipBindHandle::UnsubscribeEvent(
            EventIdentifier eventId,
            EventGroupIdentifier eventGroupId) noexcept
        {
            if (!IsValid())
            {
                return;
            }

            // Unsubscribe from the event group.
            vsomeipApp_->unsubscribe(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::eventgroup_t>(eventGroupId));

            // Unregister the notification handler keyed by the event id.
            vsomeipApp_->unregister_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventId));

            // Release the event.
            vsomeipApp_->release_event(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventId));
        }

    } // namespace vsomeip_binding
} // namespace com
