# com - 通信抽象层（贴近 AUTOSAR ara::com）

## 概述

`com` 模块提供车载通信的抽象接口层，将上层应用与底层通信协议（SOME/IP、DDS 等）解耦。

架构决策：跳过 nsomeip 中间层，保留`com` 层的 `BindRuntime` 解耦设计，底层直接绑定 vsomeip3，同时复用 `ara::core` 核心类型（`ErrorCode` / `Result` / `Future` / `Promise` / `InstanceSpecifier`）以贴近 AUTOSAR 标准

只是用来学习CAPI的架构和Code

## 架构设计

```
┌────────────────────────────────────────────────────────────┐
│                    上层应用 (Applications)                    │
│   proxy::ServiceProxy（客户端） / skeleton::ServiceSkeleton（服务端） │
└──────────────────────────────┬─────────────────────────────┘
                               │
                               ▼
                  com::Runtime（单例，管理多个 BindRuntime）
                               │
              ┌────────────────┴────────────────┐
              │          BindRuntime（抽象）        │
              └───────┬────────────────┬─────────┘
        proxy 侧        │                │        skeleton 侧
   proxy::BindHandle ◄──┘                └──► skeleton::BindSkeleton
              │                                    │
              ▼                                    ▼
      vsomeip_bind / dds_bind / tcp_bind（协议实现，后两者预留）
```

## 分阶段改造状态

| 阶段 | 内容                                                       | 状态    |
| ---- | ---------------------------------------------------------- | ------- |
| P0   | `com::Result` 别名到 `ara::core::Result<T, ErrorCode>` | ✅ 完成 |
| P1   | 定义`ComErrorDomain` + `ComErrc` 错误码枚举            | ✅ 完成 |
| P2   | 引入`ara::core::Future/Promise` 替代回调式异步           | ✅ 完成 |
| P3   | 引入`InstanceSpecifier` 服务标识（基础支持）             | ✅ 完成 |

## 核心类型

| 类型                       | 来源           | 说明                                               |
| -------------------------- | -------------- | -------------------------------------------------- |
| `com::Result<T>`         | ara::core (P0) | 别名到`ara::core::Result<T, ErrorCode>`          |
| `com::ComErrc`           | com (P1)       | 通信层错误码枚举                                   |
| `com::ComErrorDomain`    | com (P1)       | 通信层错误域                                       |
| `com::Future<T>`         | ara::core (P2) | 异步操作结果（用`GetResult()` / `get()` 获取） |
| `com::Promise<T>`        | ara::core (P2) | 异步操作承诺（与`Future` 配对使用）              |
| `com::InstanceSpecifier` | ara::core (P3) | AUTOSAR 标准服务标识                               |

### P0：错误处理标准化

- 新增 `com/com_error_domain.h`，定义 `ComErrorDomain` 和 `ComErrc`。
- `com::Result<T>` 别名到 `ara::core::Result<T, ara::core::ErrorCode>`。
- 工厂方法：`Result::FromValue(...)` / `Result::FromError(ComErrc)` / `Result::FromError(ErrorCode)`。
- 判断用 `if (result)`（`explicit operator bool`），取值用 `.Value()`，取错误用 `.Error()`（`.Error().Message()` 得到可读消息）。

### P1：错误域定义

- `ComErrc` 枚举：`kSuccess` / `kServiceNotFound` / `kTimeout` / `kSerializationFailed` / `kInvalidHandle` / `kInvalidArgument` / `kNotInitialized` / `kAlreadyInitialized` / `kCommunicationError` / `kInternalError` / `kInvalidName` / `kNotFound`。
- `ComErrorDomain` 继承 `ara::core::ErrorDomain`，域 ID = `0x434F4D00`。
- `MakeErrorCode(ComErrc)` 辅助函数创建属于该域的 `ErrorCode`。

### P2：Future/Promise 异步通信

- `com::Future<T>` / `com::Promise<T>` 分别别名到 `ara::core::Future<T>` / `ara::core::Promise<T>`。
- `VsomeipBindHandle::SendRequestAsync(methodId, data)` 返回 `Future<std::vector<uint8_t>>`。
- 内部用 `ara::core::Promise` 把 vsomeip 回调桥接为 Future 的完成。
- 回调式异步接口已移除，统一使用 Future 语义。

```cpp
// P2：ara::com 风格异步调用
auto future = handle->SendRequestAsync(0x0001, requestData);
auto result = future.GetResult();  // 阻塞，返回 ara::core::Result<std::vector<uint8_t>, ErrorCode>
if (result) {
    auto const &data = result.Value();  // 响应字节
} else {
    auto const &err = result.Error();   // ara::core::ErrorCode
}
```

