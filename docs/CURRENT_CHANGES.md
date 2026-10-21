# 当前未提交改动清单

## 1. 应用工程

仓库：`rk3568-automotive-platform`

### 修改

- apps/CMakeLists.txt
- src/CMakeLists.txt
- docs/todo.md
- docs/TASK_CONVERGENCE.md

### 新增

- apps/vehicle_status_service/
- apps/vehicle_status_client/
- src/vehicle_status/
- config/vehicle_status-local.json
- docs/VehicleStatus_Contract.md
- docs/RK3568_AB_BRINGUP.md
- docs/CURRENT_CHANGES.md

### 功能变化

- 新增 VehicleStatus SOME/IP 服务/客户端
- VehicleStatusService 已接 OtaManager
- 新增 OtaVehicleStatusService
- 新增 VehicleStatus Protocol / Proxy / Skeleton
- 仍使用 MockSlotManager / MockBootController

## 2. Rockchip SDK

路径：`~/source/linux/rk356x_linux`

### 新增

- device/rockchip/rk356x/parameter-ab.txt
- u-boot/configs/rk3568_ab_defconfig

### 修改

- device/rockchip/rk356x/BoardConfig-rk3568-evb1-ddr4-v10.mk

具体改动：

- RK_UBOOT_DEFCONFIG=rk3568 -> rk3568_ab
- RK_UBOOT_DEFCONFIG_FRAGMENT 留空
- RK_PARAMETER=parameter-buildroot-fit.txt -> parameter-ab.txt

### 删除

- u-boot/configs/rk3568-ab.config（早期尝试，已不再使用）

## 3. 编译结果

已执行：

```bash
cd ~/source/linux/rk356x_linux
./build.sh uboot
```

结果：

```text
Running build_uboot succeeded.
```

应用工程已编译通过：

```bash
cmake --build build -j4
```

## 4. 还没有做的事

- 还没有重新编译 kernel / rootfs / firmware
- 还没有烧写 A/B 固件
- 还没有实现 rk_ab_metadata
- 还没有实现 RealSlotManager / RealBootController
- 还没有提交到 GitHub
