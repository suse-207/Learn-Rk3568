# rk3568-automotive-platform

车载通信与 OTA 平台（面试证据项目）。

## 当前里程碑

- M0：工程骨架（CMake、日志、配置）
- M1：Linux 网络基座（TCP / socket / epoll / 连接管理）
- M2：DoIP 消息层（ISO 13400-2）
- M3：UDS DCM（无 DTC）
- M4：UDS Download（0x34 / 0x36 / 0x37）
- M5：SOME/IP 接入 vsomeip3（SD + get_version）
- M6：OTA Manager（状态机、SHA-256 校验、Mock A/B Slot/Boot Control）

## 编译与运行

```bash
cmake -B build
cmake --build build -j

# DoIP / UDS 服务
./build/apps/doip_server config/platform.json

# SOME/IP 服务端 / 客户端
VSOMEIP_CONFIGURATION=config/vsomeip.json ./build/apps/someip_server 1.0.0
VSOMEIP_CONFIGURATION=config/vsomeip.json ./build/apps/someip_client

# OTA Manager 模拟 A/B 升级 / 回滚
./build/apps/ota_demo                 # 成功升级 A -> B
./build/apps/ota_demo --fail-boot     # 模拟 B 启动失败并回滚 A
```

> 本机默认 `/usr/bin/cmake` 是 3.10，无法构建本工程。请使用
> `/home/topeet/Desktop/cmake-3.28.6-linux-x86_64/bin/cmake`，或把该目录加入 `PATH`。

vsomeip3 已源码 vendor 到 `third_party/vsomeip`，由本工程 `CMakeLists.txt`
通过 `add_subdirectory(third_party/vsomeip EXCLUDE_FROM_ALL)` 直接编译，
不再依赖 `/home/topeet/Desktop/vsomeip/install` 的外部 `.so`。

### CAPI isoft-doip + isoft-uds 移植版

`port/` 是把 CAPI 的 `isoft-doip` 与 `isoft-uds` 原库移植到本工程的轻量兼容层：

- 不复用整套 NAI，只实现其实际调用的 `nai_*`/事件循环子集（`port/include/nai`、`port/src/*`）；
- `ara/core` 直接复用 CAPI `core-types`；
- `ara/log` 提供最小实现，可独立运行；
- 上游 CAPI 源码已整体移入 `port/upstream/capi`，作为 `port` 模块的一部分直接编译，不再通过 `third_party/capi` 引用。

```bash
# 构建
cmake -B build-port
cmake --build build-port -j4

# 运行 DoIP(13400) + UDS 服务
./build-port/apps/capi_diag_server

# 验证
python3 tools/doip_discovery_client.py 127.0.0.1 13400
python3 tools/doip_uds_client.py 127.0.0.1 13400
```

当前 demo 已跑通：UDP 车辆发现、Entity/PowerMode、TCP Routing Activation、
Alive Check、UDS 0x3E、0x10 会话切换；0x22 等服务需继续注册 DCM 实例配置。

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
# Learn-Rk3568
