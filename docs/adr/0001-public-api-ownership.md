# ADR 0001：按 ara::com 标准重定 COM 公共 API

- 状态：**已接受**（2026-11-21，v2；评审时 4 项决策全部按推荐通过，见第 9 节）；尚未落地代码
- 日期：2026-11-21
- 关联：`docs/com-runtime-design.md` §1.5（目标架构与四条硬约束）、§7.2 第 15/16 项；`docs/IDL_CODEGEN_DESIGN.md`
- 代码基线：`main @ 9101a31`
- 参考实现（本机只读，作为标准落地的实物证据）：`~/capi`（ara::com 完整实现）、SWS 条目编号直接引用其注释中的 `@ref [SWS_CM_xxxxx]`

## 修订说明：v1 错在哪

v1 把"当前实现"当成了约束，于是得出了几个本末倒置的结论：不引入 pimpl（保持 header-inline）、把 `BindHandle` 保留在公共头只为省一次 include 变更、保留生成物里硬编码的 5s 超时、"超时归调用方"只做到语义层而不动绑定层签名、把目录拆分推给"另立 ADR"。

这些都不是标准的要求，是现状的惯性。v2 的判据只有一条：**ara::com 要求什么，我们就改成什么**；当前实现只是待改造对象，逐条列在第 3 节。

---

## 1. 判据：标准要求什么（可核对）

| 主题 | 标准条目 | 参考实现位置 |
| --- | --- | --- |
| 服务代理句柄是公共类型，绑定句柄在其内部 | `SWS_CM_00312`（HandleType Class）、`SWS_CM_00317/00318`（拷贝/移动语义） | `~/capi/com/com/src/com/include/ara/com/internal/proxy/proxy.h:154`（`class HandleType`）：只暴露 `GetInstanceId()`/`CreateBindProxy()`，`std::shared_ptr<BindHandle> bindHandle_` 是 private |
| 代理创建用命名构造，异常可控 | `SWS_CM_11326`（命名构造）、`SWS_CM_10438`（无异常创建）、`SWS_CM_00131` | `proxy.h:311`：`static Result<TProxy> Create(HandleType const&)`；`:233`：`explicit ServiceProxy(HandleType const&)`；`:334`：`GetHandle()` |
| 立即发现 + 异步发现 | `SWS_CM_00122`、`SWS_CM_00622`、`SWS_CM_00018`、`SWS_CM_00303`、`SWS_CM_00304` | `proxy.h:346`、`:379`：`static Result<ServiceHandleContainer<HandleType>> FindService(...)`（实例 ID 版 + InstanceSpecifier 版）；`ara/com/types.h:224`：`FindServiceHandler = std::function<void(ServiceHandleContainer<HandleType>, FindServiceHandle)>` |
| 方法返回 `Future<Output>`，**签名里没有超时** | `SWS_CM_...`（method 基类） | `internal/proxy/method.h:196`、`:314`：`ara::core::Future<Output> operator()(Args&&...) noexcept` |
| 事件是对象，订阅要申请样本上限 | `SWS_CM_00702`（GetNewSamples 回调签名）、`SWS_CM_00310`（SubscriptionState）、`SWS_CM_00311`（状态变化 handler）、`SWS_CM_00703/00706/11024` | `internal/proxy/event.h:103`（`Subscribe(size_t maxSampleCount)`）、`:105`（`Unsubscribe`）、`:108`（`GetSubscriptionState`）、`:112`（`SetReceiveHandler`）、`:135`（`GetNewSamples`）、`:139`（`GetFreeSampleCount`）；`types.h:351-381`：`SampleAllocateePtr`、`EventReceiveHandler`、`SubscriptionState{kSubscribed,kNotSubscribed,kSubscriptionPending}` |
| 骨架侧 Offer 语义与命名 | `SWS_CM_00010`（OfferService 可重入）、`SWS_CM_00011`（StopOfferService 可重入） | `internal/skeleton/skeleton.h:113`、`:115`：`OfferService()`、`StopOfferService()`、`ProcessNextMethodCall()`；`:131`：`class ServiceSkeleton : public Skeleton`；`:407`、`:432`：可重入实现 |
| 骨架事件发送 | `SWS_CM_00162`（应用持有数据）、`SWS_CM_90437`（CM 持有数据）、`SWS_CM_00012`（Send 可重入） | `internal/skeleton/event.h:106`、`:230`、`:342`：`Result<void> Send(SampleType const&)` |
| 错误码集合与错误域标识 | `SWS_CM_10432`（ComErrc 定义）、`SWS_CM_11264/11265`、`SWS_CM_11329` | `com_error_domain.h:53-78`：19 个具名码 + `kUnknownError=-1`；`:104`：kId `0x8000000000001267`；`:126`：Name `"ComError"` |
| 分层：公共类型 / internal api / binding 实现 / 代码生成 | —（结构事实） | `ara/com/types.h` + `com_error_domain.h`（公共）、`ara/com/internal/{proxy,skeleton}/`（api 实现）、`ara/com/internal/{nsomeip,npc,icc,dds}/`（绑定）、`ara/com/cg/`（通信组） |

