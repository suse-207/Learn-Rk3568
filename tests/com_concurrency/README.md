# com 并发回归：同一方法并发请求

复现并守护 `com::proxy::BindHandle::SendRequest` 的“同方法并发互相覆盖”缺陷。

设计与任务拆解见 [`docs/com-runtime-design.md`](../../docs/com-runtime-design.md)。

## 缺陷现象（修复前）

每次 `SendRequest` 都用 `(service, instance, method)` 为键 `register_message_handler`，
vsomeip 对同一三元组是覆盖语义，于是并发调用必然：

- 一个调用拿到**另一个调用**的响应（payload 串台，但调用方以为成功）；
- 另一个调用一直等到超时（`ComErrc::kTimeout`，errc=2）；
- 如果先超时的那一方在另一方的 handler 注册之后超时，它会顺手把别人的 handler
  也 unregister 掉，导致两个请求同时失败。

## 修正

在途请求表放到与 handler 注册同域的位置（runtime 级请求分发器），
按 vsomeip 分配的 session 精确匹配响应，超时只摘自己并记墓碑。

具体做法、锁边界与不变量见设计稿第 4 节，这里不重复，避免两处描述漂移。

## 场景与期望

| 场景 | 并发 | 方法 | 期望 |
|------|------|------|------|
| `seq` | 否 | 同一方法 | 两次都 MATCH |
| `diff` | 是 | 不同方法 | 两次都 MATCH |
| `same` | 是 | 同一方法、同一句柄 | 两次都 MATCH（修复前：一个 MISMATCH + 一个 FAIL） |
| `steal` | 是 | 同一方法，先发者 200ms 超时 | 先发者 FAIL(errc=2)，后者 MATCH（修复前：两个都 FAIL） |
| `cross` | 是 | 同一方法，分属同一 application 的两个句柄 | 两次都 MATCH（修复前：串台或假超时） |
| `async` | 是 | 同一方法，两条异步调用并发 | 两条都 MATCH（com 不承诺超时，只在响应/Stop 时结算） |

服务端方法 `0x0001` 回显请求中的 u32，`0x0002` 回显 u32+1000，两者都延迟 300ms。

## 运行

```bash
cd tests/com_concurrency
./run.sh
```

需要：`g++`（C++17）、可用的 vsomeip 库。脚本会依次找
`build-fix/third_party/vsomeip`、`build/third_party/vsomeip`、`~/Desktop/vsomeip/install`，
也可以用 `VSOMEIP_PREFIX` 指定。测试用 127.0.0.1 本地路由，不占外部网络。

失败时脚本会打印对应的 `client-*.log`，详细日志留在 `build/` 下（已被 `.gitignore` 忽略）。