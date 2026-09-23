# com - 通信抽象层

## 概述

`com` 模块提供车载通信的抽象接口层，将上层应用与底层通信协议（SOME/IP、DDS等）完全解耦。

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                    上层应用 (Applications)                    │
└─────────────────────────────────────────────────────────────┘
                              │
                             ┌┴┐
                             │ │  com::IRuntime / com::ISkeleton / com::IProxy
                             └┬┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
     ┌────────┴──────┐ ┌─────┴─────┐ ┌───────┴───────┐
     │  vsomeip_bind │ │  dds_bind │ │   tcp_bind    │
     │  (SOME/IP)    │ │  (DDS)    │ │  (Raw TCP)    │
     └───────────────┘ └───────────┘ └───────────────┘
```

## 核心接口

### IRuntime

运行时抽象接口，负责创建和管理通信句柄。

```cpp
class IRuntime {
public:
    virtual ~IRuntime() = default;
    
    // 创建通信句柄
    virtual std::shared_ptr<IHandle> create_handle(
        const std::string& name,
        const ServiceConfig& config) = 0;
    
    // 启动/停止运行时
    virtual void start() = 0;
    virtual void stop() = 0;
};
```

### ISkeleton

服务端骨架接口，用于实现 SOME/IP 服务端。

```cpp
class ISkeleton {
public:
    virtual ~ISkeleton() = default;
    
    // 注册方法回调
    virtual void register_method_handler(
        method_id_t method_id,
        MethodHandler handler) = 0;
    
    // 发送响应
    virtual void send_response(
        const Message& request,
        const Payload& response) = 0;
    
    // 发送事件
    virtual void send_event(
        eventgroup_id_t group_id,
        event_id_t event_id,
        const Payload& data) = 0;
};
```

### IProxy

客户端代理接口，用于调用远程服务。

```cpp
class IProxy {
public:
    virtual ~IProxy() = default;
    
    // 调用远程方法
    virtual Future<Payload> call_method(
        method_id_t method_id,
        const Payload& request) = 0;
    
    // 订阅事件
    virtual void subscribe_event(
        eventgroup_id_t group_id,
        event_id_t event_id,
        EventHandler handler) = 0;
};
```

## 底层绑定实现

### vsomeip_bind

基于 [vsomeip3](https://github.com/COVESA/vsomeip) 的 SOME/IP 协议实现。

```cpp
#include <com/vsomeip/vsomeip_bind_runtime.h>

// 创建 vsomeip 运行时
auto runtime = com::vsomeip::create_runtime("my_service_name");

// 创建服务端骨架
auto skeleton = runtime->create_skeleton(service_id, instance_id);

// 注册方法处理
skeleton->register_method_handler(0x0001, [](const Message& req) {
    // 处理请求
    return create_response();
});
```

### dds_bind (预留)

基于 DDS (Data Distribution Service) 的实现，预留接口。

### tcp_bind (预留)

基于原始 TCP 的实现，用于调试或简单场景。

## 编译选项

```cmake
# 启用/禁用 vsomeip 支持
option(COM_WITH_VSOMEIP "Build with vsomeip support" ON)

# 构建测试
option(COM_BUILD_TESTS "Build tests" OFF)
```

## 使用示例

### 服务端示例

```cpp
#include <com/vsomeip/vsomeip_bind_runtime.h>
#include <iostream>

int main() {
    // 创建运行时
    auto runtime = com::vsomeip::create_runtime("hello_service");
    
    // 创建骨架
    auto skeleton = runtime->create_skeleton(0x1234, 0x0001);
    
    // 注册方法处理
    skeleton->register_method_handler(0x0001, 
        [](const com::Message& request) -> com::Payload {
            std::cout << "Received request" << std::endl;
            return com::Payload::from_string("Hello, World!");
        });
    
    // 启动运行时
    runtime->start();
    
    // 等待退出
    std::cin.get();
    
    runtime->stop();
    return 0;
}
```

### 客户端示例

```cpp
#include <com/vsomeip/vsomeip_bind_runtime.h>
#include <iostream>

int main() {
    // 创建运行时
    auto runtime = com::vsomeip::create_runtime("hello_client");
    
    // 创建代理
    auto proxy = runtime->create_proxy(0x1234, 0x0001);
    
    // 调用远程方法
    auto future = proxy->call_method(0x0001, 
        com::Payload::from_string("Hi!"));
    
    // 等待响应
    future.then([](const com::Payload& response) {
        std::cout << "Response: " << response.to_string() << std::endl;
    });
    
    // 启动运行时（处理异步回调）
    runtime->start();
    
    std::cin.get();
    
    runtime->stop();
    return 0;
}
```

## 与 CAPI 的关系

本模块的设计参考了 AUTOSAR CAPI 的通信架构，但做了以下简化：

1. **去除 CAPI 依赖**：不依赖 CAPI 的 core-types、ara-core 等模块
2. **简化接口**：保留核心功能，去除复杂的生命周期管理
3. **直接绑定 vsomeip**：跳过 CAPI 的 nsomeip 层，直接绑定 vsomeip3

## 移植到 vsomeip

本模块已经直接移植到 vsomeip3，主要改动：

1. `vsomeip_bind_runtime.cpp` - 实现 IRuntime 接口
2. `vsomeip_bind_skeleton.cpp` - 实现 ISkeleton 接口  
3. `vsomeip_bind_handle.cpp` - 实现 IHandle 接口

所有 vsomeip 相关的调用都封装在 `com::vsomeip` 命名空间内，便于维护和替换。

## License

Apache License 2.0