**我们自带的 `ara::core` 已经够用**，不需要自造：`src/common/ara-core/include/ara/core/future.h:276` 有 `wait_for()` 返回 `future_status`、`:299` `wait_until()`、`:190` `GetResult()`、`:233` `get()`；`result.h:98-132` 有 `FromValue/FromError`。也就是说"方法返回 Future、超时由调用方 `wait_for`"在本仓库是可实现的，不是纸面方案。

## 2. 与标准的一处**刻意分歧**（必须写明，不是偷懒）

`~/capi` 自带线程池（`ara/com/internal/threadpool.h:48 class ThreadPool`），ara::com 的事件 `SetReceiveHandler` 与 `ProcessNextMethodCall` 依赖它。

本项目 §1.5 硬约束 1 明确禁止："**com 不拥有 loop/线程**"。因此事件 API 采用标准**语义**、去掉标准**线程模型**：

- 样本由绑定层 io 线程写入环形缓冲（不新建线程）；
- `SetReceiveHandler(handler)` 的回调**在绑定 io 线程执行**（不是 CM 的 handler 线程），文档与生成物注释必须写死这条红线；
- `GetNewSamples` 只能由应用自己的线程调用；应用在自己的线程里被 handler 唤醒后拉取。

这条分歧的影响是"handler 在哪个线程跑"，不是"事件能不能用"。硬约束 2/3（同步超时归调用方、异步不承诺超时）与标准完全一致，正好被 `Future` 形态满足。

---

## 3. 当前实现必须改的清单（19 项）

编号 C1–C18。每条给出：现状（证据）→ 标准要求 → 改成什么 → 影响面 → 风险。

### 结构性

**C1 目录与头文件没有分层**
- 现状：`src/com/include/com/` 平铺 7 个头（`proxy.h` 318 行、`skeleton.h` 219 行…），`proxy.h` 同一个文件里既定义绑定句柄 `BindHandle`（`:31`）又定义公共 `ServiceProxy`（`:100`）。
- 标准：公共类型、internal api、绑定实现、生成物四层分离（第 1 节末行）。
- 改成：`com/api/{types.h,com_error_domain.h,proxy.h,event.h,method.h,skeleton.h}` + `com/binding/{bind_runtime.h,bind_handle.h,bind_skeleton.h}` + `com/binding/vsomeip/**` + `com/gen/**`。
- 影响面：全部 include 路径（约 40 处）。
- 风险：中（纯机械，编译期即可查全）。

**C2 公共头暴露绑定类型**
- 现状：`proxy.h:31 class BindHandle`、`skeleton.h:28 class BindSkeleton` 都在公共头；`skeleton.h:168 GetBindSkeletons()` 直接返回 `std::vector<std::unique_ptr<BindSkeleton>>&`（实测全仓库仅声明处自身匹配，无调用者）。
- 标准：`HandleType`（`SWS_CM_00312`）包装绑定句柄，只暴露实例标识。
- 改成：`BindHandle`→`com::binding::IBindingHandle`、`BindSkeleton`→`com::binding::IBindingSkeleton`，移出 api 层；增加 `ServiceProxy<TProxy>::HandleType`，业务层只持有 `HandleType`；删除 `GetBindSkeletons()`。
- 影响面：`src/ucm`、6 个 app、idlc 生成物、`tests/com_concurrency`。
- 风险：中。

**C3 `ServiceProxy` 是 header-inline，无法隐藏内部**
- 现状：`proxy.h` 318 行全是 inline 定义（`Init` `:129`、`Deinit` `:205`、`WaitForAvailable` `:244`），内部 `DiscoveryState` 结构体（`:259`）也在公共头里。任何内部改动都触发全量重编，且 `BindHandle` 必须在这个头里可见。
- 标准：头里只有声明，实现在 `.cpp`（`~/capi/.../proxy/proxy.h` 全声明，`Proxy::Proxy` 等落 `.cpp`）。
- 改成：`pimpl`（`struct Impl; std::unique_ptr<Impl> impl_;`），声明与定义分离到 `src/com/src/api/proxy.cpp`。**这是 C2 能彻底做干净的前提**——pimpl 之后公共头连 `IBindingHandle` 的前置声明都不需要。
- 影响面：`proxy.h`、`skeleton.h`、新增两个 `.cpp`。
- 风险：低-中。v1 曾以"保持 header-only 模板风格"为由拒绝，这个理由不成立。

### 错误与类型

