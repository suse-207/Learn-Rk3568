# UCM 风格 OTA 升级系统 — 详细设计文档

> 本文档描述本仓库实现的一套「对齐 AUTOSAR AP `ara::ucm` 语义」的 OTA 升级 demo。
> 参考实现：`D:\开源库学习\capi-1.0.0` 中的 `ucm` 功能簇（`pkgmgr` / `vpkgmgr` / `ucmlibs`）及其 `package_management` 服务接口 arxml。

---

## 1. 背景与目标

目标是在 RK3568（Linux）上模拟 AP AUTOSAR 的 **SOTA/FOTA 软件升级**，并尽可能对齐 AUTOSAR 标准里的 **UCM（Update and Configuration Management）** 功能簇语义，而不是做成一个「只刷字节」的简单下载器。

核心对齐点（均已核对 CAPI 源码/arxml 后落实）：

- **下载方式**：UCM 的软件包通过 `TransferStart/TransferData/TransferExit` 走 **SOME-IP**（`ara::com`）分块传输，落到本地成 `.zip`，再由 `ProcessSwPackage` 处理。
- **服务接口**：`PackageManagement` 共 **17 个方法**，本 demo 全部实现。
- **错误模型**：单一 `UcmErrc` 错误码枚举（对齐 CAPI 的 `UCMErrorDomainErrc`）。
- **软件包**：带 `manifest` + HMAC 签名，`ProcessSwPackage` 解析并验签。
- **传输健壮性**：磁盘空间检查 + 会话状态持久化（断点续传）。

### 三种「下载」的准确关系

这三者不在同一层，不能简单二选一：

```
【车外】云 ──HTTP(S)──► Update Agent 拉包落盘          ← 只有 HTTP，车外那一跳
                        │
【车内】                │  ← 这里才是二选一
      ├─ A：TransferData（SOME-IP）──► 目标 UCM        （AP 原生 OTA）
      └─ B：UDS 0x34/36/37（DoIP）──► 诊断刷写         （诊断刷写）
```

- **HTTP**：互联网通用协议，典型用于「云→车」，本 demo 用 libcurl 实现。
- **UCM TransferData**：AP 原生，走 `ara::com`（默认 SOME-IP 绑定），本 demo 用自研 `com` 层承载。
- **UDS 0x34/36/37**：诊断刷写，走 DoIP（也是以太网），仓库里 `src/uds` 已有，本 demo 未接入。

---

## 2. 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        Update Agent（客户端）                       │
│   HttpDownloader(libcurl) → 打包(manifest+HMAC) → UcmProxy(SOME-IP) │
└───────────────────────────────┬──────────────────────────────────┘
                                │  TransferStart/Data/Exit/ProcessSwPackage
                                │  （SOME-IP，经 com 层）
┌───────────────────────────────▼──────────────────────────────────┐
│                      目标 UCM（服务端）                              │
│   UcmServiceSkeleton(com) → UcmService(17方法)                      │
│     ├─ 收字节写 .zip（TransferInstance 语义）                        │
│     ├─ ProcessSwPackage：解包→验签→版本/依赖检查→SHA256              │
│     └─ 下发给 OtaManager                                             │
└───────────────────────────────┬──────────────────────────────────┘
                                │  start_update / switch / rollback
┌───────────────────────────────▼──────────────────────────────────┐
│                     OTA 编排层（OtaManager）                        │
│   Checking→Downloading→Verifying→Installing→ReadyToSwitch          │
│   + SlotManager(A/B) + BootController + VersionManager + Verify    │
└──────────────────────────────────────────────────────────────────┘
```

进程内直调版本（`ucm_ota_demo`）省去 SOME-IP 这一跳，`UpdateAgent` 直接调用 `UcmService`，用于无 vsomeip 环境下快速验证 UCM 业务逻辑。

---

## 3. 完整数据流（SOME-IP 版本）

```
Update Agent(ucm_client)                        目标 UCM(ucm_service)
──────────────────────                          ─────────────────────
1. HTTP 下载 payload（libcurl）
2. 读 payload → 算 HMAC 签名
3. 构造 manifest{name,version,deps,sig}
4. BuildPackage(manifest + payload)
5. TransferStart(size)  ────SOME-IP────►  建会话 + 磁盘检查 + 建 .zip/.meta
6. TransferData(block)   ────SOME-IP────►  校验块号 → append 写 .zip → 更新 .meta
   ... 分块循环 ...
