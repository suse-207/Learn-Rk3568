# COM 层 vsomeip 绑定：并发请求缺陷与修正方案

状态：待评审
日期：2026-11-20
范围：`src/com/src/vsomeip/**`、`src/com/include/com/vsomeip/**`、新增 `tests/com_concurrency/**`
公共签名保持兼容，不改 `apps/`，不改 `third_party/vsomeip/**`

---

## 1. 架构

### 1.1 分层

```text
┌────────────────────────────────────────────────────────────────────────────┐
│ 应用层                                                                     │
│   VehicleStatus   src/vehicle_status/  (ota_vehicle_status_service)        │
│   UCM             src/ucm/                                                 │
│   OTA             src/ota/  (ota_manager / slot_manager / version_manager) │
│   DoIP / UDS      src/doip/ src/uds/   ── 诊断链路，独立于 COM             │
└───────────────────────────────────┬────────────────────────────────────────┘
                                    │ 只用 com:: 公共接口，不 include vsomeip 类型
┌───────────────────────────────────▼────────────────────────────────────────┐
│ COM 公共 API                     src/com/include/com/                      │
│   Runtime                         绑定注册/查找、Init/Deinit/Start/Stop     │
│   proxy::ServiceProxy             服务可用性、SendRequest / …Async          │
│   proxy::BindHandle               每个 binding 一个句柄（vsomeip 下        │
│                                   VsomeipBindHandle）                      │
│   skeleton::ServiceSkeleton       方法注册、应答发送                        │
│   skeleton::BindSkeleton          绑定层服务端句柄                          │
│   基础设施                          types.h / com_error_domain.h /          │
│                                   ara::core Future·Promise                 │
└───────────────────────────────────┬────────────────────────────────────────┘
                                    │ 绑定抽象：BindRuntime / BindHandle / BindSkeleton
┌───────────────────────────────────▼────────────────────────────────────────┐
│ Communication Core（协议无关语义）                                          │
│   Discovery        服务上下线、可用性代际、WaitForAvailable                 │
│   请求分发  ★P0    稳定 handler、在途请求表(session)、超时、迟到丢弃        │
│   事件/字段        订阅、通知（当前只在 vsomeip 绑定层实现）                │
│   生命周期         Init/Deinit/Start/Stop、句柄与 runtime 的销毁顺序        │
│   执行器  ★P2      服务端业务移出 io 线程（有界队列 + worker）              │
│   错误策略         ComErrc 到协议错误的单点映射                             │
└───────────────────────────────────┬────────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────────┐
│ Binding Runtime                  src/com/src/vsomeip/                      │
│   VsomeipBindRuntime              持有 vsomeip::application（每 runtime 一个）│
│   VsomeipBindHandle               客户端：请求、订阅                        │
│   VsomeipBindSkeleton             服务端：方法注册、应答                    │
│   规划中                          DDS 绑定、共享内存绑定（共用同一 Core）    │
└───────────────────────────────────┬────────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────────┐
│ Platform / Network                                                         │
│   vsomeip 3.3.0                   routing manager、SD、SOME/IP 编解码      │
│   Linux                           socket / epoll / 组播 224.244.224.245:30490│
│   third_party/vsomeip             只读依赖，不修改                          │
└────────────────────────────────────────────────────────────────────────────┘
```

★ 标出的是本次改造落点（P0 请求分发）；P2 执行器是后续阶段。

### 1.2 分层职责

| 层 | 负责 | 不负责 |
| --- | --- | --- |
| 应用层 | 业务语义（OTA 状态、UCM 升级流程） | 任何 SOME/IP 编解码、routing 配置 |
| COM 公共 API | 稳定的、协议无关的调用面（proxy/skeleton/类型/错误） | 绑定细节、线程模型 |
| Communication Core | 跨绑定语义：发现、在途请求、超时、生命周期、错误映射 | 具体报文构造、socket |
| Binding Runtime | 协议适配：application 生命周期、handler 注册、消息编解码 | 公共 API 语义 |
| Platform | vsomeip/SD/socket | 业务与 API |

### 1.3 三条边界纪律

1. 应用只依赖 `com::` 公共 API，不 include vsomeip 类型；
2. 协议细节只出现在 binding 层；
3. DoIP/UDS 诊断链路独立，不并入 vsomeip 的事件循环。

