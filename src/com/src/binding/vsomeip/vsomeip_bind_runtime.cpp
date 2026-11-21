// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_runtime.cpp
/// @brief      vsomeip binding runtime implementation

#include "com/binding/vsomeip/vsomeip_bind_runtime.h"
#include "com/binding/vsomeip/vsomeip_bind_handle.h"
#include "com/binding/vsomeip/vsomeip_bind_skeleton.h"
#include "com/binding/vsomeip/vsomeip_request_dispatcher.h"
#include "com/api/skeleton.h"
#include "com/api/runtime.h"
#include <vsomeip/runtime.hpp>
#include <vsomeip/application.hpp>
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>
#include <atomic>

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
                if (initialized_)
                {
                    return Result<void>::FromValue();
                }

                // Get vsomeip runtime singleton
                vsomeipRuntime_ = vsomeip_v3::runtime::get();
                if (!vsomeipRuntime_)
                {
                    return Result<void>::FromError(ComErrc::kInternalError);
                }

                // Create vsomeip application
                vsomeipApp_ = vsomeipRuntime_->create_application(applicationName_);
                if (!vsomeipApp_)
                {
                    return Result<void>::FromError(ComErrc::kInternalError);
                }

                // Initialize the application
                if (!vsomeipApp_->init())
                {
                    return Result<void>::FromError(ComErrc::kNotInitialized);
                }

                // 在途请求表与 handler 注册都挂在 application 作用域上：
                // 每个 runtime 一个 dispatcher，所有 handle 共用。
                dispatcher_ = std::make_shared<VsomeipRequestDispatcher>(vsomeipApp_);

                initialized_ = true;
                return Result<void>::FromValue();
            }

            Result<void> Deinit() noexcept
            {
                if (dispatcher_)
                {
                    dispatcher_->Stop();
                    dispatcher_.reset();
                }
                if (vsomeipApp_)
                {
                    if (!stopCalled_.exchange(true))
                    {
                        vsomeipApp_->stop();
                    }
                    vsomeipApp_.reset();
                }
                vsomeipRuntime_.reset();
                initialized_ = false;
                return Result<void>::FromValue();
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
                // Stop 是幂等契约：vsomeip 的 endpoint 销毁时序对重复 stop 不友好
                // （TSan 可复现其 endpoint mutex 与 io 线程的竞争），这里保证只执行一次。
                if (stopCalled_.exchange(true))
                {
                    return;
                }
                if (dispatcher_)
                {
                    dispatcher_->Stop();
                }
                if (vsomeipApp_)
                {
                    vsomeipApp_->stop();
                }
            }

            std::shared_ptr<vsomeip_v3::runtime> vsomeipRuntime_;
            std::shared_ptr<vsomeip_v3::application> vsomeipApp_;
            std::shared_ptr<VsomeipRequestDispatcher> dispatcher_;
            std::string applicationName_;
            bool initialized_{false};
            std::atomic<bool> stopCalled_{false};
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
            ServiceIdentifier serviceId = skeleton.GetServiceIdentifier();

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
                    impl_->dispatcher_,
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
            // Request the service first; vsomeip only notifies availability
            // handlers for services the client has requested.
            if (impl_->vsomeipApp_)
            {
                impl_->vsomeipApp_->request_service(
                    static_cast<vsomeip_v3::service_t>(findHandle.serviceIdentifier),
                    static_cast<vsomeip_v3::instance_t>(findHandle.instanceIdentifier));
            }

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
                                impl_->dispatcher_,
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
