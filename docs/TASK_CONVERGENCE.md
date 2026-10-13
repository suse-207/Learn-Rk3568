# 首版任务收敛

> 依据：`docs/todo.md`
> 目标：先收敛到一条可演示、可验收的主链路，不再并行扩展未验证能力。

## 1. 首版冻结范围

首版只做：

```text
DoIP/UDS
  - 车辆发现
  - 诊断连接
  - UDS 0x10
  - UDS 0x22 F190

SOME/IP
  - 服务发现
  - 车辆状态查询
  - 最小 OTA 控制
```

首版不做：

```text
DDS
完整 UCM 规范
UDS 0x34/0x36/0x37 作为首版验收主链路
```

> 说明：代码里已经存在 UDS Download、UCM 17 方法和 OTA Mock，但首版验收不把它们作为主线完成门槛。

## 2. 当前状态盘点

| 能力 | 状态 | 证据 |
| --- | --- | --- |
| 自研 DoIP transport | 代码存在，本机验证 | `apps/diag_server`、`src/doip` |
| CAPI UDS 接入 | 代码存在，本机验证 | `src/uds/capi`、`src/diag` |
| UDS 0x10 | 本机验证 | DoIP 测试：`0x10 01 -> 0x50 01` |
| UDS 0x22 F190 | 本机验证 | DoIP 测试：读取 17 字节 VIN |
| Functional addressing 0x7DF | 本机验证 | 自研 DoIP 路由测试通过 |
| SOME/IP 服务发现 | 代码存在，本机验证 | `apps/someip_server`、`apps/someip_client` |
| 车辆状态查询 | 未收敛 | 当前只有版本服务，没有统一 VehicleStatus 契约 |
| 最小 OTA 控制接口 | 部分存在 | `src/ota/ucm_service.h` 有 17 个 UCM 方法，但未冻结首版方法集 |
| OTA Core | Mock 验证 | `src/ota/ota_manager.*`、`apps/ota_demo` |
| 真实镜像写入 inactive slot | 未验证 | 当前是 MockSlotManager |
| 真实启动切换 | 未验证 | 当前是 MockBootController |
| 真实启动失败回滚 | 未验证 | `ota_demo --fail-boot` 仅模拟 |
| RK3568 板级环境 | 未盘点 | 型号、分区、U-Boot、启动链路未确认 |

## 3. 收敛后的执行顺序

### P0：先冻结，不写代码

1. 冻结首版验收命令：

```text
DoIP:
  Vehicle Identification
  Routing Activation
  UDS 0x10
  UDS 0x22 F190

SOME/IP:
  Service Discovery
  VehicleStatus.GetStatus

OTA:
  VehicleStatus.StartUpdate
  VehicleStatus.GetProgress
  VehicleStatus.Rollback
```

2. 统一 VehicleStatus 契约：

```text
Service: VehicleStatus
Method: GetStatus
Method: StartUpdate
Method: GetProgress
Method: Rollback

字段至少包含：
  version
  current_slot
  ota_state
  progress
  error_code
```

3. 盘点 RK3568 升级环境，未确认前不写真实分区。

### P1：打通真实主链

按依赖顺序做：

1. PC 通过 DoIP/UDS 读取 F190，留存命令、日志、pcap。
2. 冻结 SOME/IP VehicleStatus 接口，服务端返回版本、slot、OTA 状态。
3. 最小 OTA 控制从 PC 通过 SOME/IP 调通。
4. 下载镜像到 staging，校验大小和 SHA-256。
5. 写入 inactive slot，失败不得切换启动目标。
6. 切换启动目标并 reboot，启动后由确认机制标记成功。
7. 注入一次启动失败，验证回滚到旧 slot。

### P2：形成交付物

1. 验收脚本。
2. 日志、pcap、镜像校验、slot 状态等证据。
3. 更新 README，区分 Mock 与真实板端能力。
4. 面试演示材料。

## 4. 关键约束

- A/B 或 slot 方案未冻结前，不允许写真实 OTA。
- 真实切换必须优先于真实写入验证。
- Mock 可以继续存在，但在 README 和验收报告里必须明确标注。
- 当前 `diag_server` 已收敛 DoIP + CAPI UDS，不要再回到两条诊断链路。

## 5. 下一步最小动作

```text
1. 更新 VehicleStatus SOME/IP 契约并冻结
2. 在 RK3568 上先跑通 DoIP/UDS 读 F190
3. 在 RK3568 上确认分区、U-Boot、启动切换方式
4. 之后才允许接入真实 slot 写入与切换