### 1.4 分期与本文范围

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| P0 | 请求分发：稳定 handler + 在途请求表 + 超时 + 迟到丢弃 | **本文范围**，缺陷见第 2 节 |
| P1 | 绑定无关的服务发现与生命周期 | 未实现，另立文档 |
| P2 | 服务端执行器（业务移出 io 线程） | 未实现，另立文档 |
| P3 | Runtime 启停/状态与公共 API 语义 | 部分实现（`Runtime::Init()` 为空、绑定重名无检查），另立文档 |

### 1.5 目标架构（按最佳标准）

参考对象与取舍：

| 参照 | 拿它的什么 | 不拿什么 |
| --- | --- | --- |
| AUTOSAR AP / ara::com（本机 `~/capi` 是完整实现） | 语义与契约：方法 + 事件双模型、`Result`/错误域、显式生命周期 | 整套 AP 栈（exec/per/phm/sm，与通信层无关且体量过大） |
| CommonAPI（本机 `~/commonapi-stack` 3.2 + vsomeip binding） | 结构：IDL 单一真源、生成 proxy/stub、**绑定可替换**、超时是调用参数 | Franca 工具链（会与已有 `tools/idlc.py` 形成两套 IDL） |

四条硬约束：

1. **com 不拥有 loop/线程**。loop 属于 binding（vsomeip io）或应用；库内不建定时器、不起工作线程。
2. **超时归调用方**。同步请求由调用线程按 `Duration` 等待；异步请求在“无人驱动 loop”的前提下**不承诺超时**，只承诺响应到达或 `Stop()` 时结算。
3. **公共 API 协议无关**。只暴露 service/method/event + `Result`/`Future`；`BindHandle`、`BindSkeleton`、session 等协议概念一律留在 binding 层。
4. **绑定 SPI 可替换**。上层只依赖 `IBindingRuntime` / `IBindingHandle` / `IBindingSkeleton` 三个接口，vsomeip 只是其中一个实现。

目标目录与职责：

```
src/com/
  api/        ServiceProxy / ServiceSkeleton / Result / ErrorDomain / Types
  core/       Discovery（事件驱动、可用性代际）/ Lifecycle（状态机、幂等）/ ErrorPolicy
              —— 不含线程、不含 loop、不含协议细节
  binding/    IBindingRuntime / IBindingHandle / IBindingSkeleton        （SPI）
    vsomeip/  VsomeipBindRuntime / VsomeipBindHandle / VsomeipBindSkeleton
              + 请求分发（session 匹配、墓碑、handler 注册）—— 协议相关，属于这里
  gen/        idlc 产物：proxy / skeleton / codec
tests/
  conformance/   同一套契约用例，换绑定只换编译目标
  concurrency/   同方法并发、跨句柄、异步、停机（现有六场景）
```

目标 API 形态（关键：超时在签名里，不在库里）：

```cpp
Result<void>    Init();                                             // 幂等
Result<Payload> SendRequest(MethodId, Payload, Duration);           // 调用线程拥有超时
Future<Payload> SendRequestAsync(MethodId, Payload);                // 无超时承诺（响应或 Stop）
void            Subscribe(EventId, Handler);                        // handler 在 binding 线程
bool            WaitForAvailable(Duration);                         // 事件驱动，不轮询
```

与 1.1 的分期对应：P0 的请求分发落在 `binding/vsomeip`（session 匹配是 SOME/IP 专属），P1 的发现落在 `core`，P2 的执行器若将来要做，由**应用**提供 worker，`core` 只定义接口。

---

## 2. 问题

### 2.1 现象

同一个方法被并发调用时，会出现两种错误结果：

1. **payload 串台**：调用方拿到的是**另一个请求**的应答，而且 `errc=0`，看起来成功；
2. **错误超时**：请求在自己的 deadline 之前就被判超时，或者一直等到 deadline 才超时。

不同方法并发、同方法顺序调用都正常。缺陷只在“同方法并发”时触发，而且**静默**——串台时调用方没有任何可察觉的信号。

### 2.2 机理

`VsomeipBindHandle::SendRequest` 的现有流程（`src/com/src/vsomeip/vsomeip_bind_handle.cpp:60`）：

```
构造 request
  → register_message_handler(service, instance, method, lambda)      // :105-132
  → application->send(request)
  → 等 condition_variable 到 deadline
  → 回调里先 unregister_message_handler(...)                       // :112-115
  → 超时分支再 unregister_message_handler(...)                      // :141-144
```