**C4 `ComErrc` 与标准完全不同**
- 现状：`src/com/include/com/com_error_domain.h:21-34`，11 个自造码（`kServiceNotFound=1`、`kTimeout=2`、`kSerializationFailed=3`… `kNotFound=11`），域 kId `0x00000000434F4D00`（`:57`）、Name `"Com"`（`:63`）。
- 标准：`SWS_CM_10432` 的 19 个码（`kServiceNotAvailable=1`、`kMaxSamplesReached=2`、`kNetworkBindingFailure=3`… `kCouldNotExecute=19`）+ `kUnknownError=-1`；域 kId `0x8000000000001267`、Name `"ComError"`、`SWS_CM_11329`。
- 改成：标准码按标准值落地；项目扩展码（`kTimeout`、`kInvalidHandle`、`kNotInitialized`…）放到 `0x1000` 起的独立区间并在注释标注"非标准扩展"。域名与 kId 对齐标准。
- 影响面：全部 `MakeErrorCode` / `errc` 比较处（UCM 17 处错误映射、绑定层、tests）。
- 风险：中（数值变化，靠编译期枚举名兜住）。

**C5 缺少 `Payload` 与事件相关类型**
- 现状：公共 API 一律裸 `std::vector<uint8_t>`；无 `SamplePtr`、`SubscriptionState`、`EventReceiveHandler`（实测 `grep -rn 'SamplePtr\|SubscriptionState' src/com src/common` 无命中）。
- 标准：`types.h:351-381`。
- 改成：`types.h` 增加 `Payload`、`SamplePtr<T>`、`SubscriptionState`、`EventReceiveHandler`、`SubscriptionStateChangeHandler`。
- 风险：低。

### 请求面

**C6 请求能力不在公共 API**
- 现状：`ServiceProxy` 只有 `Init/Deinit/IsAvailable/WaitForAvailable`；请求只有 `BindHandle::SendRequest`（`proxy.h:47`）。
- 标准：请求由生成代理的 `Method` 成员发起，`operator()` 返回 `Future<Output>`（`method.h:196/314`）。
- 改成：新增 `com/api/method.h` 的 `Method<Signature>`，生成代理持有 `Method<...>` 成员；`ServiceProxy<TProxy>::Create(HandleType)` 作为唯一构造入口。
- 影响面：idlc、6 个 app、UCM。
- 风险：高（生成器与业务同时动）。

**C7 超时参数下沉到绑定层签名**
- 现状：`BindHandle::SendRequest(methodId, data, Duration)`（`proxy.h:47`），实现转发给 `VsomeipRequestDispatcher::Send(..., Duration)`（`vsomeip_bind_handle.cpp:64-81`）；`tools/idlc.py:549` 生成硬编码 `std::chrono::seconds(5)`。
- 标准：方法签名里**没有** `Duration`；超时是调用方在 `Future` 上 `wait_for`（`method.h:196`、我们 `future.h:276`）。
- 改成：绑定 SPI 只留 `Submit(methodId, payload) -> {Future, token}` 与 `Drop(token)`；`IBindingHandle` 上的 `SendRequest(..., Duration)` 降级为**非虚便利函数**（= Submit + `wait_for` + `Drop`），供 api 层与迁移期使用；`dispatcher::Send(..., Duration)` 删除，超时记账只剩调用线程一处。公共 api 的同步形态**不是**标准的一部分，标记为项目扩展。
- 影响面：`vsomeip_request_dispatcher.{h,cpp}`、`vsomeip_bind_handle.cpp`、生成物、17 处 UCM 调用。
- 风险：高（这是唯一真正动并发内核的改动，现有 8 场景是它的回归网）。

**C8 生成物方法返回 `Errc` 而非 `Future`**
- 现状：`tools/idlc.py:544-560` 生成 `Errc %sProxy::%s(...)`，内部同步 `handle_.SendRequest` 后解包。
- 标准：`Future<Output>`。
- 改成：生成 `ara::core::Future<Output> Method()`；类型化解码放在 `Future::then` 里（我们 `future.h:325-390` 有 `then`），调用方自己 `get()`/`wait_for`。
- 风险：中-高。

### 发现面

**C9 发现 API 非标准、句柄无生命周期**
- 现状：`FindServiceHandle` 是纯 struct（`types.h:82-98`，含 `uid` 自增、无析构行为）；`ServiceProxy::Init` 内部自己注册；handler 只收 handles；`IsAvailable()`/`WaitForAvailable` 是项目自造；没有 `FindService`/`StartFindService`/`StopFindService`。
- 标准：`SWS_CM_00122/00622`（立即发现）、`SWS_CM_00303`（FindServiceHandle）、`SWS_CM_00304`（ServiceHandleContainer）；handler 签名带 `FindServiceHandle`（`types.h:224`），使回调能自行停止发现。
- 改成：静态 `FindService(InstanceIdentifier)`；`StartFindService(handler)` 返回 `FindServiceHandle`；`StopFindService(FindServiceHandle)`；`FindServiceHandle` 变 RAII（析构即停止那一路发现）；`WaitForAvailable` 降级为便利方法（保留，不是标准必需）。
- 风险：中。