> 注意：导入的 `ara::core::Future` 实现里 `then()` 的续体是零参调用，与标准用法不符，不建议使用；阻塞等待请用 `GetResult()` / `get()`，非阻塞轮询请用 `is_ready()` / `wait_for()`。

### P3：InstanceSpecifier 服务标识

- `types.h` 新增 `using InstanceSpecifier = ara::core::InstanceSpecifier`。
- `ServiceProxy` / `ServiceSkeleton` 新增接受 `InstanceSpecifier` 的构造函数。
- `Runtime` 新增 `RegisterServiceMapping()` / `ResolveServiceMapping()` 用于 `InstanceSpecifier → (serviceId, instanceId)` 映射。
- 绑定层仍使用 `uint16_t` 数字 ID。

## 核心接口

### Runtime（运行时单例）

```cpp
class Runtime
{
public:
    static Runtime *Get() noexcept;                       // 单例（永不返回空）
    Result<void> Init() noexcept;                          // 初始化
    Result<void> Deinit() noexcept;                        // 反初始化
    void Start() noexcept;                                 // 启动所有绑定运行时（阻塞）
    void Stop() noexcept;                                  // 停止所有绑定运行时

    Result<void> RegisterBindRuntime(std::unique_ptr<BindRuntime> rt) noexcept;
    Result<void> UnregisterBindRuntime(std::string const &name) noexcept;
    BindRuntime *GetBindRuntime(std::string const &name) noexcept;
    std::vector<std::string> GetBindRuntimeNames() const noexcept;

    // P3：InstanceSpecifier → (serviceId, instanceId)
    void RegisterServiceMapping(InstanceSpecifier const &spec,
                                ServiceIdentifier serviceId,
                                InstanceIdentifier instanceId) noexcept;
    bool ResolveServiceMapping(InstanceSpecifier const &spec,
                               ServiceIdentifier &serviceId,
                               InstanceIdentifier &instanceId) const noexcept;
};
```

### BindRuntime（绑定层抽象）

每个协议实现（vsomeip / dds / tcp）继承此类：

```cpp
class BindRuntime
{
public:
    virtual Result<void> Init() noexcept;                 // 默认成功
    virtual Result<void> Deinit() noexcept;               // 默认成功
    virtual void Start() noexcept;                        // 默认空
    virtual void Stop() noexcept;                         // 默认空

    // 服务端：创建绑定骨架
    virtual void CreateBindSkeleton(
        skeleton::Skeleton &skeleton,
        InstanceIdentifier const &instanceIdentifier,
        std::vector<std::unique_ptr<skeleton::BindSkeleton>> &out) noexcept = 0;

    // 客户端：获取可用服务句柄 / 注册服务发现
    virtual void GetAvailableServiceHandles(
        ServiceIdentifier const &serviceId,
        InstanceIdentifier const &instanceId,
        ServiceHandleContainer<std::shared_ptr<proxy::BindHandle>> &out) noexcept = 0;
    virtual void RegisterFindServiceHandle(
        FindServiceHandle const &findHandle,
        FindServiceHandler<std::shared_ptr<proxy::BindHandle>> const &handler) noexcept = 0;
    virtual void UnregisterFindServiceHandle(FindServiceHandle const &findHandle) noexcept = 0;

    virtual char const *GetName() const noexcept = 0;     // 例如 "vsomeip"
};
```

### proxy 层（客户端）

- `proxy::BindHandle`（抽象）：`IsValid()` / `GetBindRuntimeName()` / `SendRequest(methodId, data, timeout)`（同步，返回 `Result<std::vector<uint8_t>>`）/ `SendRequestAsync(methodId, data)`（异步，返回 `Future<std::vector<uint8_t>>`）。
- `proxy::BindProxy`（抽象）：`Init()` / `Deinit()` / `GetBindRuntimeName()`。
- `proxy::ServiceProxy`：持有多个 `BindHandle`，提供 `Init()` / `Deinit()` / `IsAvailable()` / `WaitForAvailable(timeout)` / `GetInstanceSpecifier()`。

### skeleton 层（服务端）

- `skeleton::BindSkeleton`（抽象）：`Init()` / `Deinit()` / `Offer()` / `StopOffer()` / `RegisterMethodHandler(methodId, handler)` / `GetBindRuntimeName()`。
- `skeleton::Skeleton`：持有多个 `BindSkeleton`，提供 `GetServiceIdentifier()` / `SetServiceIdentifier()` / `Init(instanceId)` / `Deinit()` / `Offer()` / `StopOffer()`。
- `skeleton::ServiceSkeleton : public Skeleton`：应用继承此类实现服务，构造函数接受 `(serviceId, instanceId)` 或 `(specifier, serviceId, instanceId)`，提供 `GetInstanceSpecifier()`。

### vsomeip 绑定实现（`com::vsomeip_binding`）