vsomeip 的 routing 对 `(service, instance, method)` 只保留**一个** handler 槽位，作用域是 **application**。于是：

- 请求 A 注册 handler H1，请求 B 用同一组 ID 注册 H2，**H1 被覆盖**；
- 应答先到，投给 H2，B 拿到属于 A 的 payload 并返回成功，A 永远等不到自己的应答 → 超时；
- 回调内和超时分支都会 `unregister`，谁后执行谁把当前槽位删掉，于是出现“还没到 deadline 就超时”。

### 2.3 涉及代码

| 编号 | 问题 | 位置 |
| --- | --- | --- |
| D1 | 每个请求都重新注册 method handler，写同一个 `(service, instance, method)` 槽位 | `vsomeip_bind_handle.cpp:105-132` |
| D2 | 回调内先 `unregister` 再交付；超时分支也 `unregister` | `:112-115`、`:141-144` |
| D3 | 异步路径同构 | `:156-226`（注册 `:195-219`，回调内注销 `:202-205`） |
| D4 | 响应只按 method 分发，**没有任何一处使用 `get_session()`**，没有会话维度 | `git grep get_session -- src/` 为空 |
| D5 | 句柄析构只 `release_service`，不清理在途请求、不注销 handler | `:36-45` |

仓库已在 `src/com/README.md:312` 记录这条限制：

> 当前 vsomeip 同步请求采用 method 级 `register_message_handler`，同一个 `(service, instance, method)` 并发请求可能互相覆盖 handler；一发一收场景没问题，高并发同 method 请求需要进一步改造。

### 2.4 关键约束

handler 注册在共享的 `vsomeip::application` 上（`vsomeip_bind_runtime.cpp:92`，每 runtime 一个 application），而 `VsomeipBindHandle` 是**每个 proxy 一个**（`vsomeip_bind_runtime.cpp:168/206`）。因此：

> **在途请求表必须与 handler 同域（application / runtime 级）。**
>
> 放在单个 handle 内部不够：一个进程里只要有两个 proxy 访问同一服务的同一方法，它们各自注册 handler，仍会互相覆盖。

---

## 3. 证据

### 3.1 测试装置

`tests/com_concurrency/`（随本文一起入库）：单可执行文件 + 双进程，`argv[1]=server|client`，`argv[2]=场景`；`run.sh` 负责编译、起服务、跑客户端、按日志计数判定。

- 服务 `0x6100 / 0x0001`，TCP 30530，SD 组播 224.244.224.245:30490
- 服务端收到请求后固定延迟 300 ms 再回显，便于制造并发窗口
- 场景与判定：

| 场景 | 语义 | 期望 |
| --- | --- | --- |
| `seq` | 同方法顺序两次 | 2 match / 0 fail |
| `diff` | 不同方法并发 | 2 match / 0 fail |
| `same` | **同方法并发** | 2 match / 0 fail |
| `steal` | 一个请求超时时另一个在途 | 1 match / 1 fail（设计预期） |

### 3.2 现状（未修正）

```
== scenario: seq
[client] seq m=1 id=1 OK elapsed=304ms echo=1 MATCH
[client] seq m=1 id=2 OK elapsed=302ms echo=2 MATCH
   PASS seq (match=2 fail=0)
== scenario: diff
[client] diff m=2 id=2 OK elapsed=313ms echo=1002 MATCH
[client] diff m=1 id=1 OK elapsed=417ms echo=1 MATCH
   PASS diff (match=2 fail=0)
== scenario: same
[client] same m=1 id=1 OK elapsed=315ms echo=2 MISMATCH
[client] same m=1 id=2 FAIL elapsed=3001ms errc=2
   FAIL same (match=0 fail=1, expected 2/0)
== scenario: steal
[client] steal m=1 id=1 FAIL elapsed=210ms errc=2
[client] steal m=1 id=2 FAIL elapsed=3011ms errc=2
   FAIL steal (match=0 fail=2, expected 1/1)
RESULT: FAILED
```

`errc=2` 是 `ComErrc::kTimeout`（`com_error_domain.h:25`）。逐条读：

- `same`：`id=1` 收到的 `echo=2` 是 `id=2` 的应答，**串台且返回成功**；`id=2` 等到 3001 ms 超时；
- `steal`：`id=1` 在 210 ms 就 `kTimeout`，远早于自己的 deadline —— handler 已被覆盖，不存在自然超时；
- `seq` / `diff` 通过，把缺陷锁定在“同方法并发”。