### 装配面

**C19 应用层自己创建并注册绑定 runtime，绑定名写死在业务里**
- 现状：6 个 app 各自做三件事——`com::CreateBindRuntime("vsomeip", "<app>")`、`runtime->RegisterBindRuntime(...)`、`runtime->GetBindRuntime("vsomeip")`：`apps/someip_client/main.cpp:60/67/75`、`apps/vehicle_status_client/main.cpp:50/55/60`、`apps/ucm_service/main.cpp:35/40/44`、`apps/ucm_client/main.cpp:59/64/68`、`apps/vehicle_status_service/main.cpp:70/75/80`、`apps/someip_server/main.cpp:95/102/110`。共 18 处，每个应用都写着协议名 `"vsomeip"`。
- 这是比 `BindHandle` 更彻底的泄漏：业务层不仅看见了绑定类型，还**指定了协议实现**。违反 §1.5 硬约束 4"绑定 SPI 可替换"——换绑定时要改 6 个应用。
- 标准：应用只调 `Runtime::GetInstance()` 与 `GetBindRuntimes()`，绑定由 CM 按配置装载（`~/capi/.../internal/proxy/proxy.h:346-375` 的 `FindService` 就是这么用的：直接遍历 `GetBindRuntimes()`，应用不构造任何绑定）。
- 改成：`Runtime::Initialize(config)` 由 runtime 自己装配绑定；`CreateBindRuntime`/`RegisterBindRuntime`/`GetBindRuntime` 移出应用可见面（降为 `binding/` 内部 API 或 runtime 私有）；应用启动块只剩 `Runtime::Initialize()`。
- 影响面：6 个 app 的启动段；`Runtime` 需要一条配置入口（先用现有 vsomeip 配置路径，不新增配置文件格式）。
- 风险：中（改的是每个进程的启动顺序，出错表现为"服务找不到"，需要逐个 app 跑一遍）。

### 事件面（新增子系统）

**C10 公共 API 无事件**
- 现状：只有 `BindHandle::SubscribeEvent(eventId, group, std::function<void(std::vector<uint8_t> const&)>)`（`proxy.h:63`）——回调直塞原始字节，无订阅状态、无样本上限、无拉取。
- 标准：`Event<T>` 对象：`Subscribe(maxSampleCount)`、`Unsubscribe()`、`GetSubscriptionState()`、`SetReceiveHandler()`、`GetNewSamples(cb, max)`、`GetFreeSampleCount()`（`event.h:98-140`，`SWS_CM_00702`）。
- 改成：新增 `com/api/event.h`；生成代理持有 `Event<Payload>`（v2 先以 `Payload` 为样本类型，避免在 com 层引入 IDL 类型）；生成器把 "SubscribeXxx(handler)" 换成 `Event` 成员 + 应用自行 `GetNewSamples` 解码。
- 风险：高（新子系统）。

**C11 无样本缓冲与丢样本语义**
- 现状：无。`§7.2` 第 16 项也承认"事件路径完全没测"。
- 标准：`Subscribe(maxSampleCount)` 承诺应用最多持有多少样本，超出返回 `kMaxSamplesReached`（`SWS_CM_10432`）；`GetFreeSampleCount()` 可查。
- 改成：绑定 io 线程写环形缓冲；应用 `GetNewSamples` 拉取；超限按标准码报错并计数（复用 dispatcher `Stats` 的可观测思路）。
- 风险：高。

**C12 无 `SubscriptionState`**
- 现状：无。
- 标准：`SWS_CM_00310` 三态 + `SWS_CM_00311` 状态变化 handler；`SWS_CM_00313/00314/00315` 约束了 pending→subscribed 与重连上的通知时序。
- 改成：加枚举与 `SetSubscriptionStateChangeHandler`；vsomeip 的 subscribe ACK 映射到 pending→subscribed。
- 风险：中。

### 骨架面

**C13 骨架无 `OfferService`/`StopOfferService` 命名与能力**
- 现状：`Skeleton::Offer()`（`skeleton.h:144`）、`StopOffer()`；无 `ProcessNextMethodCall()`。
- 标准：`SWS_CM_00010/00011` + `skeleton.h:113-121`。
- 改成：重命名并对齐可重入语义；`ProcessNextMethodCall` 若本次不做，必须在头文件与文档里显式写"不支持"并让 `MethodCallProcessingMode` 保持 `kEvent`（见 C15）。
- 风险：低。

