// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_request_dispatcher.cpp
/// @brief      vsomeip 请求分发器实现

#include "com/vsomeip/vsomeip_request_dispatcher.h"

#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>
#include <vsomeip/runtime.hpp>

#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

namespace com
{
    namespace vsomeip_binding
    {

        namespace
        {
            /// @brief 墓碑上限：超过后整体清空，避免无界增长
            constexpr std::size_t kMaxAbandonedSessions = 1024;

            /// @brief 早到响应暂存上限（单个 session / 全局）
            constexpr std::size_t kMaxEarlyPerSession = 4;
            constexpr std::size_t kMaxEarlySessions = 64;

            using PromisePtr = std::shared_ptr<ara::core::Promise<std::vector<uint8_t>>>;
            using MethodKey = std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>;
        } // namespace

        class VsomeipRequestDispatcher::Impl
            : public std::enable_shared_from_this<VsomeipRequestDispatcher::Impl>
        {
        public:
            explicit Impl(std::shared_ptr<vsomeip_v3::application> vsomeipApp) noexcept
                : app_(std::move(vsomeipApp))
            {
            }

            ~Impl() noexcept
            {
                Stop();
            }

            /// @brief 一次提交的结果：token 为 0 表示提交失败（错误已写进 future）
            struct Submission
            {
                Future<std::vector<uint8_t>> future;
                std::uint32_t token{0};
            };

            Submission Submit(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept;

            /// @brief 放弃某个在途请求（超时）
            /// @return true 表示由本函数结束（调用方应返回 kTimeout）
            ///         false 表示响应已先行取走，调用方应等待 future 的自然结果
            bool Abandon(std::uint32_t token) noexcept;

            void Stop() noexcept;

        private:
            struct PendingCall
            {
                PromisePtr promise;
                std::uint32_t token{0};
                bool bound{false};
                vsomeip_v3::session_t session{0};
            };

            void EnsureHandlerLocked(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId) noexcept;

            void OnMessage(
                vsomeip_v3::session_t session,
                std::shared_ptr<vsomeip_v3::message> const &message) noexcept;

            void RememberAbandonedLocked(vsomeip_v3::session_t session) noexcept;

            void DropLocked(std::map<std::uint32_t, PendingCall>::iterator it) noexcept;

            std::shared_ptr<vsomeip_v3::application> app_;

            std::mutex mutex_;
            std::map<std::uint32_t, PendingCall> pending_;                  // token -> 在途请求
            std::map<vsomeip_v3::session_t, std::uint32_t> sessionToToken_;  // session -> token
            std::map<vsomeip_v3::session_t, std::vector<std::vector<uint8_t>>> early_;
            std::set<vsomeip_v3::session_t> abandoned_;
            std::set<MethodKey> registered_;
            std::uint32_t nextToken_{0};
            bool stopped_{false};
        };

        // ---------------------------------------------------------------- 内部工具

        void VsomeipRequestDispatcher::Impl::RememberAbandonedLocked(
            vsomeip_v3::session_t session) noexcept
        {
            abandoned_.insert(session);
            if (abandoned_.size() > kMaxAbandonedSessions)
            {
                // session 是 16 位、会回绕；宁可放弃迟到判断，也不允许无界增长
                abandoned_.clear();
                abandoned_.insert(session);
            }
        }

        void VsomeipRequestDispatcher::Impl::DropLocked(
            std::map<std::uint32_t, PendingCall>::iterator it) noexcept
        {
            if (it->second.bound)
            {
                sessionToToken_.erase(it->second.session);
                RememberAbandonedLocked(it->second.session);
            }
            pending_.erase(it);
        }

        void VsomeipRequestDispatcher::Impl::EnsureHandlerLocked(
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId,
            MethodIdentifier methodId) noexcept
        {
            MethodKey key{serviceId, instanceId, methodId};
            if (!registered_.insert(key).second)
            {
                return;
            }

            std::weak_ptr<Impl> weak = weak_from_this();
            app_->register_message_handler(
                static_cast<vsomeip_v3::service_t>(serviceId),
                static_cast<vsomeip_v3::instance_t>(instanceId),
                static_cast<vsomeip_v3::method_t>(methodId),
                [weak](std::shared_ptr<vsomeip_v3::message> const &message)
                {
                    auto self = weak.lock();
                    if (!self || !message)
                    {
                        return;
                    }
                    self->OnMessage(message->get_session(), message);
                });
        }

        // ---------------------------------------------------------------- 提交

        auto VsomeipRequestDispatcher::Impl::Submit(
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId,
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData) noexcept -> Submission
        {
            auto promise = std::make_shared<ara::core::Promise<std::vector<uint8_t>>>();
            Submission submission;
            submission.future = promise->get_future();

            if (!app_)
            {
                promise->SetError(MakeErrorCode(ComErrc::kNotInitialized));
                return submission;
            }

            // 阶段一：占位登记（锁内只登记，不 send）
            std::uint32_t token = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopped_)
                {
                    EnsureHandlerLocked(serviceId, instanceId, methodId);
                    token = ++nextToken_;
                    if (token == 0)
                    {
                        token = ++nextToken_;
                    }
                    PendingCall call;
                    call.promise = promise;
                    call.token = token;
                    pending_.emplace(token, std::move(call));
                    submission.token = token;
                }
            }

