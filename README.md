# rk3568-automotive-platform

车载通信与 OTA 平台

## 架构概述

```
┌─────────────────────────────────────────────────────────────┐
│                      Demo Applications                      │
│  doip_server │ someip_server │ someip_client │ ota_demo    │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    │   com (通信抽象层)  │
                    │  bind_runtime.h   │
                    │  proxy.h/skeleton.h│
                    └─────────┬─────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
     ┌────────┴──────┐ ┌─────┴─────┐ ┌───────┴───────┐
     │ vsomeip_bind  │ │ dds_bind  │ │  tcp_bind     │
     │ (SOME/IP)     │ │ (DDS)     │ │ (Raw TCP)     │
     └───────────────┘ └───────────┘ └───────────────┘
```

### 模块解耦设计

通信层采用抽象接口设计，上层应用与底层通信协议完全解耦：

- **`com::IRuntime`** - 运行时抽象接口
- **`com::IHandle`** - 通信句柄抽象接口
- **`com::ISkeleton`** - 服务端骨架抽象接口
- **`com::IProxy`** - 客户端代理抽象接口

底层绑定实现：

- **vsomeip_bind** - 基于 vsomeip3 的 SOME/IP 实现
- **dds_bind** - (预留) DDS 实现
- **tcp_bind** - (预留) 原始 TCP 实现

切换底层通信只需更改绑定的 Runtime 实现，上层代码无需修改。

## 当前里程碑

- M0：工程骨架（CMake、日志、配置）
- M1：Linux 网络基座（TCP / socket / epoll / 连接管理）
- M2：DoIP 消息层（ISO 13400-2）
- M3：UDS DCM（无 DTC）
- M4：UDS Download（0x34 / 0x36 / 0x37）
- M5：SOME/IP 接入 vsomeip3（SD + get_version）
- M6：OTA Manager（状态机、SHA-256 校验、Mock A/B Slot/Boot Control）
- M7：通信层解耦，支持 vsomeip/dds 可切换绑定

## 编译与运行

### 依赖

- CMake >= 3.16
- C++17 编译器
- vsomeip3 (已内置于 `third_party/vsomeip`)

### 构建

```bash
# 配置（默认启用 vsomeip 支持）
cmake -B build

# 或禁用 vsomeip（仅构建基础功能）
cmake -B build -DWITH_VSOMEIP=OFF

# 编译
cmake --build build -j
```

### 运行示例

```bash
# DoIP / UDS 服务
./build/apps/doip_server config/platform.json

# SOME/IP 服务端 / 客户端（需要 vsomeip 配置）
VSOMEIP_CONFIGURATION=config/vsomeip.json ./build/apps/someip_server 1.0.0
VSOMEIP_CONFIGURATION=config/vsomeip.json ./build/apps/someip_client

# OTA Manager 模拟 A/B 升级 / 回滚
./build/apps/ota_demo                 # 成功升级 A -> B
./build/apps/ota_demo --fail-boot     # 模拟 B 启动失败并回滚 A
```

### 通信绑定切换

通信层支持多种底层实现，通过编译选项或运行时配置切换：

```cpp
// 使用 vsomeip 绑定
#include <com/vsomeip/vsomeip_bind_runtime.h>
auto runtime = com::vsomeip::create_runtime("my_service");

// 未来：使用 DDS 绑定
// #include <com/dds/dds_bind_runtime.h>
// auto runtime = com::dds::create_runtime("my_service");
```

### 自研 DoIP/UDS 异步 dispatch

自研 `doip_server` 的网络线程只负责 epoll 收包、DoIP 帧重组和回包；
UDS 请求通过 `UdsDispatcher::Submit` 投递到 worker 线程处理，处理结果再经
`EpollLoop::post` 回到网络线程发送。这样长耗时的诊断服务不会阻塞 epoll loop。

## DoIP 模块

```text
src/doip/
├── doip_types.h          协议常量与车辆身份配置
├── doip_frame.h/.cpp      8 字节 Header 编解码
├── doip_connection.h/.cpp TCP 粘包重组与单连接状态
├── doip_routing_table.*    tester SA → fd 路由表
├── doip_discovery.h/.cpp   UDP 发现、Entity/Power、周期 Announcement
└── doip_server.h/.cpp      TCP 分发、Routing Activation、Alive Check
```

已覆盖 DoIP Payload Type：

```text
0x0000 Generic NACK
0x0001-0x0004 Vehicle Identification / Announcement
0x0005-0x0006 Routing Activation Request / Response
0x0007-0x0008 Alive Check Request / Response
0x4001-0x4004 Entity Status / Power Mode
0x8001 Diagnostic Message
0x8003 Diagnostic Message Negative ACK
```