**C14 三个服务端 app 绕过 `Skeleton::Init`/`Offer`**
- 现状：`apps/ucm_service/main.cpp:46-53`、`apps/someip_server/main.cpp:122-127`、`apps/vehicle_status_service/main.cpp:91-97` 都直接 `bindRuntime->CreateBindSkeleton(...)` + `bs->Offer()`；`ServiceSkeleton` 实质只是 ID 载体。
- 标准：应用只与 `ServiceSkeleton` 交互，Offer 由骨架完成（`skeleton.h:407-442`）。
- 改成：app 改走 `ServiceSkeleton::Init/OfferService`。
- 风险：中（服务端行为首次真正经过骨架层，需要一次服务端回归）。

**C15 `MethodCallProcessingMode` 是死类型**
- 现状：`types.h:104` 定义 `enum class MethodCallProcessingMode{kEvent,kParallel}`，全仓库无使用者。
- 标准：该枚举决定服务端执行模型（`kEvent` 下由 `ProcessNextMethodCall` 驱动）。
- 改成（决策 D1）：**删除该枚举**，在 `api/skeleton.h` 与 `docs/` 显式声明"本项目只支持事件驱动执行模型，`kParallel` 不支持"。不允许保留一个没人用、也没人实现的枚举。
- 风险：低。

**C16 生成骨架是"组合 + Register()"而非继承 `ServiceSkeleton`**
- 现状：`tools/idlc.py:581-606` 生成独立类 `VehicleStatusServiceSkeleton`，持 `std::vector<com::skeleton::BindSkeleton*> binds_`，靠 `Register(BindSkeleton&)`/`RegisterEvent(BindSkeleton&)` 注入。
- 标准：生成的 skeleton 继承 `ServiceSkeleton`（`skeleton.h:131`）。
- 改成：`class VehicleStatusSkeleton : public com::skeleton::ServiceSkeleton`，方法 handler 与事件在构造函数里注册，不再有 `Register()`。
- 风险：中。

### 守卫

**C17 测试只覆盖绑定层，无 conformance 层**
- 现状：`tests/com_concurrency/main.cpp` 直接 `com::CreateBindRuntime(...)` + `BindHandle`（`:94-99`、`:172`、`:219`），测的是绑定层并发；公共 API 一条端到端用例都没有。§1.5 要求的 `tests/conformance`（同一套契约用例换绑定只换目标）不存在。
- 标准：契约用例靠公共 API 表达，换绑定只换编译目标。
- 改成：新增 `tests/conformance/**`（方法、事件、发现、超时、Stop 结算各一组）；`tests/com_concurrency` 保留为绑定层测试，明确它测的不是公共 API。
- 风险：中。

**C18 "不泄漏绑定类型"只靠人工，无守卫**
- 现状：§1.5 硬约束 4 是文字；`proxy.h` 自己就同时有 `BindHandle` 与 `ServiceProxy`。
- 标准：无（这是工程手段，但缺了它 C1/C2 迟早回退）。
- 改成：新增 `tests/api_headers/`：一个只 `#include "com/api/*.h"` 的 TU（编不过就说明泄漏）+ 一个 grep 守卫（`com/api/**` 不得出现 `binding::`、`vsomeip`、`Impl`）。
- 风险：低。

---

## 4. 目标 API（关键签名）

