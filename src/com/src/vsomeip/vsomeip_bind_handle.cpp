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

            // Synchronous request with timeout
            std::mutex mtx;
            std::condition_variable cv;
            bool responseReceived = false;
            std::vector<uint8_t> responseData;
            bool success = false;

            // Register message handler for response
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [&](std::shared_ptr<vsomeip_v3::message> const &response)
                {
                    if (response && response->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                    {
                        auto respPayload = response->get_payload();
                        if (respPayload)
                        {
                            responseData.assign(respPayload->get_data(),
                                                respPayload->get_data() + respPayload->get_length());
                            success = true;
                        }
                    }
                    std::lock_guard<std::mutex> lock(mtx);
                    responseReceived = true;
                    cv.notify_one();
                });

            // Send request
            vsomeipApp_->send(request);

            // Wait for response with timeout
            {
                std::unique_lock<std::mutex> lock(mtx);
                auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
                if (cv.wait_for(lock, timeoutMs, [&]
                                { return responseReceived; }))
                {
                    // Response received
                }
                else
                {
                    // Timeout
                    vsomeipApp_->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(serviceId_),
                        static_cast<vsomeip_v3::instance_t>(instanceId_),
                        static_cast<vsomeip_v3::method_t>(methodId));
                    return Result<std::vector<uint8_t>>::FromError(ComErrc::kTimeout);
                }
            }

            // Unregister handler
            vsomeipApp_->unregister_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId));

            if (success)
            {
                return Result<std::vector<uint8_t>>::FromValue(responseData);
            }
            return Result<std::vector<uint8_t>>::FromError(ComErrc::kCommunicationError);
        }

        void VsomeipBindHandle::SendRequestAsync(
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData,
            std::function<void(Result<std::vector<uint8_t>>)> handler) noexcept
        {
            if (!IsValid() || !handler)
            {
                if (handler)
                {
                    handler(Result<std::vector<uint8_t>>::FromError(ComErrc::kInvalidHandle));
                }
                return;
            }

            // Create request message
            auto request = vsomeip_v3::runtime::get()->create_request();
            if (!request)
            {
                handler(Result<std::vector<uint8_t>>::FromError(ComErrc::kInternalError));
                return;
            }

            request->set_service(static_cast<vsomeip_v3::service_t>(serviceId_));
            request->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId_));
            request->set_method(static_cast<vsomeip_v3::method_t>(methodId));

            // Set payload
            auto payload = vsomeip_v3::runtime::get()->create_payload(requestData);
            if (payload)
            {
                request->set_payload(payload);
            }

            // Register one-shot handler for response
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [this, methodId, handler](std::shared_ptr<vsomeip_v3::message> const &response)
                {
                    // Unregister handler after first response
                    vsomeipApp_->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(serviceId_),
                        static_cast<vsomeip_v3::instance_t>(instanceId_),
                        static_cast<vsomeip_v3::method_t>(methodId));

                    if (response && response->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                    {
                        auto respPayload = response->get_payload();
                        if (respPayload)
                        {
                            handler(Result<std::vector<uint8_t>>::FromValue(
                                std::vector<uint8_t>(respPayload->get_data(),
                                                     respPayload->get_data() + respPayload->get_length())));
                            return;
                        }
                    }
                    handler(Result<std::vector<uint8_t>>::FromError(ComErrc::kCommunicationError));
                });

            // Send request
            vsomeipApp_->send(request);
        }

        ara::core::Future<std::vector<uint8_t>> VsomeipBindHandle::SendRequestAsync(
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData) noexcept
        {
            // Create Promise/Future pair (P2: ara::com-style async)
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

            // Register message handler for response that fulfills the Promise
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [promisePtr, methodId](std::shared_ptr<vsomeip_v3::message> const &response) mutable
                {
                    // Unregister handler after first response
                    // Note: In a real implementation, you'd need to track the app reference
                    // For now, we just fulfill the promise

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

            // Send request
            vsomeipApp_->send(request);

            return future;
        }

        void VsomeipBindHandle::SubscribeEvent(
            EventIdentifier eventGroupId,
            std::function<void(std::vector<uint8_t> const &)> handler) noexcept
        {
            if (!IsValid() || !handler)
            {
                return;
            }

            // Request the event
            vsomeipApp_->request_event(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventGroupId),
                {static_cast<vsomeip_v3::eventgroup_t>(eventGroupId)},
                vsomeip_v3::event_type_e::ET_EVENT);

            // Register notification handler
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventGroupId),
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

            // Subscribe to event group
            vsomeipApp_->subscribe(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::eventgroup_t>(eventGroupId));
        }

        void VsomeipBindHandle::UnsubscribeEvent(EventIdentifier eventGroupId) noexcept
        {
            if (!IsValid())
            {
                return;
            }

            // Unsubscribe from event group
            vsomeipApp_->unsubscribe(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::eventgroup_t>(eventGroupId));

            // Unregister handler
            vsomeipApp_->unregister_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventGroupId));

            // Release the event
            vsomeipApp_->release_event(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::event_t>(eventGroupId));
        }

    } // namespace vsomeip_binding
} // namespace com