### 3.3 按第 4 节策略实现后的结果（探针验证）

```
== scenario: same
[client] same m=1 id=1 OK elapsed=315ms echo=1 MATCH
[client] same m=1 id=2 OK elapsed=418ms echo=2 MATCH
== scenario: steal
[client] steal m=1 id=1 FAIL elapsed=208ms errc=2
[client] steal m=1 id=2 OK elapsed=313ms echo=2 MATCH
RESULT: PASSED
```

`same` 两条各自拿到自己的 payload；`steal` 只有真正超时的那条失败。

### 3.4 结论与覆盖缺口

1. 缺陷稳定复现，且静默（串台返回成功）；
2. 第 4 节策略在同一测试集内消除了串台与错误超时；
3. 原先测试集只覆盖单 handle。S2 已补 `cross` 场景（同一个 application 上两个句柄并发调同一方法），用来验证 2.4 的约束：该场景在修复前失败（一个请求超时 3012 ms），修复后通过。

---

## 4. 修正策略

### 4.1 结构

在途请求的状态从 handle 上移到 **binding 层的 runtime 级分发器**，与 application 同生命周期。
序列化/session 是 SOME/IP 专属概念，因此分发器属于 `binding/vsomeip`，不放进 `core`。
它**不拥有任何线程**：超时由调用方线程负责，库内没有定时器。

```
VsomeipBindRuntime::Impl                    ← 与 application 同生命周期
  ├ application (vsomeip)
  └ VsomeipRequestDispatcher                （binding/vsomeip，无自有线程）
        registeredMethods_ : set<(svc,ins,method)>         每个组合只注册一个稳定 handler
        calls_             : map<token, PendingCall>       在途请求（唯一权威记录）
        bySession_         : map<session, token>           派生索引
        deadlines_         : set<(deadline, token)>        派生索引（同步等待按 deadline 排序）
        earlyResponses_    : map<session, vector<payload>>  send 与绑定之间到达的响应
        abandoned_         : set<session>                   墓碑（上限 N，超限整体清空）
        stopped_           : bool                           Stop 之后拒绝新请求
  └ VsomeipBindHandle (每 proxy 一个)        ← 只做参数转换与调用，不再持有请求状态
```

```cpp
struct PendingCall {
    std::shared_ptr<ara::core::Promise<std::vector<uint8_t>>> promise;
    std::chrono::steady_clock::time_point deadline;   // 仅同步路径使用
    std::optional<vsomeip_v3::session_t> session;     // send 之后才有值
};
```

### 4.2 发送：两阶段登记

```text
Send(svc, ins, method, data, timeout)：                 // 同步；超时归调用方
  1. lock
       if stopped_ → 解锁，返回 kNotInitialized
       EnsureMethodHandlerLocked(svc, ins, method)      // 仅首次注册，handler 常驻
       token = NextTokenLocked()                        // 我们自己的身份，不与 session 混用
       calls_[token] = {promise, now + timeout, nullopt}
       deadlines_.insert({deadline, token})             // 派生索引，供同步等待排序
     unlock
  2. request = CreateRequest(...); application->send(request)      // 不持锁
  3. lock
       session = request->get_session()                 // send 之后才有值
       calls_[token].session = session; bySession_[session] = token
       若 earlyResponses_ 里有该 session：取出 payload，DropLocked(token)，标记待交付
     unlock
  4. 锁外交付：set_value(payload)
  5. 调用方在 deadline 前未拿到结果：
       lock → 若 token 仍在 calls_ → DropLocked(token)（含墓碑） → unlock
       → 返回 kTimeout（不覆盖别人已经结算的结果）
```

不持锁 send 的原因：发送不被串行化；也不会出现“调用线程持锁 send、io 线程等同一把锁投递响应”的死锁窗口。
`SendRequestAsync` 复用同一段登记逻辑，只是第 5 步不存在（无超时承诺）。

### 4.3 响应处理

```text
OnResponse(msg):                                   // vsomeip io 线程
  session = msg->get_session()
  lock
    if session ∈ abandoned_      → 丢弃（迟到响应），计数并告警；解锁返回
    it = bySession_.find(session) → calls_.find(token)
      not found → earlyResponses_[session].push_back(payload)；解锁返回
      found     → entry = calls_.extract(token)      // 保证只 settle 一次
  unlock
  entry.promise->set_value(payload)                 // 锁外
```

### 4.4 超时策略