```cpp
// com/api/types.h
namespace com {
using Payload = std::vector<std::uint8_t>;
using ServiceIdentifier = ...; using InstanceIdentifier = ...;   // 保持现有别名
template <typename T> using SamplePtr = std::shared_ptr<T const>;
enum class SubscriptionState : std::uint8_t { kSubscribed, kNotSubscribed, kSubscriptionPending }; // SWS_CM_00310
using EventReceiveHandler = std::function<void()>;                                            // SWS_CM_00702 配套
using SubscriptionStateChangeHandler = std::function<void(SubscriptionState)>;                // SWS_CM_00311
} // namespace com

// com/api/proxy.h
namespace com {
class Proxy {                                        // 生成代理的基类
public:
    virtual ~Proxy() noexcept = default;
    virtual ServiceIdentifier const &GetServiceIdentifier() const noexcept = 0;
};

template <typename TProxy>
class ServiceProxy : public Proxy {
public:
    class HandleType {                               // SWS_CM_00312：只暴露实例标识
    public:
        InstanceIdentifier const &GetInstanceId() const noexcept;
        bool operator==(HandleType const &) const noexcept;
        bool operator<(HandleType const &) const noexcept;
    private:
        std::shared_ptr<binding::IBindingHandle> bindHandle_;   // 绑定句柄不外露
    };

    static Result<TProxy> Create(HandleType const &handle) noexcept;                      // SWS_CM_11326/10438
    static Result<ServiceHandleContainer<HandleType>> FindService() noexcept;             // 任意实例
    static Result<ServiceHandleContainer<HandleType>> FindService(
        InstanceIdentifier const &instance) noexcept;                                     // SWS_CM_00122
    static Result<FindServiceHandle> StartFindService(FindServiceHandler<HandleType> handler) noexcept;
    static void StopFindService(FindServiceHandle handle) noexcept;
    HandleType GetHandle() const noexcept;                                                // SWS_CM_10383
protected:
    explicit ServiceProxy(HandleType const &handle) noexcept;                             // SWS_CM_00131
    Result<void> Initialize() noexcept;                                                   // 创建 bind proxy
};

template <typename R, typename... Args>
class Method {                                       // SWS：operator() -> Future
public:
    ara::core::Future<R> operator()(Args &&...args) noexcept;                 // 标准形态，无超时
    // 项目扩展（非标准，标注清楚）：调用线程内的同步便利
    Result<R> Call(Duration timeout) noexcept;                                // = operator() + wait_for + Drop
};
} // namespace com

// com/api/event.h
namespace com {
template <typename T>
class Event {
public:
    Result<void> Subscribe(std::size_t maxSampleCount) noexcept;              // SWS_CM_00702
    void Unsubscribe() noexcept;
    SubscriptionState GetSubscriptionState() const noexcept;                  // SWS_CM_00310
    Result<void> SetReceiveHandler(EventReceiveHandler) noexcept;             // handler 在绑定 io 线程
    Result<void> UnsetReceiveHandler() noexcept;
    Result<std::size_t> GetNewSamples(
        std::function<void(SamplePtr<T const>)> &&callback,
        std::size_t maxNumberOfSamples) noexcept;                            // 只能由应用线程调用
    std::size_t GetFreeSampleCount() noexcept;
};
} // namespace com

// com/api/skeleton.h
namespace com {
namespace skeleton {
class Skeleton {
public:
    virtual ~Skeleton() noexcept = default;
    virtual ServiceIdentifier const &GetServiceIdentifier() const noexcept = 0;
    virtual Result<void> OfferService() noexcept = 0;                         // SWS_CM_00010
    virtual void StopOfferService() noexcept = 0;                             // SWS_CM_00011
    virtual Future<bool> ProcessNextMethodCall() noexcept = 0;                // 本次实现或显式不支持
protected:
    std::vector<std::unique_ptr<binding::IBindingSkeleton>> bindSkeletons_;   // 私有，无 getter
};

template <typename T>
class Event {                                                                 // 骨架侧
public:
    Result<void> Send(T const &sample) noexcept;                              // SWS_CM_00162
};

class ServiceSkeleton : public Skeleton { /* 生成的骨架继承它 */ };
} // namespace skeleton
} // namespace com
```

生成物形态（idlc 目标）：

```cpp
namespace vehicle_status {
class VehicleStatusProxy final : public com::ServiceProxy<VehicleStatusProxy> {
public:
    static constexpr com::ServiceIdentifier serviceIdentifier{0x6100};
    com::Method<Errc, std::string&, std::string&, OtaState&> GetStatus;
    com::Event<OtaStateChangedPayload> OtaStateChanged;
private:
    friend class com::ServiceProxy<VehicleStatusProxy>;
    explicit VehicleStatusProxy(HandleType const &handle) noexcept;
};

class VehicleStatusSkeleton final : public com::skeleton::ServiceSkeleton {
public:
    static constexpr com::ServiceIdentifier serviceIdentifier{0x6100};
    explicit VehicleStatusSkeleton(VehicleStatusService &service) noexcept;
    com::skeleton::Event<OtaStateChangedPayload> OtaStateChanged;
};
}  // namespace vehicle_status
```

业务侧最终形态：

```cpp
auto handles = com::ServiceProxy<vehicle_status::VehicleStatusProxy>::FindService();
if (!handles || handles.Value().empty()) { /* 无服务 */ }
auto proxy = vehicle_status::VehicleStatusProxy::Create(handles.Value().front());
if (!proxy) { /* 创建失败 */ }
auto fut = proxy.Value().GetStatus();                          // 异步，无超时承诺
// 同步：调用方自己等，库不记账
if (fut.wait_for(std::chrono::seconds(5)) == ara::core::future_status::timeout) { /* 调用方决定 */ }
auto out = fut.GetResult();                                   // Result<GetStatusOutput>
```

关于输出的两点决定（都按标准，不按现状）：

1. **生成的 `Method` 用聚合输出**，不用 `(std::string&, std::string&, OtaState&)` 这种出参：出参跨越 Future 的线程边界是错的设计。`Output` 是只含载荷字段的聚合体。
2. **线上的 `errc` 映射为 Future 的 error**（`MakeErrorCode(Errc)`），而不是塞进 `Output` 里。这需要为契约里的 `Errc` 建一个 `ara::core::ErrorDomain`（标准做法，参考实现同样把业务错误放在独立域里）。

