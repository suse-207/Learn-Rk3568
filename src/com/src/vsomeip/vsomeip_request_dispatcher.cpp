// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_request_dispatcher.cpp
/// @brief      vsomeip 请求分发器实现
///
/// 并发模型（两方，**无库内线程**）：
///   - 调用线程：Submit（登记 → send → 绑定 session）、同步等待并在到期时摘下自己那条
///   - vsomeip io 线程：OnMessage（按 session 找请求、取出、锁外交付）
///
/// 所有可变状态由 mutex_ 保护；Promise 的结算一律在锁外完成
/// （Promise::SetResult 会同步执行用户续体，持锁结算会自死锁）。

#include "com/vsomeip/vsomeip_request_dispatcher.h"

#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>
#include <vsomeip/runtime.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace com
{
    namespace vsomeip_binding
    {

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

            /// @brief 提交结果：token 为空表示立即失败（错误已写进 future）
            struct Submission
            {
                Future<std::vector<uint8_t>> future;
                std::optional<std::uint32_t> token;
            };

            /// @brief 提交一个请求；返回的 future 一定会在响应或停止时结算
            Submission Submit(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept;

            /// @brief 同步调用方到期时摘下自己那条（成功摘下返回 true 并记统计）
            bool Drop(std::uint32_t token) noexcept;

            void Stop() noexcept;

            Stats GetStats() const noexcept;

        private:
            // ---- 策略参数（文件级策略，收在 Impl 内）
            static constexpr std::size_t kMaxAbandonedSessions = 1024;  ///< 墓碑上限，超过整体清空
            static constexpr std::size_t kMaxEarlyPerSession = 4;       ///< 早到响应暂存上限（单 session）
            static constexpr std::size_t kMaxEarlySessions = 64;        ///< 早到响应暂存上限（全局）

            using PromisePtr = std::shared_ptr<ara::core::Promise<std::vector<uint8_t>>>;
            using MethodKey = std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>;
            using Session = vsomeip_v3::session_t;

            /// @brief 一条在途请求（唯一权威记录）
            struct PendingCall
            {
                PromisePtr promise;
                std::optional<Session> session;  ///< send 之后才有值
            };

            void EnsureHandlerLocked(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId) noexcept;

            void OnMessage(Session session, std::shared_ptr<vsomeip_v3::message> const &message) noexcept;

            /// @brief 唯一删除入口：同时清理 calls_ 与 bySession_
            void DropLocked(std::uint32_t token) noexcept;

            void RememberAbandonedLocked(Session session) noexcept;

            std::uint32_t NextTokenLocked() noexcept;

            std::shared_ptr<vsomeip_v3::application> app_;

            mutable std::mutex mutex_;
            std::map<std::uint32_t, PendingCall> calls_;                  ///< token -> 在途请求（权威）
            std::map<Session, std::uint32_t> bySession_;                  ///< session -> token（派生索引）
            std::map<Session, std::vector<std::vector<uint8_t>>> early_;  ///< send 与绑定之间到达的响应
            std::set<Session> abandoned_;                                 ///< 墓碑：已完成/已放弃的 session
            std::set<MethodKey> registered_;                              ///< 已注册稳定 handler 的方法

            std::uint32_t nextToken_{0};
            bool stopped_{false};

            std::atomic<std::uint64_t> statSubmitted_{0};
            std::atomic<std::uint64_t> statTimedOut_{0};
            std::atomic<std::uint64_t> statDroppedLate_{0};
            std::atomic<std::uint64_t> statDroppedEarly_{0};
        };

        // ---------------------------------------------------------------- 内部工具

        std::uint32_t VsomeipRequestDispatcher::Impl::NextTokenLocked() noexcept
        {
            ++nextToken_;
            if (nextToken_ == 0)
            {
                ++nextToken_;
            }
            return nextToken_;
        }

        void VsomeipRequestDispatcher::Impl::RememberAbandonedLocked(Session session) noexcept
        {
            abandoned_.insert(session);
            if (abandoned_.size() > kMaxAbandonedSessions)
            {
                // session 是 16 位、会回绕；宁可放弃迟到判断，也不允许无界增长
                abandoned_.clear();
                abandoned_.insert(session);
            }
        }

        void VsomeipRequestDispatcher::Impl::DropLocked(std::uint32_t token) noexcept
        {
            auto it = calls_.find(token);
            if (it == calls_.end())
            {
                return;
            }
            if (it->second.session.has_value())
            {
                bySession_.erase(*it->second.session);
                RememberAbandonedLocked(*it->second.session);
            }
            calls_.erase(it);
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

            // 阶段一：锁内登记（不 send、不结算）
            std::uint32_t token = 0;
            std::optional<ComErrc> registerError;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopped_ || !app_)
                {
                    registerError = ComErrc::kNotInitialized;
                }
                else
                {
                    EnsureHandlerLocked(serviceId, instanceId, methodId);
                    token = NextTokenLocked();
                    calls_.emplace(token, PendingCall{promise, std::nullopt});
                    statSubmitted_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (registerError.has_value())
            {
                promise->SetError(MakeErrorCode(*registerError));
                return submission;
            }
            submission.token = token;

            // 阶段二：构造并发送（不持锁）
            auto dropAndSettle = [this, token, &promise](ComErrc errc)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    DropLocked(token);
                }
                promise->SetError(MakeErrorCode(errc));
            };

            auto request = vsomeip_v3::runtime::get()->create_request();
            if (!request)
            {
                dropAndSettle(ComErrc::kInternalError);
                return submission;
            }
            request->set_service(static_cast<vsomeip_v3::service_t>(serviceId));
            request->set_instance(static_cast<vsomeip_v3::instance_t>(instanceId));
            request->set_method(static_cast<vsomeip_v3::method_t>(methodId));

            auto payload = vsomeip_v3::runtime::get()->create_payload();
            if (!payload)
            {
                dropAndSettle(ComErrc::kInternalError);
                return submission;
            }
            payload->set_data(requestData);
            request->set_payload(payload);
            app_->send(request);

            // 阶段三：绑定 session，并处理 send 期间早到的响应
            std::vector<uint8_t> earlyPayload;
            bool completedFromEarly = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = calls_.find(token);
                if (it == calls_.end())
                {
                    // 已被别人结算（例如 Stop），不要覆盖
                    return submission;
                }

                const Session session = request->get_session();
                it->second.session = session;
                bySession_[session] = token;

                auto early = early_.find(session);
                if (early != early_.end() && !early->second.empty())
                {
                    earlyPayload = std::move(early->second.front());
                    early->second.erase(early->second.begin());
                    if (early->second.empty())
                    {
                        early_.erase(early);
                    }
                    DropLocked(token);
                    completedFromEarly = true;
                }
            }

            if (completedFromEarly)
            {
                promise->set_value(std::move(earlyPayload));
            }
            return submission;
        }

        // ---------------------------------------------------------------- 响应

        void VsomeipRequestDispatcher::Impl::OnMessage(
            Session session,
            std::shared_ptr<vsomeip_v3::message> const &message) noexcept
        {
            if (!message)
            {
                return;
            }

            PromisePtr promise;
            std::vector<uint8_t> payload;
            bool hasPayload = false;
            bool dropLate = false;
            bool dropEarly = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (abandoned_.find(session) != abandoned_.end())
                {
                    dropLate = true;
                }
                else
                {
                    auto index = bySession_.find(session);
                    if (index == bySession_.end())
                    {
                        // 早到的响应：send 已发出但 session 还未绑定，先暂存
                        auto respPayload = message->get_payload();
                        if (message->get_message_type() == vsomeip_v3::message_type_e::MT_RESPONSE &&
                            respPayload && early_.size() <= kMaxEarlySessions)
                        {
                            auto &slot = early_[session];
                            if (slot.size() < kMaxEarlyPerSession)
                            {
                                slot.emplace_back(respPayload->get_data(),
                                                  respPayload->get_data() + respPayload->get_length());
                            }
                            else
                            {
                                dropEarly = true;
                            }
                        }
                        else
                        {
                            dropEarly = true;
                        }
                    }
                    else
                    {
                        auto it = calls_.find(index->second);
                        if (it == calls_.end())
                        {
                            bySession_.erase(index);
                            dropEarly = true;
                        }
                        else
                        {
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
                            DropLocked(index->second);
                        }
                    }
                }
            }

            if (dropLate)
            {
                statDroppedLate_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (dropEarly)
            {
                statDroppedEarly_.fetch_add(1, std::memory_order_relaxed);
                return;
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

        bool VsomeipRequestDispatcher::Impl::Drop(std::uint32_t token) noexcept
        {
            PromisePtr promise;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = calls_.find(token);
                if (it == calls_.end())
                {
                    return false;  // 已被响应或 Stop 结算
                }
                promise = it->second.promise;
                DropLocked(token);
            }
            statTimedOut_.fetch_add(1, std::memory_order_relaxed);
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
                drained.swap(calls_);
                registered.swap(registered_);
                bySession_.clear();
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

        VsomeipRequestDispatcher::Stats VsomeipRequestDispatcher::Impl::GetStats() const noexcept
        {
            Stats stats;
            stats.submitted = statSubmitted_.load(std::memory_order_relaxed);
            stats.timedOut = statTimedOut_.load(std::memory_order_relaxed);
            stats.droppedLate = statDroppedLate_.load(std::memory_order_relaxed);
            stats.droppedEarly = statDroppedEarly_.load(std::memory_order_relaxed);

            std::lock_guard<std::mutex> lock(mutex_);
            stats.inFlight = calls_.size();
            return stats;
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
            auto submission = impl_->Submit(serviceId, instanceId, methodId, requestData);
            if (!submission.token.has_value())
            {
                return submission.future.GetResult();  // 立即失败，错误已写入 future
            }

            if (submission.future.wait_for(timeout) == ara::core::future_status::ready)
            {
                return submission.future.GetResult();
            }

            // 到期：由调用线程摘下自己那条（记墓碑），不覆盖别人已结算的结果
            if (impl_->Drop(*submission.token))
            {
                return Result<std::vector<uint8_t>>::FromError(ComErrc::kTimeout);
            }
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

        VsomeipRequestDispatcher::Stats VsomeipRequestDispatcher::GetStats() const noexcept
        {
            return impl_ ? impl_->GetStats() : Stats{};
        }

    } // namespace vsomeip_binding
} // namespace com