前提（见 1.5）：**com 不拥有 loop/线程，超时归调用方。**

- **同步** `SendRequest(svc, ins, method, data, Duration)`：由**调用线程**持有一条在途记录并等待到 deadline；到期时调用方摘下自己那条、记墓碑、返回 `kTimeout`。库内不需要定时器，也不存在“两个所有者”。
- **异步** `SendRequestAsync(svc, ins, method, data)`：**不承诺超时**，只在响应到达或 `Stop()` 时结算。原因很简单——异步没有等待线程，而 com 不允许自己起定时线程；在“应用没有统一 loop”的现状下，任何“库内超时”都等于偷偷加一个 loop。
- **将来若要异步超时**：由**持有 loop 的一方**驱动（应用主循环或 binding 的 io loop），例如调用 `dispatcher.Expire(now)`；在出现真实调用方之前不实现（当前 `SendRequestAsync` 的调用点为 0）。
- **停机**：`Stop()` 幂等，把所有在途请求以 `kCommunicationError` 结算并注销 handler。

对应影响：S3 里“4 参异步重载 + 5s 默认超时 + 定时线程”**全部回退**；`async` 测试场景改为“同一方法两条异步并发，各自拿到自己的响应”。

### 4.5 生命周期

| 事件 | 行为 |
| --- | --- |
| `VsomeipBindHandle` 析构 | 不再需要清 pending（状态在 dispatcher）；只注销该 handle 独享资源 |
| `VsomeipBindRuntime::Stop()` / 析构 | 调 `dispatcher->Stop()`：置 stopped、settle 全部 pending、注销全部 handler |
| dispatcher 析构 | 幂等，清空 `abandoned_` |

### 4.6 必须满足的不变量

- **I1** 请求表作用域 = handler 注册作用域（application / runtime 级）。
- **I2** 每个请求恰好 settle 一次（`extract` + 单次 `set_value`/`SetError`）。
- **I3** 迟到响应只能被丢弃，不能落到任何请求上（墓碑）。
- **I4** 墓碑集合有上限，超限整体清空，不允许无界增长。
- **I5** handler 生命周期 = dispatcher 生命周期，不在单次请求里注册/注销。
- **I6** 所有 settle 都在锁外（`Promise::SetResult` 会同步执行用户续体，见 `ara/core/promise.h:144-159` → `internal/state.h:165-171`）。

### 4.7 非目标

- 不引入线程池或新的异步执行框架（服务端执行器是 P2）
- 不改 `BindHandle::SendRequest` / 现有 `SendRequestAsync` 签名，不改 `apps/`
- 不修改 `third_party/vsomeip/**`
- 不用 FIFO 兜底（session 精确匹配 + 墓碑，语义更强）
- 事件/字段（`SubscribeEvent` / `OfferEvent` / `SendEvent`）不在本阶段
- `tests/com_concurrency` 不并入 CMake/CTest 默认目标

---

## 5. 任务拆解

每步一个提交，都在 `main` 上；每步结束都跑下面四条命令，任何一条失败即回滚该步（`git revert`）。

```bash
cmake --build build --target diag_server -j4      # 退出码 0
ctest --test-dir build -R idlc_byte_compare       # 1/1 Passed
bash tests/com_concurrency/run.sh                 # RESULT: PASSED
TSAN=1 bash tests/com_concurrency/run.sh          # 并发改动必跑：只看本仓库代码是否被报竞态
```

| 步骤 | 内容 | 完成标准 |
| --- | --- | --- |
| **S0** | 本文档 + `tests/com_concurrency/` 入库（测试不纳入 CMake/CTest） | 文档评审通过；`run.sh` 在未修正代码上失败、在探针实现上通过（见 3.2 / 3.3） |
| **S1** | 新增 `VsomeipRequestDispatcher`（稳定 handler + pending + 墓碑 + 超时），`SendRequest` 改走 dispatcher | `seq/diff/same/steal` 全通过；`diag_server` 构建通过 |
| **S2** | 新增 `cross` 场景：同一应用内两个句柄（各自对应一个 proxy 的绑定句柄）并发调同一服务的同一方法 | `cross` 通过（验证 I1） |
| **S3** | `SendRequestAsync` 走同一条在途表（无超时承诺）；回退 4 参重载、5 s 默认值与定时线程 | `async` 场景通过（两条异步并发各拿自己的响应）；无库内线程 |
| **S4** | 生命周期收尾：`Stop()`/析构 settle 全部 pending、统一注销 handler | `shutdown` 场景通过（有在途请求时 Stop，全部 future 以错误结束） |
| **S5** | `late`、`stress` 场景；更新 `src/com/README.md:312` 与异步超时说明 | `late`、`stress` 通过；README 与实现一致 |