- `VsomeipBindRuntime`：`BindRuntime` 的 vsomeip3 实现，构造参数为应用名（`std::string`）。
- `VsomeipBindHandle`：`BindHandle` 的 vsomeip3 实现，额外提供 `SubscribeEvent(eventId, eventGroupId, handler)` / `UnsubscribeEvent(eventId, eventGroupId)` / `GetServiceId()` / `GetInstanceId()`。
- `VsomeipBindSkeleton`：`BindSkeleton` 的 vsomeip3 实现，额外提供 `SendEvent(eventId, data)`。（`RegisterMethodHandler` 的 `handler` 签名为 `void(std::vector<uint8_t> const &request, std::vector<uint8_t> &response)`。）

## 底层绑定实现

### vsomeip_bind

基于 [vsomeip3](https://github.com/COVESA/vsomeip) 的 SOME/IP 协议实现。

```cpp
#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"

// 创建并注册 vsomeip 运行时
auto runtime = com::Runtime::Get();
auto vsomeip = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("my_service");
vsomeip->Init();
runtime->RegisterBindRuntime(std::move(vsomeip));
```

### dds_bind（预留）

基于 DDS 的实现，预留接口。

### tcp_bind（预留）

基于原始 TCP 的实现，用于调试或简单场景。

## 已知限制

1. **Future 的 `then()` 不可靠**：导入的 `ara::core::Future::then()` 以零参调用续体，与 AUTOSAR 标准不符；请用 `GetResult()` / `get()`。
2. **并发同 method 请求不能正确关联**：vsomeip3 的响应经 method 级 `register_message_handler` 路由，同一个 `(service, instance, method)` 只能有一个 handler，重复注册会覆盖；顺序请求（一发一收）正确，并发同 method 请求需改用 session 级分派（待办）。

## 编译选项

```cmake
# 启用/禁用 vsomeip 支持
option(COM_WITH_VSOMEIP "Build with vsomeip support" ON)

# 构建示例程序
option(COM_BUILD_EXAMPLES "Build com examples" OFF)
```

## 使用示例

### 服务端

```cpp
#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/skeleton.h"

class EchoService : public com::skeleton::ServiceSkeleton
{
public:
    EchoService() : com::skeleton::ServiceSkeleton(0x1234, 1) {}
};

int main()
{
    auto *runtime = com::Runtime::Get();
    runtime->Init();

    auto vsomeip = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("echo_service");
    vsomeip->Init();
    runtime->RegisterBindRuntime(std::move(vsomeip));

    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");

    EchoService skeleton;
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(skeleton, 1, bindSkeletons);
    for (auto &bs : bindSkeletons) bs->Offer();

    runtime->Start();  // 阻塞，处理请求

    for (auto &bs : bindSkeletons) bs->StopOffer();
    runtime->Stop();
    runtime->Deinit();
    return 0;
}
```

### 客户端

```cpp
#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/proxy.h"

int main()
{
    auto *runtime = com::Runtime::Get();
    runtime->Init();

    auto vsomeip = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("echo_client");
    vsomeip->Init();
    runtime->RegisterBindRuntime(std::move(vsomeip));

    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");

    com::FindServiceHandle findHandle{0x1234, 1};
    bindRuntime->RegisterFindServiceHandle(findHandle,
        [](com::ServiceHandleContainer<std::shared_ptr<com::proxy::BindHandle>> const &handles)
        {
            for (auto const &handle : handles)
            {
                if (handle && handle->IsValid())
                {
                    std::vector<uint8_t> request = {1, 2, 3};
                    auto result = handle->SendRequest(0x0001, request, std::chrono::seconds(5));
                    if (result)
                    {
                        // result.Value() 为响应字节
                    }
                }
            }
        });

    runtime->Start();  // 阻塞，处理服务发现与回调
    runtime->Deinit();
    return 0;
}
```

## 与 CAPI 的关系

本模块的设计参考了 AUTOSAR CAPI 的通信架构，但做了以下简化：

1. **复用 CAPI 核心类型**：直接使用 `ara::core` 的 `ErrorCode` / `Result` / `Future` / `Promise` / `InstanceSpecifier`。
2. **简化接口**：保留核心功能，去除复杂的生命周期管理。
3. **直接绑定 vsomeip**：跳过 CAPI 的 nsomeip 层，直接绑定 vsomeip3。

## 移植到 vsomeip

本模块已经直接移植到 vsomeip3，主要改动：

1. `vsomeip_bind_runtime.cpp` - 实现 `BindRuntime` 接口。
2. `vsomeip_bind_handle.cpp` - 实现 `BindHandle` 接口。
3. `vsomeip_bind_skeleton.cpp` - 实现 `BindSkeleton` 接口。

所有 vsomeip 相关的调用都封装在 `com::vsomeip_binding` 命名空间内，便于维护和替换。

## License

Apache License 2.0
