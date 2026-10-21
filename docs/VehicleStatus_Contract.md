# VehicleStatus SOME/IP 契约 v0.1

> 状态：首版冻结
> 目标：统一车辆状态查询和最小 OTA 控制接口
> 传输：SOME/IP over vsomeip3

## 1. 基本信息

| 项 | 值 |
| --- | --- |
| Service ID | 0x6100 |
| Instance ID | 0x0001 |
| Major Version | 1 |
| Minor Version | 0 |
| 字节序 | big-endian |
| 字符串 | u32 长度 + UTF-8 bytes |
| 枚举 | u8 |

## 2. 错误码

所有响应第一个字段是 `error_code`：

```text
0x00 SUCCESS
0x01 INVALID_ARGUMENT
0x02 NOT_FOUND
0x03 BUSY
0x04 INTERNAL_ERROR
0x05 NOT_READY
0x06 VERIFY_FAILED
0x07 INSTALL_FAILED
0x08 ROLLBACK_FAILED
```

类型：`u32`

## 3. OTA 状态枚举

```text
0x00 IDLE
0x01 DOWNLOADING
0x02 VERIFYING
0x03 INSTALLING
0x04 READY_TO_SWITCH
0x05 REBOOTING
0x06 SUCCESS
0x07 FAILED
0x08 ROLLBACK
```

类型：`u8`

## 4. 方法

### 4.1 GetStatus

请求：

```text
无
```

响应：

```text
u32 error_code
u32 version_len
u8[version_len] version
u32 current_slot_len
u8[current_slot_len] current_slot
u8 ota_state
```

语义：

```text
读取车辆版本、当前启动 slot 和 OTA 状态。
```

### 4.2 StartUpdate

请求：

```text
u32 version_len
u8[version_len] version
u64 package_size
u32 sha256_len
u8[sha256_len] sha256
```

响应：

```text
u32 error_code
u32 update_id_len
u8[update_id_len] update_id
```

语义：

```text
发起一次 OTA 更新。
package_size 是待写入 inactive slot 的镜像大小。
sha256 是镜像摘要。
成功返回 update_id，后续用 GetProgress 查询进度。
```

### 4.3 GetProgress

请求：

```text
u32 update_id_len
u8[update_id_len] update_id
```

响应：

```text
u32 error_code
u8 ota_state
u32 percent
u32 detail_len
u8[detail_len] detail
```

语义：

```text
查询指定 update_id 的当前进度。
```

### 4.4 Rollback

请求：

```text
无
```

响应：

```text
u32 error_code
```

语义：

```text
回滚到上一个可用 slot。
```

## 5. 预留事件

当前不实现，但保留 ID：

```text
Event: ota_state_changed
Event ID: 0x8001
```

后续 OTA 状态变化时广播：

```text
u8 ota_state
u32 percent
```

## 6. 首版验收命令

PC 端需要能完成：

```text
Service Discovery
VehicleStatus.GetStatus
VehicleStatus.StartUpdate
VehicleStatus.GetProgress
VehicleStatus.Rollback
```

对应 SOME/IP method ID：

```text
GetStatus    0x0001
StartUpdate  0x0002
GetProgress  0x0003
Rollback     0x0004
```

## 7. 当前实现状态

| 能力 | 状态 |
| --- | --- |
| 契约冻结 | 完成 |
| 代码生成 / 实现 | 未开始 |
| PC 验证 | 未开始 |
