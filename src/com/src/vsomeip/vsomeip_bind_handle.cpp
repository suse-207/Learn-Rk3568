// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_handle.cpp
/// @brief      vsomeip binding handle implementation

#include "com/vsomeip/vsomeip_bind_handle.h"
#include "com/vsomeip/vsomeip_request_dispatcher.h"
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>

namespace com
{
    namespace vsomeip_binding
    {

        VsomeipBindHandle::VsomeipBindHandle(
            std::shared_ptr<vsomeip_v3::application> vsomeipApp,
            std::shared_ptr<VsomeipRequestDispatcher> dispatcher,
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId) noexcept
            : vsomeipApp_(vsomeipApp),
              dispatcher_(std::move(dispatcher)),
              serviceId_(serviceId),
              instanceId_(instanceId)
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
            // 在途请求由 runtime 级 dispatcher 持有，句柄析构不需要清理；
            // dispatcher::Stop() 会统一结算并注销 handler。
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

            if (!dispatcher_)
            {
                return Result<std::vector<uint8_t>>::FromError(ComErrc::kNotInitialized);
            }

            // 请求状态、session 匹配与超时全部由 runtime 级 dispatcher 负责。
            return dispatcher_->Send(serviceId_, instanceId_, methodId, requestData, timeout);
        }

        Future<std::vector<uint8_t>> VsomeipBindHandle::SendRequestAsync(
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData) noexcept
        {
            if (!IsValid())
            {
                auto promise = std::make_shared<ara::core::Promise<std::vector<uint8_t>>>();
                auto future = promise->get_future();
                promise->SetError(MakeErrorCode(ComErrc::kInvalidHandle));
                return future;
            }

            if (!dispatcher_)
            {
                auto promise = std::make_shared<ara::core::Promise<std::vector<uint8_t>>>();
                auto future = promise->get_future();
                promise->SetError(MakeErrorCode(ComErrc::kNotInitialized));
                return future;
            }

            return dispatcher_->SendAsync(serviceId_, instanceId_, methodId, requestData);
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
                static_cast<vsomeip_v3::eventgroup_t>(eventGroupId),
                static_cast<vsomeip_v3::major_version_t>(0),
                static_cast<vsomeip_v3::event_t>(eventId));
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