            if (token == 0)
            {
                promise->SetError(MakeErrorCode(ComErrc::kNotInitialized));
                return submission;
            }

            auto fail = [this, token, &promise, &submission](ComErrc errc)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = pending_.find(token);
                    if (it != pending_.end())
                    {
                        DropLocked(it);
                    }
                }
                promise->SetError(MakeErrorCode(errc));
                submission.token = 0;
            };

            // 阶段二：构造并发送（不持锁，避免发送被串行化、也避免与 io 线程抢锁）
            auto request = vsomeip_v3::runtime::get()->create_request();
            if (!request)
            {
                fail(ComErrc::kInternalError);
                return submission;
            }

            request->set_service(static_cast<vsomeip_v3::service_t>(serviceId));
            request->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId));
            request->set_method(static_cast<vsomeip_v3::method_t>(methodId));

            auto payload = vsomeip_v3::runtime::get()->create_payload();
            if (!payload)
            {
                fail(ComErrc::kInternalError);
                return submission;
            }

            payload->set_data(requestData);
            request->set_payload(payload);
            app_->send(request);

            // 阶段三：绑定真实 session，并检查 send 期间早到的响应
            std::vector<uint8_t> earlyPayload;
            bool completedFromEarly = false;
            bool lostEntry = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = pending_.find(token);
                if (it == pending_.end())
                {
                    // 不应发生（此时尚无 session 可匹配）；保守处理并结算错误
                    lostEntry = true;
                    submission.token = 0;
                }
                else
                {
                    const vsomeip_v3::session_t session = request->get_session();
                    it->second.bound = true;
                    it->second.session = session;
                    sessionToToken_[session] = token;

                    auto early = early_.find(session);
                    if (early != early_.end() && !early->second.empty())
                    {
                        earlyPayload = std::move(early->second.front());
                        early->second.erase(early->second.begin());
                        if (early->second.empty())
                        {
                            early_.erase(early);
                        }
                        DropLocked(it);
                        completedFromEarly = true;
                    }
                }
            }

            if (lostEntry)
            {
                promise->SetError(MakeErrorCode(ComErrc::kInternalError));
                return submission;
            }

            if (completedFromEarly)
            {
                promise->set_value(std::move(earlyPayload));
            }

            return submission;
        }

        // ---------------------------------------------------------------- 响应

        void VsomeipRequestDispatcher::Impl::OnMessage(
            vsomeip_v3::session_t session,
            std::shared_ptr<vsomeip_v3::message> const &message) noexcept
        {
            if (!message)
            {
                return;
            }

            PromisePtr promise;
            std::vector<uint8_t> payload;
            bool hasPayload = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (abandoned_.find(session) != abandoned_.end())
                {
                    return;  // 迟到响应，直接丢弃
                }

                auto index = sessionToToken_.find(session);
                if (index == sessionToToken_.end())
                {
                    // 早到的响应：先暂存，等 Submit 绑定 session 后交付
                    if (message->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                    {
                        auto respPayload = message->get_payload();
                        if (respPayload && early_.size() <= kMaxEarlySessions)
                        {
                            auto &slot = early_[session];
                            if (slot.size() < kMaxEarlyPerSession)
                            {
                                slot.emplace_back(
                                    respPayload->get_data(),
                                    respPayload->get_data() + respPayload->get_length());
                            }
                        }
                    }
                    return;
                }

                auto it = pending_.find(index->second);
                if (it == pending_.end())
                {
                    sessionToToken_.erase(index);
                    return;
                }

                promise = it->second.promise;
                if (message->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE)
                {
                    auto respPayload = message->get_payload();
                    if (respPayload)
                    {
                        payload.assign(respPayload->get_data(),
                                       respPayload->get_data() + respPayload->get_length());
                        hasPayload = true;
                    }
                }

                DropLocked(it);
            }

            // 锁外交付：Promise::SetResult 会同步执行用户续体
            if (hasPayload)
            {
                promise->set_value(std::move(payload));
            }
            else
            {
                promise->SetError(MakeErrorCode(ComErrc::kCommunicationError));
            }
        }

        // ---------------------------------------------------------------- 超时 / 停止

        bool VsomeipRequestDispatcher::Impl::Abandon(std::uint32_t token) noexcept
        {
            PromisePtr promise;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = pending_.find(token);
                if (it == pending_.end())
                {
                    return false;  // 响应已先行取走，让调用方等 future 的自然结果
                }
                promise = it->second.promise;
                DropLocked(it);
            }
            promise->SetError(MakeErrorCode(ComErrc::kTimeout));
            return true;
        }

        void VsomeipRequestDispatcher::Impl::Stop() noexcept
        {
            std::map<std::uint32_t, PendingCall> drained;
            std::set<MethodKey> registered;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopped_)
                {
                    return;
                }
                stopped_ = true;
                drained.swap(pending_);
                registered.swap(registered_);
                sessionToToken_.clear();
                early_.clear();
            }

            for (auto &entry : drained)
            {
                if (entry.second.promise)
                {
                    entry.second.promise->SetError(MakeErrorCode(ComErrc::kCommunicationError));
                }
            }

            if (app_)
            {
                for (auto const &key : registered)
                {
                    app_->unregister_message_handler(
                        static_cast<vsomeip_v3::service_t>(std::get<0>(key)),
                        static_cast<vsomeip_v3::instance_t>(std::get<1>(key)),
                        static_cast<vsomeip_v3::method_t>(std::get<2>(key)));
                }
            }
        }

        // ---------------------------------------------------------------- 对外接口

        VsomeipRequestDispatcher::VsomeipRequestDispatcher(
            std::shared_ptr<vsomeip_v3::application> vsomeipApp) noexcept
            : impl_(std::make_shared<Impl>(std::move(vsomeipApp)))
        {
        }

        VsomeipRequestDispatcher::~VsomeipRequestDispatcher() noexcept
        {
            if (impl_)
            {
                impl_->Stop();
            }
        }

        Result<std::vector<uint8_t>> VsomeipRequestDispatcher::Send(
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId,
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData,
            Duration timeout) noexcept
        {
            using ResponseResult = Result<std::vector<uint8_t>>;

            auto submission = impl_->Submit(serviceId, instanceId, methodId, requestData);
            if (submission.token == 0)
            {
                return submission.future.GetResult();
            }

            if (submission.future.wait_for(timeout) == ara::core::future_status::ready)
            {
                return submission.future.GetResult();
            }

            if (impl_->Abandon(submission.token))
            {
                return ResponseResult::FromError(ComErrc::kTimeout);
            }

            // 响应刚好在超时判定与放弃之间到达：等它 settle 后取真实结果
            submission.future.wait();
            return submission.future.GetResult();
        }

        Future<std::vector<uint8_t>> VsomeipRequestDispatcher::SendAsync(
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId,
            MethodIdentifier methodId,
            std::vector<uint8_t> const &requestData) noexcept
        {
            return impl_->Submit(serviceId, instanceId, methodId, requestData).future;
        }

        void VsomeipRequestDispatcher::Stop() noexcept
        {
            if (impl_)
            {
                impl_->Stop();
            }
        }

    } // namespace vsomeip_binding
} // namespace com
