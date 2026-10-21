> 任务收敛状态见：docs/TASK_CONVERGENCE.md

### P0：先冻结范围与目标板方案

- [x] **冻结首版范围**：DoIP/UDS 负责车辆发现、诊断连接和读取 F190；SOME/IP 负责服务发现、车辆状态查询及最小 OTA 控制；DDS、完整 UCM、UDS Download 暂不进入首版验收。
- [ ] **盘点现有功能并标注状态**：每项标记为“代码存在 / 本机验证 / RK3568 验证 / PC 互通验证 / 证据已留存”，尤其区分 Mock A/B 与真实 A/B。
- [ ] **确认 RK3568 升级环境**：记录板卡型号、存储介质、分区表、当前启动链路、U-Boot 版本及可恢复方式。
- [ ] **确定 A/B 方案**：明确升级对象、A/B 分区或 slot 的划分、inactive slot 写入方式、启动目标切换方式，以及新系统确认和失败回退机制。先验证启动切换与恢复，再允许 OTA 写入。

### P1：完成端到端主链路

- [ ] **打通 DoIP + UDS 读版本**：PC 完成 Vehicle Identification、Routing Activation、UDS 0x22 F190，并留存命令、日志和抓包。
- [ ] **冻结 SOME/IP 接口契约**（VehicleStatus 契约已冻结，见 docs/VehicleStatus_Contract.md）：固定 Service/Instance/Method/Event ID、字段编码、错误码和网络配置；完成 SD 服务发现及 VehicleStatus 查询，返回版本、当前 slot 和 OTA 状态。
- [ ] **打通最小 OTA 控制接口**：实现并从 PC 验证 CheckVersion、StartUpdate、GetProgress、Rollback，确认接口调用的是同一个 OTA Core。
- [ ] **实现真实镜像安装**：下载测试镜像到暂存区，校验大小与 SHA-256，写入 inactive slot；失败时不得切换启动目标。
- [ ] **实现真实切换与启动确认**：设置下一次启动 slot，重启进入新 slot，并由健康检查/确认机制标记升级成功。
- [ ] **实现并验证真实回滚**：加入一种可重复的启动失败注入；验证设备回到旧 slot，并持久化升级结果和回滚原因。

### P2：形成可演示、可复现的交付物

- [ ] **编写端到端验收脚本**：覆盖正常升级和启动失败回滚；每步包含执行命令及明确预期结果。
- [ ] **留存验收证据**：保存关键日志、DoIP/SOME/IP pcap、镜像校验结果、slot/启动配置、升级耗时及测试报告。
- [ ] **更新项目说明**：README 清楚标注已完成项、Mock 与真实板端能力的区别，以及构建、运行、升级和故障注入步骤。
- [ ] **准备面试演示材料**：用同一条主链路整理架构/状态机图和演示流程；扩展功能只作为后续计划说明。

**首版完成门槛**：PC 能读到车辆版本、发现 SOME/IP 服务并查询状态；真实镜像写入 inactive slot 后能启动到新版本；启动失败注入后能恢复旧 slot，且全过程有日志和抓包证据。