7. TransferExit          ────SOME-IP────►  校验字节数
8. ProcessSwPackage(id)  ────SOME-IP────►  解包 → 验签 → 版本/依赖检查
                                           → 写 payload.img → SHA256
                                           → OtaManager.start_update
                                           
9.（服务端后台）OtaManager:
   Checking → Downloading(拷贝staging) → Verifying(SHA256)
   → Installing(写非活跃槽) → ReadyToSwitch
10. Activate()           ────SOME-IP────►  switch_and_reboot()（boot 切槽 + 重启）
11. Finish()             ────SOME-IP────►  report_boot_result(true)（确认成功）
    （失败则 Rollback() 回滚）
```

---

## 4. 模块详解

### 4.1 OTA 编排层（`src/ota/`）

| 文件 | 类/职责 |
|---|---|
| `ota_manager.h/.cpp` | `OtaManager`：升级状态机 + 异步 worker。`start_update(path, digest, version)` / `switch_and_reboot()` / `report_boot_result()` / `rollback()` / `progress()` |
| `ota_types.h/.cpp` | `State` 枚举（Idle/Checking/Downloading/Verifying/Installing/ReadyToSwitch/Rebooting/BootVerify/Success/Failed/Rollback）+ `Progress` |
| `slot_manager.h/.cpp` | `SlotManager`（抽象）+ `MockSlotManager`（A/B 槽位，目录模拟） |
| `boot_controller.h/.cpp` | `BootController`（抽象）+ `MockBootController`（boot 槽选择/重启/结果记录） |
| `version_manager.h/.cpp` | `VersionManager`：当前/目标版本 + 升级历史 |
| `verify_strategy.h/.cpp` | `VerifyStrategy`（SHA256 摘要计算/校验）+ `make_verify_strategy` 工厂 |

### 4.2 UCM 服务层（`src/ota/`）

| 文件 | 职责 |
|---|---|
| `ucm_types.h` | `UcmErrc` 错误码枚举 + 数据结构（`SwPackageInfo`/`SwClusterInfo`/`HistoryEntry`/`ProgressInfo`） |
| `ucm_service.h/.cpp` | `UcmService`：目标 UCM，17 个方法，接收传输字节、处理包、暴露生命周期与查询 |
| `package_manifest.h/.cpp` | `PackageManifest` 结构 + `BuildPackage`/`UnpackPackage` + `HmacSha256Hex`（OpenSSL HMAC） |

### 4.3 Update Agent（`src/ota/`）

| 文件 | 职责 |
|---|---|
| `http_downloader.h/.cpp` | `HttpDownloader`：libcurl 下载（`Download(url, path, error)`） |
| `update_agent.h/.cpp` | `UpdateAgent`：HTTP 下载 → 打包签名 → 分块 TransferData → ProcessSwPackage |

### 4.4 SOME-IP 传输适配层（`src/ucm/`）

| 文件 | 职责 |
|---|---|
| `ucm_protocol.h` | 17 个 SOME-IP method ID + 二进制序列化工具（header-only） |
| `ucm_service_skeleton.h/.cpp` | `UcmServiceSkeleton`：把 `UcmService` 挂到 com 骨架，注册 17 个 handler |
| `ucm_proxy.h/.cpp` | `UcmProxy`：客户端代理，序列化调用 `SendRequest` |

### 4.5 com 层（`src/com/`）

自研 `ara::com` 风格通信抽象（详见 `src/com/README.md`）：`Runtime` 单例、`BindRuntime`/`BindHandle`/`BindSkeleton` 抽象，vsomeip3 绑定实现。UCM 的 `TransferData` 即通过其 `SendRequest(methodId, data)` ↔ `RegisterMethodHandler(methodId, handler)` 承载 SOME-IP 通信。

---

## 5. UCM `PackageManagement` 17 方法接口

| # | 方法 | 请求 | 返回 | 映射到 |
|---|---|---|---|---|
| 1 | `TransferStart` | size | id + blockSize | 建会话/建文件 |
| 2 | `TransferData` | id + block + counter | errc | 收字节写 .zip |
| 3 | `TransferExit` | id | errc | 结束传输 |
| 4 | `ProcessSwPackage` | id | errc | 解包/验签/检查 → OtaManager |
| 5 | `DeleteTransfer` | id | errc | 删传输会话 |
| 6 | `Activate` | — | errc | `switch_and_reboot` |
| 7 | `Finish` | — | errc | `report_boot_result(true)` |
| 8 | `Cancel` | id | errc | 丢弃传输 |
| 9 | `Rollback` | — | errc | `rollback` |
| 10 | `RevertProcessedSwPackages` | — | errc | 占位（demo 空操作） |
| 11 | `GetSwPackages` | — | 包列表 | `VersionManager` |
| 12 | `GetSwClusterInfo` | — | 簇列表 | `VersionManager` |
| 13 | `GetSwClusterChangeInfo` | — | 变更列表 | `VersionManager` |
| 14 | `GetSwClusterDescription` | name | 描述 | 占位 |
| 15 | `GetSwProcessProgress` | id | 进度 | `OtaManager::progress` |
| 16 | `GetHistory` | — | 历史 | `VersionManager::history` |
| 17 | `GetId` | — | id | UCM 实例标识 |

SOME-IP method ID 分配：`0x0001` ~ `0x0011`（见 `ucm_protocol.h`）。

---

## 6. 错误码（`UcmErrc`）

单一枚举，枚举值即 SOME-IP 线上状态码（对齐 CAPI `UCMErrorDomainErrc`）：

| 值 | 名称 | 触发场景 |
|---|---|---|
| 0 | `kSuccess` | 成功 |
| 1 | `kInvalidTransferId` | transfer id 不存在 |
| 2 | `kInvalidPackageManifest` | manifest 缺失/解析失败 |
| 3 | `kAuthenticationFailed` | HMAC 验签失败 |
| 4 | `kInsufficientMemory` | 磁盘不足/文件失败 |
| 5 | `kOperationNotPermitted` | 状态不允许 |
| 6 | `kServiceBusy` | OTA 忙（重复 start_update） |
| 7 | `kIncompatibleDelta` | 预留 |
| 8 | `kProcessedSoftwarePackageInconsistent` | 预留 |
| 9 | `kProcessSwPackageCancelled` | 预留 |
| 10 | `kSoftwareClusterMissing` | 预留 |
| 11 | `kIncompatiblePackageVersion` | 目标版本 == 当前版本 |
| 12 | `kMissingDependencies` | 依赖不满足 |
| 13 | `kOldVersion` | 预留 |
| 14 | `kPackageInconsistent` | 预留 |
| 15 | `kIncorrectBlock` | 块序号错误 |
| 16 | `kIncorrectBlockSize` | 预留 |
| 17 | `kBlockInconsistent` | 预留 |
| 18 | `kIncorrectSize` | 传输字节数超限 |
| 19 | `kInsufficientData` | 退出时字节不足 |
| 20 | `kNothingToRollback` | 预留 |
| 21 | `kNotAbleToRollback` | 预留 |
| 22 | `kNothingToRevert` | 预留 |
| 23 | `kNotAbleToRevertPackages` | 预留 |

> 「预留」的错误码枚举已定义、线上可携带，但 demo 尚未触发（等完整状态机/manifest 校验落地）。

---

## 7. 软件包 manifest + 签名

**包格式**（demo 简化版，真实 UCM 用 `.zip` + JSON manifest）：

```
[u32 manifest_len][manifest blob][payload]
```

**manifest blob**（二进制编码，字段见 `package_manifest.h`）：

```
[name][version][dep_count][dep1][dep2]...[signature]
```

**签名**：`signature = HMAC-SHA256(payload, demo-ota-secret)`，hex 编码。

**`ProcessSwPackage` 校验流程**：

1. `UnpackPackage` 解出 manifest + payload（失败 → `kInvalidPackageManifest`）
2. `HmacSha256Hex(payload) == manifest.signature`（失败 → `kAuthenticationFailed`）
3. `manifest.version == 当前版本`（→ `kIncompatiblePackageVersion`）
4. 每个依赖 `== 当前版本`（否则 → `kMissingDependencies`）
5. 写 payload 到 `.img` → `VerifyStrategy::digest` 算 SHA256
6. `OtaManager::start_update(payload.img, digest, version)`

---

## 8. 传输健壮性

- **磁盘空间检查**：`TransferStart` 用 `statvfs(f_bavail * f_frsize)` 检查剩余空间，不足 → `kInsufficientMemory`。
- **会话状态持久化**：每个 transfer 的 `{expected_bytes, received_bytes, expected_block}` 写到 `<transferDir>/<id>.meta`，每次 `TransferData` 后更新。
- **断点续传**：`TransferData` 若内存里无此会话，则从 `.meta` load-on-demand 恢复（模拟重启后续传）。
- **文件清理**：`DeleteTransfer`/`ProcessSwPackage` 后删除 `.zip` + `.meta`。

> 对齐 CAPI `TransferInstance`：`GetFreeDiskSpace` ↔ `statvfs`；`TransferStatusStorage::StoreStatus/GetStatus` ↔ `SaveSession/LoadSession`。

---

## 9. 与 CAPI `ara::ucm` 的对应关系

| CAPI 组件 | 本 demo 实现 |
|---|---|
| `package_management` 服务接口（SOME-IP） | `ucm_service_skeleton` / `ucm_proxy` + `ucm_protocol` |
| `TransferInstance`（收字节写 .zip、磁盘检查、持久化） | `UcmService::TransferStart/Data/Exit` |
| `ProcessSwPackage`（校验 manifest/签名/版本/依赖） | `UcmService::ProcessSwPackage` |
| `UCMErrorDomainErrc` | `UcmErrc` |
| `data_transfer`（Update Agent 推字节侧） | `UpdateAgent` + `UcmProxy` |
| `Activate/Finish/Rollback` | `OtaManager::switch_and_reboot/report_boot_result/rollback` |
| manifest 解析 + 验签（`software_package.cpp`/`signature_check.cpp`） | `package_manifest` + HMAC |

---

## 10. 构建与运行

### 依赖

- CMake ≥ 3.16，C++17
- vsomeip3（`third_party/vsomeip`，由根 CMake `WITH_VSOMEIP=ON` 构建）
- libcurl（`find_package(CURL)`）
- OpenSSL（HMAC/SHA256）
- Threads

### 构建

```bash
cmake -B build -DWITH_VSOMEIP=ON -DCOM_BUILD_EXAMPLES=ON
cmake --build build
```

### 运行（SOME-IP 版，两进程）

```bash
# 起 HTTP 服务放测试镜像
python -m http.server 8000

