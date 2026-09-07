# M6 OTA Manager 验证记录

## 目标

在 x86 Ubuntu 上用 Mock A/B Slot 和 Mock Boot Controller 跑通 OTA 状态机，为后续 RK3568 真实 A/B 切换提供稳定的业务层骨架。

## 状态机

```text
IDLE
  -> CHECKING
  -> DOWNLOADING
  -> VERIFYING
  -> INSTALLING
  -> READY_TO_SWITCH
  -> REBOOTING
  -> BOOT_VERIFY
  -> SUCCESS | ROLLBACK
```

## 模块

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| OtaManager | `src/ota/ota_manager.*` | 固定升级流程，异步 worker + 进度 |
| VerifyStrategy | `src/ota/verify_strategy.*` | SHA-256 / CRC32 策略 |
| SlotManager | `src/ota/slot_manager.*` | Mock A/B slot 写入与切换 |
| BootController | `src/ota/boot_controller.*` | Mock 下一次启动 slot 与启动结果 |
| VersionManager | `src/ota/version_manager.*` | 当前版本 / 目标版本 / 历史记录 |

## 验证命令

```bash
cd ~/rk3568-automotive-platform
/home/topeet/Desktop/cmake-3.28.6-linux-x86_64/bin/cmake --build build --target ota_demo -- -j4

# A -> B 正常升级
./build/apps/ota_demo

# 模拟 B 启动失败，回滚 A
./build/apps/ota_demo --fail-boot
```

## 验证结果

- 正常路径：`current_slot=A -> B`，版本 `1.1.0`，历史记录 `SUCCESS`。
- 故障路径：`current_slot` 恢复为 `A`，目标版本仍为 `1.1.0`，历史记录 `ROLLBACK`。