### 5.1 测试矩阵（S1~S5 逐步点亮）

| 场景 | 覆盖点 | 现状 | 目标 | 由谁点亮 |
| --- | --- | --- | --- | --- |
| `seq` | 同方法顺序 | 通过 | 保持 | S1 |
| `diff` | 不同方法并发 | 通过 | 保持 | S1 |
| `same` | 同方法并发（单 handle） | 失败 | 通过 | S1 |
| `steal` | 超时与在途并存 | 失败 | 1 fail / 1 match | S1 |
| `cross` | 同一服务同方法，分属同一 application 的两个句柄 | 修复前失败 | 通过 | S2 |
| `async` | 同方法两条异步调用并发（无超时承诺） | 未覆盖 | 两条都 MATCH；超时判据由同步 `steal` 覆盖 | S3 |
| `shutdown` | 有在途请求时 `Stop()` | 未覆盖 | 全部以错误结束，无泄漏 | S4 |
| `late` | 超时后重发同方法，迟到响应到达 | 未覆盖 | 迟到被丢弃，新请求不受影响 | S5 |
| `stress` | 64 并发 + 乱序/逆序应答 | 未覆盖 | 全部匹配 | S5 |
| `discovery` | 服务在线后 `ServiceProxy::WaitForAvailable` 必须立即就绪（事件驱动） | 未覆盖 | 每次 client 输出 `discovery ready` | C1 |

---

## 6. 风险

1. **session 回绕**：`vsomeip_v3::session_t` 是 16 位，墓碑只能缓解不能根除；实现时记录判定条件与告警日志。
2. **回调线程**：响应处理器跑在 vsomeip io 线程，只允许做 map 操作 + 一次 settle；服务端业务处理移出 io 线程是 P2 的事。
3. **异步行为变化**：3 参 `SendRequestAsync` 从“无超时”变为 5 s 默认超时，需在 README 与调用方说明中体现（S5）。
4. **测试依赖真实 vsomeip 进程**：需要 vsomeip 3.3.0 与可用的回环端口，目前手工执行，未接 CI。
5. **外部缺陷（vsomeip 3.3.0，不是本仓库代码）**：TSan 在六个场景下稳定报出
   `vsomeip_v3::client_endpoint_impl<local::stream_protocol>::~client_endpoint_impl()`
   中的 `pthread_mutex_destroy` 与 io 线程的 `pthread_mutex_unlock` 竞争（12 条报告 / 7 个日志，
   全部落在 `libvsomeip3.so.3`，本仓库代码只作为调用帧出现，`vsomeip_request_dispatcher.cpp`
   命中 0 条）。把 stop 收敛为单次后报告从 12 降到 3，但没有消失 —— 说明是 vsomeip 自身的
   endpoint 销毁时序问题，我们只放大了它。
   **规避约束**：`Stop()` 单一所有者（禁止多线程重复 stop）、`Start()` 返回后再 `Stop()/Deinit()`、
   不修改 `third_party/vsomeip/**`；该缺陷记录在案，不当作本项目的偏差。

---

## 7. 缺陷清单与状态（权威核对表）

状态只有三种：**已修**（附验证证据）/ **延后**（已决定不做，注明归属）/ **未验证**（代码存在但没测到）。
不在这张表里的东西，一律不算“已验证”。

### 7.1 已修（14 项）

