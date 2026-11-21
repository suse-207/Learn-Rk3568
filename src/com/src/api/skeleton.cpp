// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       skeleton.cpp
/// @brief      Skeleton 实现（pimpl）
/// @details    各绑定的 BindSkeleton 只存在于本文件，公共头看不到它们。

#include "com/api/skeleton.h"

#include "com/api/runtime.h"
#include "com/binding/bind_runtime.h"
#include "com/binding/bind_skeleton.h"

#include <mutex>
#include <vector>

namespace com
{
    namespace skeleton
    {

        /// @brief Skeleton 的内部状态
        class Skeleton::Impl
        {
        public:
            Result<void> Init(InstanceIdentifier const &instanceIdentifier) noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>::FromValue();
                }
                instanceIdentifier_ = instanceIdentifier;

                auto *runtime = Runtime::Get();
                if (!runtime)
                {
                    return Result<void>::FromError(ComErrc::kNotInitialized);
                }
                for (auto const &name : runtime->GetBindRuntimeNames())
                {
                    auto *bindRuntime = runtime->GetBindRuntime(name);
                    if (!bindRuntime)
                    {
                        continue;
                    }
                    std::vector<std::unique_ptr<BindSkeleton>> created;
                    bindRuntime->CreateBindSkeleton(*owner_, instanceIdentifier_, created);
                    for (auto &bs : created)
                    {
                        bindSkeletons_.push_back(std::move(bs));
                    }
                }
                initialized_ = true;
                return Result<void>::FromValue();
            }

            Result<void> Deinit() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        bs->Deinit();
                    }
                }
                bindSkeletons_.clear();
                initialized_ = false;
                return Result<void>::FromValue();
            }

            Result<void> Offer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        auto result = bs->Offer();
                        if (!result)
                        {
                            return result;
                        }
                    }
                }
                return Result<void>::FromValue();
            }

            Result<void> StopOffer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        bs->StopOffer();
                    }
                }
                return Result<void>::FromValue();
            }

            /// @brief 指回宿主 Skeleton（CreateBindSkeleton 需要 Skeleton&）
            Skeleton *owner_{nullptr};
            ServiceIdentifier serviceIdentifier_{0};
            InstanceIdentifier instanceIdentifier_{0};
            std::vector<std::unique_ptr<BindSkeleton>> bindSkeletons_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

        Skeleton::Skeleton() noexcept : impl_(std::make_unique<Impl>())
        {
            impl_->owner_ = this;
        }

        Skeleton::~Skeleton()
        {
            Deinit();
        }

        ServiceIdentifier const &Skeleton::GetServiceIdentifier() const noexcept
        {
            return impl_->serviceIdentifier_;
        }

        Result<void> Skeleton::Init(InstanceIdentifier const &instanceIdentifier) noexcept
        {
            return impl_->Init(instanceIdentifier);
        }

        Result<void> Skeleton::Deinit() noexcept
        {
            if (!impl_)
            {
                return Result<void>::FromValue();
            }
            return impl_->Deinit();
        }

        Result<void> Skeleton::Offer() noexcept
        {
            return impl_->Offer();
        }

        Result<void> Skeleton::StopOffer() noexcept
        {
            return impl_->StopOffer();
        }

        void Skeleton::SetServiceIdentifier(ServiceIdentifier serviceIdentifier) noexcept
        {
            impl_->serviceIdentifier_ = serviceIdentifier;
        }

        void Skeleton::SetInstanceIdentifier(InstanceIdentifier instanceIdentifier) noexcept
        {
            impl_->instanceIdentifier_ = instanceIdentifier;
        }

    } // namespace skeleton
} // namespace com