`src/ucm/ucm_proxy.{h,cpp}` 的 17 处 `handle_.SendRequest` 全部变成对生成代理的方法调用，`UcmProxy` 的构造参数从 `BindHandle&` 变成 `ServiceProxy&`（或直接持有生成代理）。UCM 业务层把线上 `errc` 转成 `ota::UcmErrc` 的那层映射保留——那是业务语义，不属于 com。

**一个不对齐但决定不改的点**：标准里 `InstanceIdentifier` 是类（带 `InstanceIdentifier::MakeAny()`），我们的是 `using InstanceIdentifier = uint16_t`（`types.h:37`）。本次不改成类——"任意实例"用无参 `FindService()` 重载表达，比为一个概念凭空造 sentinel 更干净。这条要记在文档里，避免以后被当成漏项。

---

## 5. 代价（诚实版）

1. **C6/C7/C8 一次贯通**：绑定 SPI 的 `Submit/token` 化、dispatcher 删 `Send(Duration)`、idlc 生成 `Future` 方法、17 处 UCM + 6 个 app 改写。这是本次唯一真正动并发内核的改动，现有 8 个并发场景是它的回归网；**必须新增 Future 超时/迟到结算用例**，否则等于把未验证行为塞进新内核。
2. **C10/C11/C12 是新增子系统**（事件样本缓冲 + 订阅状态机），当前零覆盖。工作量与风险都独立于前一项，必须单独成阶段、单独出测试。
3. **C1/C2/C3 是纯结构改造**，机械但波及全部 include；好处是之后 C2 的守卫（C18）才成立。
4. **C4 错误码重排**会一次性改动所有 `errc` 比较点，编译期能查全，但语义上"哪个错误对应哪个码"需要逐处确认。
5. **服务端首次真正经过骨架层**（C14/C16），`apps/*_service` 的行为可能暴露隐藏假设（例如当前 `bs->Offer()` 的调用时机、事件注册顺序）。
6. **`ProcessNextMethodCall` 与 `MethodCallProcessingMode` 要么实现要么删**（C13/C15）；实现它意味着服务端执行模型可选，属于独立设计题。

---

## 6. 分阶段落地（每步独立可编译、可回滚）

| 阶段 | 内容 | 覆盖项 | 验收 |
| --- | --- | --- | --- |
| P1 | 目录与头文件分层；旧 `com/proxy.h` 等改为转发头；include 路径全量更新 | C1 | 全量构建 + ctest + 8 场景 |
| P2 | `ServiceProxy`/`Skeleton` 改 pimpl，声明/定义分离；`HandleType` 成型 | C2、C3 | 同上 |
| P3 | `ComErrc` 对齐标准 + 扩展区间；类型补齐（`Payload`/`SamplePtr`/`SubscriptionState`） | C4、C5 | 同上 + 错误码值断言单测 |
| P4a | dispatcher：删 `Send(Duration)`，加 `Submit()→{Future,token}` + `Drop(token)`；`IBindingHandle::SendRequest(Duration)` 降为非虚便利函数（行为等价） | C7（前半） | 同上（此时调用方零变化） |
| P4b | `Method` 与生成器改成 `Future` 形态；UCM 17 处 + 6 个 app 迁移 | C6、C7（后半）、C8 | 同上 + Future 超时/迟到用例 |
| P5 | Runtime 装配与发现面标准化：`Runtime::Initialize` 自装配绑定（C19）、`FindService`/`StartFindService`/`StopFindService`、`FindServiceHandle` RAII（C9） | C9、C19 | 同上 + 6 个 app 逐个启动验证 |
| P6 | 事件子系统（`Event<Payload>`、样本缓冲、`SubscriptionState`）+ 生成器事件成员 + 骨架 `Event::Send` | C10、C11、C12、C16（事件部分） | 同上 + 事件端到端用例 |
| P7 | 骨架 `OfferService` 重命名、生成骨架继承 `ServiceSkeleton`、删 `GetBindSkeletons()`、3 个服务端 app 迁移 | C13、C14、C15、C16 | 同上 + 服务端回归 |
| P8 | 守卫与测试：`tests/api_headers`（头卫生）、`tests/conformance`；删除全部转发头 | C17、C18 | 全量 + 9+ 场景 + TSan |

顺序理由：先能把"不泄漏"变成结构事实（P1–P3），再动最危险的内核（P4），最后才是新子系统（P6）。P4a 单独拆出来是因为它**不改变任何调用方**，可以独立验证"超时从库内搬到调用线程后行为不变"。

每阶段结束后统一跑：

