// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_runtime.cpp
/// @brief      vsomeip binding runtime implementation

#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/vsomeip/vsomeip_bind_handle.h"
#include "com/vsomeip/vsomeip_bind_skeleton.h"
#include "com/runtime.h"
#include <vsomeip/runtime.hpp>
#include <vsomeip/application.hpp>
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>

namespace com
{
    namespace vsomeip_binding
    {

        /// @brief Implementation class for VsomeipBindRuntime
        class VsomeipBindRuntime::Impl
        {
        public:
            explicit Impl(std::string const &applicationName) noexcept
                : applicationName_(applicationName)
            {
            }

            ~Impl() noexcept = default;

            Result<void> Init() noexcept
            {
                // Get vsomeip runtime singleton
                vsomeipRuntime_ = vsomeip_v3::runtime::get();
                if (!vsomeipRuntime_)
                {
                    return Result<void>(false, "Failed to get vsomeip runtime");
                }

                // Create vsomeip application
                vsomeipApp_ = vsomeipRuntime_->create_application(applicationName_);
                if (!vsomeipApp_)
                {
                    return Result<void>(false, "Failed to create vsomeip application");
                }

                // Initialize the application
                if (!vsomeipApp_->init())
                {
                    return Result<void>(false, "Failed to initialize vsomeip application");
                }

                initialized_ = true;
                return Result<void>(true);
            }

            Result<void> Deinit() noexcept
            {
                if (vsomeipApp_)
                {
                    vsomeipApp_->stop();
                    vsomeipApp_.reset();
                }
                vsomeipRuntime_.reset();
                initialized_ = false;
                return Result<void>(true);
            }

            void Start() noexcept
            {
                if (vsomeipApp_ && initialized_)
                {
                    vsomeipApp_->start();
                }
            }

            void Stop() noexcept
            {
                if (vsomeipApp_)
                {
                    vsomeipApp_->stop();
                }
            }

            std::shared_ptr<vsomeip_v3::runtime> vsomeipRuntime_;
            std::shared_ptr<vsomeip_v3::application> vsomeipApp_;
            std::string applicationName_;
            bool initialized_{false};
        };

        VsomeipBindRuntime::VsomeipBindRuntime(std::string const &applicationName) noexcept
            : impl_(std::make_unique<Impl>(applicationName))
        {
        }

        VsomeipBindRuntime::~VsomeipBindRuntime() noexcept = default;

        Result<void> VsomeipBindRuntime::Init() noexcept
        {
            return impl_->Init();
        }

        Result<void> VsomeipBindRuntime::Deinit() noexcept
        {
            return impl_->Deinit();
        }

        void VsomeipBindRuntime::Start() noexcept
        {
            impl_->Start();
        }

        void VsomeipBindRuntime::Stop() noexcept
        {
            impl_->Stop();
        }

        void VsomeipBindRuntime::CreateBindSkeleton(
            skeleton::Skeleton &skeleton,
            InstanceIdentifier const &instanceIdentifier,
            std::vector<std::unique_ptr<skeleton::BindSkeleton>> &bindSkeletons) noexcept
        {
            if (!impl_->vsomeipApp_)
            {
                return;
            }

            // Get service identifier from skeleton
            auto *serviceSkeleton = dynamic_cast<skeleton::ServiceSkeleton<void> *>(&skeleton);
            ServiceIdentifier serviceId = 0;
            if (serviceSkeleton)
            {
                serviceId = serviceSkeleton->GetServiceIdentifier();
            }

            // Create vsomeip bind skeleton
            auto bindSkeleton = std::make_unique<VsomeipBindSkeleton>(
                impl_->vsomeipApp_,
                skeleton,
                serviceId,
                instanceIdentifier);

            if (bindSkeleton->Init())
            {
                bindSkeletons.push_back(std::move(bindSkeleton));
            }
        }

        void VsomeipBindRuntime::GetAvailableServiceHandles(
            ServiceIdentifier const &serviceIdentifier,
            InstanceIdentifier const &instanceIdentifier,
            ServiceHandleContainer<std::shared_ptr<proxy::BindHandle>> &bindHandles) noexcept
        {
            if (!impl_->vsomeipApp_)
            {
                return;
            }

            // Check if service is available
            bool available = impl_->vsomeipApp_->is_available(
                static_cast<vsomeip_v3::service_t>(serviceIdentifier),
                static_cast<vsomeip_v3::instance_t>(instanceIdentifier));

            if (available)
            {
                // Create bind handle
                auto bindHandle = std::make_shared<VsomeipBindHandle>(
                    impl_->vsomeipApp_,
                    serviceIdentifier,
                    instanceIdentifier);

                if (bindHandle->IsValid())
                {
                    bindHandles.push_back(std::move(bindHandle));
                }
            }
        }

        void VsomeipBindRuntime::RegisterFindServiceHandle(
            FindServiceHandle const &findHandle,
            FindServiceHandler<std::shared_ptr<proxy::BindHandle>> const &findHandler) noexcept
        {
            // Register availability handler with vsomeip
            if (impl_->vsomeipApp_)
            {
                impl_->vsomeipApp_->register_availability_handler(
                    static_cast<vsomeip_v3::service_t>(findHandle.serviceIdentifier),
                    static_cast<vsomeip_v3::instance_t>(findHandle.instanceIdentifier),
                    [this, findHandler](vsomeip_v3::service_t service,
                                        vsomeip_v3::instance_t instance,
                                        bool isAvailable)
                    {
                        if (isAvailable)
                        {
                            // Create bind handle and notify
                            auto bindHandle = std::make_shared<VsomeipBindHandle>(
                                impl_->vsomeipApp_,
                                static_cast<ServiceIdentifier>(service),
                                static_cast<InstanceIdentifier>(instance));

                            if (bindHandle->IsValid() && findHandler)
                            {
                                ServiceHandleContainer<std::shared_ptr<proxy::BindHandle>> handles;
                                handles.push_back(std::move(bindHandle));
                                findHandler(handles);
                            }
                        }
                    });
            }
        }

        void VsomeipBindRuntime::UnregisterFindServiceHandle(FindServiceHandle const &findHandle) noexcept
        {
            if (impl_->vsomeipApp_)
            {
                impl_->vsomeipApp_->unregister_availability_handler(
                    static_cast<vsomeip_v3::service_t>(findHandle.serviceIdentifier),
                    static_cast<vsomeip_v3::instance_t>(findHandle.instanceIdentifier));
            }
        }

        char const *VsomeipBindRuntime::GetName() const noexcept
        {
            return "vsomeip";
        }

        std::shared_ptr<vsomeip_v3::runtime> VsomeipBindRuntime::GetVsomeipRuntime() const noexcept
        {
            return impl_->vsomeipRuntime_;
        }

        std::shared_ptr<vsomeip_v3::application> VsomeipBindRuntime::GetVsomeipApplication() const noexcept
        {
            return impl_->vsomeipApp_;
        }

    } // namespace vsomeip_binding
} // namespace com