# 终端1：目标 UCM（服务端）
./build/apps/ucm_service

# 终端2：Update Agent（客户端）
./build/apps/ucm_client http://127.0.0.1:8000/package.img 1.1.0
```

### 运行（进程内直调版，无 vsomeip）

```bash
./build/apps/ucm_ota_demo http://127.0.0.1:8000/package.img 1.1.0
```

---

## 11. Demo 简化与生产差异（诚实清单）

| 项 | demo | 生产（CAPI/AP AUTOSAR） |
|---|---|---|
| 软件包容器 | 二进制 bundle | `.zip`（zlib 解压，含多文件） |
| manifest 格式 | 二进制 blob | JSON（`rjson`） |
| 签名 | HMAC-SHA256 + 硬编码密钥 | 证书链 + 验签（`SignatureCheck`） |
| 持久化 | 每会话一个 `.meta` 文件 | KVS/persistency 库 |
| 依赖检查 | 「dep == 当前版本」朴素逻辑 | 依赖图 + 版本解析 |
| 断点续传 | 服务端 load-on-demand | 客户端也持久化块号并重发 |
| 序列化 | 手写二进制 | AUTOSAR 生成的 typed proxy/skeleton |
| campaign/多 ECU | 无 | `vpkgmgr` FSM + RolloutStep 多 ECU 协同 |
| 进程模型 | 两个 demo 进程 | AP 执行管理（EM）+ 状态管理（SM）集成 |

---

## 12. 面试要点（一句话口径）

> 我按 AP AUTOSAR 的 UCM 模型实现 OTA：Update Agent 用 libcurl 从云 HTTPS 拉包、算 HMAC 签名并打包；通过 SOME-IP 的 `TransferStart/TransferData/TransferExit` 把包分块推给目标 UCM，UCM 收字节写 `.zip`（带磁盘检查 + 断点续传持久化）；`ProcessSwPackage` 解包验签、检查版本/依赖、算 SHA256，交给 OtaManager 走「校验 → 装非活跃槽 → A/B 切换 → 失败回滚」。接口对齐了 `ara::ucm` 的 17 方法 + 单一错误域，传输层同时支持进程内直调和 SOME-IP。
