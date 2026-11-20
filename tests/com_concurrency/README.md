# com 并发回归：同一方法并发请求

复现并守护 `com::proxy::BindHandle::SendRequest` 的“同方法并发互相覆盖”缺陷。

## 缺陷现象（修复前）

每次 `SendRequest` 都用 `(service, instance, method)` 为键 `register_message_handler`，
vsomeip 对同一三元组是覆盖语义，于是并发调用必然：

- 一个调用拿到**另一个调用**的响应（payload 串台，但调用方以为成功）；
- 另一个调用一直等到超时（`ComErrc::kTimeout`，errc=2）；
- 如果先超时的那一方在另一方的 handler 注册之后超时，它会顺手把别人的 handler
  也 unregister 掉，导致两个请求同时失败。

## 修复

- 每个 method 只注册**一个稳定 handler**，生命周期与 handle 相同；
- 发送后从 `request->get_session()` 取得 vsomeip 分配的 session id（vsomeip 在
  `application_impl::send()` 里赋值），回调按 `response->get_session()` 精确匹配；
- 超时只摘除自己那一条 pending，并记下 session 墓碑，迟到响应直接丢弃；
- `send()` 与 session 绑定放在同一把锁内，因此同一时刻每个方法最多一条“待绑定”记录，
  响应抢跑时也能唯一匹配；等待响应在锁外进行，请求仍然并发。

## 场景与期望

| 场景 | 并发 | 方法 | 期望 |
|------|------|------|------|
| `seq` | 否 | 同一方法 | 两次都 MATCH |
| `diff` | 是 | 不同方法 | 两次都 MATCH |
| `same` | 是 | 同一方法 | 两次都 MATCH（修复前：一个 MISMATCH + 一个 FAIL） |
| `steal` | 是 | 同一方法，先发者 200ms 超时 | 先发者 FAIL(err=2)，后者 MATCH（修复前：两个都 FAIL） |

服务端方法 `0x0001` 回显请求中的 u32，`0x0002` 回显 u32+1000，两者都延迟 300ms。

## 运行

```bash
cd tests/com_concurrency
./run.sh
```

需要：`g++`（C++17）、可用的 vsomeip 库。脚本会依次找
`build-fix/third_party/vsomeip`、`build/third_party/vsomeip`、`~/Desktop/vsomeip/install`，
也可以用 `VSOMEIP_PREFIX` 指定。测试用 127.0.0.1 本地路由，不占外部网络。
