# VehicleStatus 事件/字段（Event/Field）支持

## 背景

`com` 绑定层最初只支持 method（请求/响应），没有 event/field。本次为
VehicleStatus 服务补上事件通知能力：OTA 状态变化时主动通知订阅者。

## 分层改动

### 1. com 抽象层（绑定无关）

- `src/com/include/com/types.h`
  - 新增 `EventType` 枚举（`kEvent` / `kField`）。
- `src/com/include/com/skeleton.h`
  - `BindSkeleton` 新增纯虚接口 `OfferEvent` / `StopOfferEvent` / `SendEvent`。
- `src/com/include/com/proxy.h`
  - `BindHandle` 新增纯虚接口 `SubscribeEvent` / `UnsubscribeEvent`。

### 2. vsomeip 实现层

- `src/com/src/vsomeip/vsomeip_bind_skeleton.cpp`
  - 实现 `OfferEvent`：调用 vsomeip `offer_event` 把 event 注册进 eventgroup。
  - 实现 `StopOfferEvent`：调用 vsomeip `stop_offer_event`。
  - 实现 `SendEvent`：调用 vsomeip `notify(service, instance, event, payload)`。
- `src/com/src/vsomeip/vsomeip_bind_handle.cpp`
  - 实现 `SubscribeEvent`：`request_event` + `register_message_handler` +
    `subscribe(service, instance, eventgroup, major, event)`。
  - 实现 `UnsubscribeEvent`：`unsubscribe` + `unregister_message_handler` +
    `release_event`。
- `src/com/src/vsomeip/vsomeip_bind_runtime.cpp`
  - `RegisterFindServiceHandle` 补齐 `request_service`（缺它会收不到
    availability 通知，find 回调永不触发）。

### 3. 协议层

- `src/vehicle_status/vehicle_status_protocol.h/.cpp`
  - 新增 `kEventGroupId = 0x0001`、`kEventOtaStateChanged = 0x8001`。
  - 新增 `AppendOtaState` / `ReadOtaState`（u8 state + u32 percent +
    string detail）。

### 4. OTA 层

- `src/ota/ota_manager.h/.cpp`
  - 新增 `ProgressCallback` + `SetProgressCallback`，在
    `set_progress_locked` 末尾触发，实现状态变化即通知。

### 5. VehicleStatus 服务/客户端

- `src/vehicle_status/vehicle_status_service_skeleton.h/.cpp`
  - `RegisterEvent`：对每个 BindSkeleton 调 `OfferEvent`。
  - `NotifyState`：序列化 OTA 状态并 `SendEvent`。
  - `Register` 中保存 BindSkeleton 引用。
- `src/vehicle_status/ota_vehicle_status_service.h`
  - `MapState` 改为 public static，供 main 桥接回调。
- `apps/vehicle_status_service/main.cpp`
  - `manager.SetProgressCallback` 桥接到 `skeleton.NotifyState`。
- `src/vehicle_status/vehicle_status_proxy.h/.cpp`
  - 新增 `SubscribeOtaStateChanged`，内部解析通知 payload 后回调。
- `apps/vehicle_status_client/main.cpp`
  - 新增订阅流程：订阅 → 等 ACK → StartUpdate → 收通知。

### 6. 配置

- `config/vehicle_status-remote.json`
  - service 增加 `events`（0x8001）与 `eventgroups`（0x0001）。
- `config/vehicle_status-client-local.json`
  - 客户端配置，`service-discovery.ttl` 设为 60（订阅有效期）。

## 验证结果

板子 mock 模式（64KB 测试镜像）双进程验证，客户端收到 5 条状态通知：

```text
[client] <- NOTIFY OtaStateChanged state=2 percent=1  detail=checking source image
[client] <- NOTIFY OtaStateChanged state=1 percent=15 detail=copying image to staging
[client] <- NOTIFY OtaStateChanged state=2 percent=55 detail=verifying image digest
[client] <- NOTIFY OtaStateChanged state=3 percent=75 detail=writing image to inactive slot
[client] <- NOTIFY OtaStateChanged state=4 percent=90 detail=ready to switch
[client] received 5 notifications
```

## 关键坑

1. `SendEvent` 必须用 vsomeip 的 `notify()` 而不是
   `create_notification()` + `send()`。后者不走事件订阅分发，notification
   会被 provider 自己的 `application_impl::on_message` 拦下，报
   `blocked as the subscription is already inactive`。
2. `RegisterFindServiceHandle` 必须先 `request_service`，否则 vsomeip 不
   通知 availability，find 回调永不触发。
3. `subscribe` 要传具体 event id（而非默认 `ANY_EVENT`），否则客户端本地
   订阅表存的是 `ANY_EVENT`，通知检查匹配不上。
4. 客户端配置的 `service-discovery.ttl` 若为 0，订阅会被 vsomeip 判为立即
   到期（inactive）。