| # | 缺陷 | 处置 | 验证证据 |
| --- | --- | --- | --- |
| 1 | 同方法并发 payload 串台 / 假超时 | method 级稳定 handler + session 精确匹配 | `same`/`steal` 修复前 FAILED，修复后 PASS（S1 `6bb4854`） |
| 2 | 跨 handle 同方法并发仍然串台 | 请求表放 runtime 级（与 handler 注册同域） | `cross` 修复前 FAILED，修复后 PASS（S2 `f6f0ce9`） |
| 3 | 异步请求走另一套注册路径 | 异步复用同一条在途表 | `async` 两条并发各拿自己的响应 |
| 4 | 超时有两个所有者 | 删库内定时线程；同步超时由调用线程拥有 | 代码中无 `std::thread`；构建 + 六场景 |
| 5 | 迟到响应可能命中回绕后的新请求 | session 墓碑（上限 1024，超限整体清空） | 代码 + TSan 干净 |
| 6 | 到期判定依赖全表扫描 O(n) | 无定时器后不再需要；同步只比较自己那条 | 代码 |
| 7 | `Submission.token == 0` 当失败哨兵 | 改为 `{future, optional<token>}` | 代码 |
| 8 | 状态散在三处（`pending_`/`sessionToToken_`/`bound`） | `calls_` 唯一权威 + `bySession_` 派生索引 + `optional<session>` | 代码 |
| 9 | `Impl` 构造里起线程且 `noexcept` | 不启动任何线程 | 代码 |
| 10 | 迟到/早到响应静默丢弃 | `Stats{submitted,inFlight,timedOut,droppedLate,droppedEarly}` | 代码 + `GetStats()` |
| 11 | `Runtime::Init()` 空实现 | 显式状态 + 幂等；未 Init 拒绝注册 | 构建 + 六场景（S3/D3/C2 `5160125`） |
| 12 | 绑定重名静默覆盖（先 Init 再覆盖 → 泄漏） | `kAlreadyInitialized`，检查放在 `Init()` 之前 | `duplicate binding rejected errc=7` |
| 13 | 重复 `Stop()` 放大 vsomeip teardown 竞态 | `Stop()` 幂等（atomic 守卫） | TSan 报告仍来自 vsomeip，来源已确认（外部缺陷） |
| 14 | 发现机制：`Init()` 不注册回调、`WaitForAvailable` 轮询、析构留悬空回调 | 注册 `RegisterFindServiceHandle` + 条件变量 + 析构 `Deinit()` 注销 | `discovery ready (event-driven)`；位置放错时会打印 FAIL（C1，未提交） |

### 7.2 明确延后（5 项）

| # | 项 | 原因 | 归属 |
| --- | --- | --- | --- |
| 15 | `BindHandle` 泄漏到业务（`src/ucm` 9 处、`apps/someip_client`、idlc 产物） | 需要同时改 UCM + idlc 生成器 | UCM/IDL 阶段 |
| 16 | 公共 API 无事件能力（`SubscribeEvent` 只在 binding 层） | 需要 codegen 与 `ServiceProxy` 一起改 | UCM/IDL 阶段 |
| 17 | 公共 API 的线程归属 / 错误语义未成文 | 纯文档，需与 API 定稿一起写 | 文档专项 |
| 18 | 测试用 `sleep 3` 等服务上线、未进 CI | 需改测试结构与 CI | 测试专项 |
| 19 | 服务端执行器（handler 在 io 线程执行、捕获 `this`） | 属 P2，改动面大 | P2 |

### 7.3 未验证区域（6 块，不能声称“无缺陷”）

| 未覆盖 | 风险 |
| --- | --- |
| `stress`（64 并发、乱序/逆序应答） | 墓碑上限、`early_` 上限、高压下 session 回绕行为未知 |
| `shutdown`（有在途请求时 `Stop()`） | 全部以错误结算，但“先 Stop 后响应到达”的时序没测 |
| `late`（超时后重发，迟到响应到达） | 墓碑逻辑只有推演 |
| 事件路径（`SubscribeEvent`/`UnsubscribeEvent`） | 完全没测；handler 在 io 线程执行，无悬空防护 |
| 服务端路径（`VsomeipBindSkeleton`） | 未测；已知会阻塞 io 线程 |
| 跨进程 / 真实网络 | 全部测试都是同机 127.0.0.1 + 本地 UDS |

### 7.4 自洽点进度

| 自洽点 | 内容 | 状态 |
| --- | --- | --- |
| ① | dispatcher 内部只剩一处真源（无线程、单状态源、可观测） | 完成 |
| ② | 发现（C1）与运行时（C2）收口 | 完成；B1/B3 延后 |
| ③ | 运行时状态机与错误语义 | 部分（C2 完成；B5 延后） |
| ④ | 测试去 sleep、可入 CI | 待办 |

---



```bash
cd <repo>
export VSOMEIP_PREFIX=$PWD/build/third_party/vsomeip
bash tests/com_concurrency/run.sh          # 单次全跑，约 2 分钟
```

`run.sh` 会在 `tests/com_concurrency/build/` 下留下每次的 `server-*.log`、`client-*.log`，失败时自动打印对应日志。
