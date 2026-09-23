// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_skeleton.cpp
/// @brief      vsomeip binding skeleton implementation

#include "com/vsomeip/vsomeip_bind_skeleton.h"
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>

namespace com
{
    namespace vsomeip_binding
    {

        VsomeipBindSkeleton::VsomeipBindSkeleton(
            std::shared_ptr<vsomeip_v3::application> vsomeipApp,
            skeleton::Skeleton &skeleton,
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId) noexcept
            : vsomeipApp_(vsomeipApp), skeleton_(skeleton), serviceId_(serviceId), instanceId_(instanceId)
        {
        }

        VsomeipBindSkeleton::~VsomeipBindSkeleton() noexcept
        {
            if (offered_)
            {
                StopOffer();
            }
        }

        Result<void> VsomeipBindSkeleton::Init() noexcept
        {
            if (!vsomeipApp_)
            {
                return Result<void>(false, "Invalid vsomeip application");
            }

            // Request service for offering
            vsomeipApp_->request_service(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_));

            return Result<void>(true);
        }

        Result<void> VsomeipBindSkeleton::Deinit() noexcept
        {
            if (offered_)
            {
                StopOffer();
            }

            if (vsomeipApp_)
            {
                vsomeipApp_->release_service(
                    static_cast<vsomeip_v3::service_t>(serviceId_),
                    static_cast<vsomeip_v3::instance_t>(instanceId_));
            }

            return Result<void>(true);
        }

        Result<void> VsomeipBindSkeleton::Offer() noexcept
        {
            if (!vsomeipApp_)
            {
                return Result<void>(false, "Invalid vsomeip application");
            }

            if (offered_)
            {
                return Result<void>(true); // Already offered
            }

            // Offer the service
            vsomeipApp_->offer_service(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_));

            offered_ = true;
            return Result<void>(true);
        }

        Result<void> VsomeipBindSkeleton::StopOffer() noexcept
        {
            if (!vsomeipApp_ || !offered_)
            {
                return Result<void>(true); // Not offered
            }

            // Stop offering the service
            vsomeipApp_->stop_offer_service(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_));

            offered_ = false;
            return Result<void>(true);
        }

        char const *VsomeipBindSkeleton::GetBindRuntimeName() const noexcept
        {
            return "vsomeip";
        }

        void VsomeipBindSkeleton::RegisterMethodHandler(
            MethodIdentifier methodId,
            std::function<void(std::vector<uint8_t> const &, std::vector<uint8_t> &)> handler) noexcept
        {
            if (!vsomeipApp_ || !handler)
            {
                return;
            }

            // Register message handler for incoming requests
            vsomeipApp_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId_),
                static_cast<vsomeip_v3::instance_t>(instanceId_),
                static_cast<vsomeip_v3::method_t>(methodId),
                [this, methodId, handler](std::shared_ptr<vsomeip_v3::message> const &request)
                {
                    if (!request || request->get_message_type() != vsomeip_v3::message_type_e::MT_REQUEST)
                    {
                        return;
                    }

                    // Extract request data
                    std::vector<uint8_t> requestData;
                    auto payload = request->get_payload();
                    if (payload)
                    {
                        requestData = payload->get_data();
                    }

                    // Call handler
                    std::vector<uint8_t> responseData;
                    handler(requestData, responseData);

                    // Create and send response
                    auto response = vsomeipApp_->get_runtime()->create_response(request);
                    if (response)
                    {
                        auto responsePayload = vsomeipApp_->get_runtime()->create_payload(responseData);
                        if (responsePayload)
                        {
                            response->set_payload(responsePayload);
                        }
                        vsomeipApp_->send(response);
                    }
                });
        }

        void VsomeipBindSkeleton::SendEvent(EventIdentifier eventId, std::vector<uint8_t> const &eventData) noexcept
        {
            if (!vsomeipApp_ || !offered_)
            {
                return;
            }

            // Create notification message
            auto notification = vsomeipApp_->get_runtime()->create_notification();
            if (!notification)
            {
                return;
            }

            notification->set_service(static_cast<vsomeip_v3::service_t>(serviceId_));
            notification->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId_));
            notification->set_method(static_cast<vsomeip_v3::event_t>(eventId));

            // Set payload
            auto payload = vsomeipApp_->get_runtime()->create_payload(eventData);
            if (payload)
            {
                notification->set_payload(payload);
            }

            // Send notification
            vsomeipApp_->send(notification);
        }

    } // namespace vsomeip_binding
} // namespace com