```bash
export PATH=/home/topeet/Desktop/cmake-3.28.6-linux-x86_64/bin:$PATH
cd /home/topeet/rk3568-automotive-platform
cmake --build build -j4
ctest --test-dir build --output-on-failure
export VSOMEIP_PREFIX=$PWD/build/third_party/vsomeip
bash tests/com_concurrency/run.sh          # 8 场景必须仍全绿
```

## 7. 回滚

- 每阶段一个工作区 diff，验收不通过即 `git checkout -- <该阶段涉及文件>`；因为转发头在 P8 之前一直保留，P1–P7 任一步回退都不影响其余阶段的 include。
- 全局兜底：`git checkout -- .` 回到 `9101a31`。
- P4 有独立回滚点：`dispatcher::Send(Duration)` 删除与 `Submit/Drop` 新增在同一个 diff 里，回退即恢复原内核。

## 8. 明确不做（写清楚，不假装支持）

| 项 | 处置 |
| --- | --- |
| E2E 保护（`GetResult`/`GetSMState`） | 不做；`Event` 不提供 E2E 接口，文档标注不支持 |
| Field 的 getter/setter | 不做；只保留 notifier 语义（对应现有 `EventType::kField`），getter/setter 需要额外的 method 语义与线格式，另立 ADR |
| CommunicationGroup（`ara/com/cg`） | 不做；本项目无通信组模型 |
| 自定义 allocator / `SampleAllocateePtr` 的共享内存语义 | 不做；`SamplePtr` 用 `shared_ptr`，`Send` 走"应用持有数据"那条（`SWS_CM_00162`） |
| 多绑定同时 Offer 的仲裁 | 保持"向所有已注册绑定 Offer"，与参考实现一致（`~/capi/.../internal/skeleton/skeleton.h:407-442`） |

## 9. 决策记录

### 9.1 已决议（2026-11-21 评审通过）

| # | 问题 | 决定 | 对本文的影响 |
| --- | --- | --- | --- |
| D1 | `ProcessNextMethodCall` / `MethodCallProcessingMode` 实现还是删除 | **删除枚举**，头文件与文档显式声明"不支持 `kParallel` 执行模型" | C15 从"二选一"收敛为删除；P7 执行 |
| D2 | 生成物是否提供非标准 `...Sync(Duration)` | **提供**，命名与注释都标注"非标准扩展"；标准形态 `Future` 同时保留 | C8 实现里两套签名并存，但依赖关系是从 Sync 调 Future，不是两套内核 |
| D3 | `Event<Payload>` 还是 `Event<Typed>` | **`Event<Payload>`**：com 层只交换字节，解码留在 `gen` 层 | C10 的样本类型定为 `Payload`；`GetNewSamples` 回调里多一层解码，接受 |
| D4 | ADR 整体是否按其执行 | 按 ADR 执行，18 项 + 本次补充的 C19 全做 | 阶段表生效 |

### 9.2 仍开放

1. **样本缓冲的容量上界与丢弃策略**（丢最旧 / 拒绝新样本 / 计数告警）：需要一个默认值，D3 只定了样本类型，没定背压策略。P6 开工前定。
2. **仓库外是否存在 `com/**` 头的消费者**（板端/上层工程）：未验证，无法从本仓库判断。处置：P1 保留 7 个旧公共头作为转发头，P8 才删；若确认无外部消费者，P8 可提前。

---

## 附录：取证命令

```bash
# 标准侧（参考实现）
sed -n '90,240p'  ~/capi/com/com/src/com/include/ara/com/internal/proxy/event.h
sed -n '140,230p' ~/capi/com/com/src/com/include/ara/com/internal/proxy/proxy.h   # HandleType
sed -n '330,400p' ~/capi/com/com/src/com/include/ara/com/internal/proxy/proxy.h   # Create/FindService
sed -n '50,110p'  ~/capi/com/com/src/com/include/ara/com/com_error_domain.h       # ComErrc
sed -n '85,130p'  ~/capi/com/com/src/com/include/ara/com/internal/skeleton/skeleton.h

# 现状侧（本仓库）
grep -n 'class BindHandle\|class ServiceProxy\|virtual Result\|SendRequest' src/com/include/com/proxy.h
grep -n 'class BindSkeleton\|class Skeleton\|GetBindSkeletons\|OfferEvent\|SendEvent' src/com/include/com/skeleton.h
grep -n 'kServiceNotFound\|kTimeout\|kId' src/com/include/com/com_error_domain.h
grep -n 'SendRequest\|Duration' src/com/include/com/vsomeip/vsomeip_request_dispatcher.h
grep -n 'BindHandle\|BindSkeleton\|SendRequest\|SubscribeEvent\|OfferEvent' tools/idlc.py
grep -rn 'handle_.SendRequest' src/ucm/ucm_proxy.cpp        # 17 处
grep -rn 'CreateBindSkeleton' apps/*/main.cpp             # 3 个服务端 app
```
