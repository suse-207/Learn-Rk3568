# RK3568 Linux A/B 接入方案

> 目标：从当前单 rootfs 固件切换到 Rockchip Linux A/B System。

## 1. 当前状态

```text
by-name:
  uboot
  misc
  boot
  recovery
  backup
  rootfs
  oem
  userdata

misc A/B metadata:
  存在，但当前 bootloader 未按 A/B 启动

/proc/cmdline:
  无 androidboot.slot_suffix
```

结论：当前固件是单 slot，需要重新生成 A/B 固件。

## 2. 已创建文件

SDK 路径：

```text
~/source/linux/rk356x_linux
```

新增：

```text
device/rockchip/rk356x/parameter-ab.txt
u-boot/configs/rk3568-ab.config
```

### parameter-ab.txt 分区规划

```text
uboot
misc
boot_a
boot_b
backup
rootfs_a
rootfs_b
oem
userdata
```

其中：

```text
rootfs_a = 原 rootfs 位置
rootfs_b = 新增第二 rootfs
userdata = 剩余空间
```

## 3. U-Boot 配置

新增 config fragment：

```text
u-boot/configs/rk3568-ab.config
```

内容：

```text
CONFIG_ANDROID_AB=y
```

需要合并进 defconfig 后重新编译 U-Boot。

## 4. 固件构建

使用 Rockchip SDK 构建脚本，并把 parameter 指向 A/B 文件。

具体命令以讯为/Rockchip SDK 文档为准，一般包括：

```bash
cd ~/source/linux/rk356x_linux
./build.sh uboot
./build.sh kernel
./build.sh rootfs
./build.sh firmware
```

烧写时选择 A/B parameter。

## 5. Bootloader 启动参数

当前启动参数固定：

```text
root=PARTUUID=614e0000-0000
```

A/B 启动需要根据 slot 选择：

```text
slot A -> rootfs_a
slot B -> rootfs_b
```

Bootloader 或 boot script 需要传递：

```text
root=PARTUUID=614e0000-0000   # slot A
root=PARTUUID=614e0001-0000   # slot B
```

## 6. 应用侧需要实现

### 6.1 misc A/B 元数据读写

```text
src/ota/rk_ab_metadata.h/.cpp
```

实现：

```text
ReadAbMetadata
WriteAbMetadata
MarkSlotSuccessful
SetActiveSlot
GetLastBootSlot
```

### 6.2 RealSlotManager

```text
src/ota/real_slot_manager.h/.cpp
```

实现 `ota::SlotManager`：

```text
current_slot
inactive_slot
write_image_to_inactive
activate_inactive
rollback_to_last_good
is_inactive_valid
```

### 6.3 RealBootController

```text
src/ota/real_boot_controller.h/.cpp
```

实现 `ota::BootController`：

```text
set_next_slot
reboot
mark_boot_success
mark_boot_failed
```

## 7. 验收顺序

```text
1. 重新生成 A/B 固件
2. 烧写 slot A 和 slot B
3. 验证手动切换 A/B
4. 验证启动失败回滚
5. 再接入真实镜像写入
6. 最后接 VehicleStatus / OtaManager
