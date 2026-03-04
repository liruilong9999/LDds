# LDds 分阶段实施文档（执行版）

## 1. 文档目的
本文档用于把 LDds 的演进目标拆分为可执行、可验收、可回滚的阶段任务。  
你可以直接按 `Step 编号` 将任务下发给 Codex 执行代码修改。

本文档特点：
1. 每个阶段均有明确输入/输出。
2. 每个 Step 均提供建议修改点、验收标准与风险控制。
3. 优先保证核心可靠性和可维护性，再推进高级能力。

---

## 2. 当前基线（基于仓库现状）

### 2.1 已有能力
1. `UDP/TCP` 传输抽象，基于 Qt Network。
2. 基础 QoS 字段与 XML 加载：`transportType/historyDepth/deadlineMs/reliable`。
3. `LDds` 调度、topic 发布订阅、Domain 历史缓存。
4. `LMessage` 已有 `topic/sequence/payload` 头部。
5. IDL 解析与 C++ 代码生成可用（`struct/package/include/topic` 主路径）。
6. 已有 stage3/4/7/56/8 冒烟与压力测试基线。

### 2.2 主要缺口
1. UDP 可靠传输语义尚未实现（ACK/NACK/重传窗口等）。
2. QoS 兼容性检查过于简化。
3. Domain 仅在本地对象层，未进入消息头和网络隔离链路。
4. 发现机制、网络分区恢复机制较弱。
5. Durability 持久化、Ownership 语义未落地。
6. 观测与安全能力未形成体系化方案。

---

## 3. 总体阶段规划

### 3.1 阶段顺序（建议）
1. Phase 0：工程治理与测试基线固化。
2. Phase 1：Domain ID（0-255）全链路落地 + QoS 兼容性增强。
3. Phase 2：TCP 连接池与重连机制 + 基础存活检测。
4. Phase 3：UDP 可靠传输 MVP（ACK + 重传 + 接收窗口）。
5. Phase 4：发现协议与多播增强（自动发现、动态 peer）。
6. Phase 5：Durability 持久化 + QoS 热更新。
7. Phase 6：观测能力（指标、日志、链路追踪）+ 安全基础。
8. Phase 7：IDL/类型系统扩展 + 工业化验证矩阵。

### 3.2 执行原则
1. 每阶段结束必须可回归通过已有 smoke/stress。
2. 每阶段新增能力必须有最小可验证测试用例。
3. 优先兼容已有 API；确需 break 时必须提供迁移说明。

---

## 4. Phase 0：基线治理（建议先做）

### 4.1 目标
在不引入新特性的前提下，统一测试入口、日志口径、配置样例，为后续迭代降低返工成本。

### 4.2 Step 拆分

#### Step 0.1：统一测试入口脚本
1. 新增脚本：`scripts/run_all_stage_tests.ps1`。
2. 串行执行 stage3/4/7/56/8，收集退出码与日志摘要。
3. 输出统一结果格式：`PASS/FAIL + stage name + duration`。

验收标准：
1. 一条命令可运行所有阶段测试。
2. 任一阶段失败时脚本返回非零退出码。

#### Step 0.2：新增开发配置样例
1. 新增 `qos.example.xml`，补充注释字段说明。
2. 保留当前 `qos.xml` 行为不变。

验收标准：
1. `qos.example.xml` 可直接被 `loadFromXmlFile` 成功加载。
2. README 中增加“如何从 example 拷贝并修改”的说明。

#### Step 0.3：统一日志前缀
1. 各测试可执行程序输出统一前缀：`[stageX]` 或 `[module]`。
2. 便于脚本解析与 CI 汇总。

验收标准：
1. 关键路径日志行可被脚本正则抓取。

### 4.3 风险与回滚
1. 风险：脚本路径依赖本地目录结构。
2. 回滚：仅删除新增脚本和文档，不影响核心代码。

---

## 5. Phase 1：Domain ID（0-255）+ QoS 兼容性增强

### 5.1 目标
把 Domain 从“本地对象概念”升级到“网络隔离概念”，实现跨主机隔离与多域并存。  
同时增强 QoS 兼容性检查，避免发布订阅错误匹配。

### 5.2 关键设计
1. `domainId` 有效范围：`0-255`，默认 `0`。
2. 端口映射策略：`effectivePort = basePort + domainId * domainPortOffset`。
3. 消息头新增 `domainId` 字段，接收端过滤非本域消息。
4. XML 支持 `domainId`、`basePort`、`domainPortOffset`（若采用 QoS 承载）。
5. QoS 兼容性检查至少覆盖：`transportType/reliability/history/deadline/domainId`。

### 5.3 Step 拆分

#### Step 1.1：QoS 模型扩展
建议修改点：
1. `src/Lib/LDdsCore/LQos.h`
2. `src/Lib/LDdsCore/LQos.cpp`

任务：
1. 在 `LQos` 新增 `domainId`（`uint8_t`）字段。
2. `validate` 增加合法性检查（0-255 范围在 `uint8_t` 天然满足，仍可做显式校验）。
3. `loadFromXmlString/loadFromXmlFile` 解析 `domainId`。
4. `merge/isCompatibleWith` 纳入 `domainId` 判断逻辑。

验收标准：
1. XML 读取后 `domainId` 可正确生效。
2. 两个不同域的 QoS 兼容性检查返回不兼容。

#### Step 1.2：TransportConfig 端口映射参数
建议修改点：
1. `src/Lib/LDdsCore/ITransport.h`
2. `src/Lib/LDdsCore/ITransport.cpp`
3. `src/Lib/LDdsCore/LDds.cpp`

任务：
1. 在 `TransportConfig` 增加 `basePort/domainPortOffset/enableDomainPortMapping`。
2. 在 `LDds::initialize` 中计算绑定端口和远端端口（若启用映射）。
3. 保持默认行为兼容旧配置（不启用映射时行为不变）。

验收标准：
1. 域 `0` 与域 `1` 在同机可同时启动，不端口冲突。
2. 关闭映射时，旧行为与旧测试不变。

#### Step 1.3：消息头扩展 domainId
建议修改点：
1. `src/Lib/LDdsCore/LMessage.h`
2. `src/Lib/LDdsCore/LMessage.cpp`
3. `src/Lib/LDdsCore/LDds.cpp`

任务：
1. 在 `LMessageHeader` 新增 `domainId` 字段，更新序列化反序列化。
2. `LDds` 发布时写入当前域号。
3. 接收时过滤 `message.domainId != localDomainId`。
4. 处理兼容策略：  
   方案 A：协议版本字节 + 双格式兼容。  
   方案 B：一次性升级（若允许 break）。

验收标准：
1. 同 topic 下不同 domain 的消息互不可见。
2. 同 domain 下消息行为保持一致。

#### Step 1.4：LDds 初始化路径统一域来源
建议修改点：
1. `src/Lib/LDdsCore/LDds.h`
2. `src/Lib/LDdsCore/LDds.cpp`

任务：
1. 明确域来源优先级：`initialize(domainId参数) > qos.domainId > DEFAULT_DOMAIN_ID` 或反之。
2. 在日志和错误信息中打印最终域号。

验收标准：
1. 域来源规则文档化并稳定可复现。

#### Step 1.5：测试与验证
新增建议：
1. 新增 `stage1_domain_smoke`（或 `stage9_domain_smoke`）测试程序。
2. 覆盖 `domain 0/1` 隔离、端口映射、跨域过滤。

验收标准：
1. 输出 `domain_stage=ok`。
2. 与现有 stage3/4/7/56/8 不冲突。

### 5.4 风险与回滚
1. 风险：消息头变更导致协议兼容问题。
2. 缓解：引入协议版本字段或编译开关控制新旧格式。
3. 回滚：关闭 `enableDomainPortMapping`，并保持旧消息格式解析分支。

---

## 6. Phase 2：TCP 连接池与重连机制

### 6.1 目标
提升 TCP 模式稳定性，避免单连接脆弱性和瞬时网络抖动导致业务中断。

### 6.2 核心能力
1. 连接池（按 endpoint 维护连接状态）。
2. 指数退避重连（含最大重试间隔）。
3. 连接健康检查（基于心跳/写失败/状态变更）。
4. 发送队列在断线期间可配置：丢弃或限长缓存。

### 6.3 Step 拆分

#### Step 2.1：连接状态机重构
建议修改点：
1. `src/Lib/LDdsCore/LTcpTransport.h`
2. `src/Lib/LDdsCore/LTcpTransport.cpp`

任务：
1. 定义连接状态：`Disconnected/Connecting/Connected/Backoff`。
2. 每个远端 endpoint 独立状态与计时器。
3. 将 `connectToHost` 变为幂等（重复调用不重复建连）。

验收标准：
1. 同 endpoint 重复发送不会创建大量重复连接。
2. 网络断开后可自动恢复。

#### Step 2.2：重连策略参数化
建议修改点：
1. `TransportConfig` 新增：
   `autoReconnect/reconnectMinMs/reconnectMaxMs/reconnectMultiplier/maxPendingMessages`。

验收标准：
1. 配置可控制重连节奏。
2. 断线 30 秒内恢复后可继续收发。

#### Step 2.3：发送队列策略
任务：
1. 增加限长队列与溢出策略（DropOldest/DropNewest/FailFast）。
2. 关键日志输出队列长度和丢弃计数。

验收标准：
1. 压测下内存不无限增长。
2. 溢出策略行为与配置一致。

### 6.4 测试建议
1. 新增 TCP 断链恢复测试（可脚本化杀进程/断开端口）。
2. 压测中统计恢复后吞吐恢复时间。

---

## 7. Phase 3：UDP 可靠传输 MVP

### 7.1 目标
在 UDP 下实现“最小可靠传输闭环”，用于减少丢包影响，适配实时场景。

### 7.2 MVP 范围
1. 序列号管理（按 topic 或按 writer）。
2. ACK（累计确认即可先落地）。
3. 发送端重传队列（按超时重传）。
4. 接收端窗口与去重。
5. 心跳请求/响应用于丢包探测。

非目标（本阶段不做）：
1. 全 RTPS 兼容。
2. 跨版本/跨厂商互操作。
3. 复杂流控算法。

### 7.3 Step 拆分

#### Step 3.1：协议字段扩展
建议修改点：
1. `LMessageType` 增加：`Ack/Nack/HeartbeatReq/HeartbeatRsp`。
2. `LMessageHeader` 或 payload 控制区增加必要字段：
   `writerId/firstSeq/lastSeq/ackSeq/windowInfo`。

验收标准：
1. 控制消息可被正确编码/解码。

#### Step 3.2：发送端可靠状态
建议修改点：
1. `LDds.cpp` 或新增 `ReliableSession` 模块。

任务：
1. 发送后写入重传队列。
2. 收到 ACK 删除已确认消息。
3. 超时未 ACK 触发重传。

验收标准：
1. 人工注入丢包时，最终接收完整率明显提升。

#### Step 3.3：接收端窗口与去重
任务：
1. 维护 `expectedSeq` 与接收窗口。
2. 重复包丢弃，乱序包缓冲（可选）。
3. 定期发送 ACK（或在收到数据后 piggyback）。

验收标准：
1. 重复包不会触发重复回调。
2. 乱序到达时可按策略处理。

#### Step 3.4：可靠 QoS 生效
任务：
1. `qos.reliable=false` 时走旧 best-effort 路径。
2. `qos.reliable=true` 时启用可靠状态机。

验收标准：
1. 新老行为可配置切换。
2. 旧测试不受影响。

### 7.4 风险与回滚
1. 风险：状态机并发复杂度升高，引入死锁/性能回退。
2. 缓解：先单线程状态机，再逐步并发优化。
3. 回滚：通过 QoS 开关退回 best-effort。

---

## 8. Phase 4：发现协议与网络增强

### 8.1 目标
降低手工 remote 配置成本，实现局域网内自动发现、动态加入与基本存活判断。

### 8.2 能力范围
1. 周期性发现广播（节点ID、domain、topic 列表、endpoint）。
2. 本地 peer 表维护（TTL 过期剔除）。
3. UDP 多播组支持（按 domain 映射组地址）。
4. 网络分区检测与恢复通知。

### 8.3 Step 拆分

#### Step 4.1：Discovery 报文定义
任务：
1. 定义 `DiscoveryAnnounce` 报文结构。
2. 支持版本号与节点唯一标识。

验收标准：
1. 两节点同域启动后无需手动 remote 可互发现。

#### Step 4.2：Peer 表与 TTL 机制
任务：
1. 新增 peer 表：`lastSeen/endpoints/topics/capabilities`。
2. TTL 到期自动下线。

验收标准：
1. 节点退出后可在超时时间后从 peer 表消失。

#### Step 4.3：多播组按域隔离
建议策略：
1. 组播地址：`239.255.0.(domainId)` 或可配置模板。

验收标准：
1. 不同 domain 的发现流量互不影响。

---

## 9. Phase 5：Durability 持久化与 QoS 热更新

### 9.1 目标
让历史数据可跨进程重启保留，并允许运行时调整 QoS 配置。

### 9.2 Step 拆分

#### Step 5.1：Durability 落地（SQLite）
建议修改点：
1. `LDomain` 扩展存储后端接口。
2. 新增 `SqliteDurabilityStore` 模块。

任务：
1. `DurabilityKind::Persistent` 时写入 SQLite。
2. 启动时回放最近 `historyDepth` 数据。

验收标准：
1. 重启后可读取上次历史数据。
2. 性能在可接受范围内（需压测数据支持）。

#### Step 5.2：Ownership 语义初版
任务：
1. Shared/Exclusive 最小实现。
2. Exclusive 模式下支持主写者优先级仲裁。

验收标准：
1. 同 topic 多写者时，Exclusive 仅一个写者生效。

#### Step 5.3：QoS 热更新
任务：
1. 文件监控 `qos.xml`（轮询或平台 watcher）。
2. 可热更字段白名单（先支持 deadline/history/reliability）。
3. 对需重建的项执行平滑重建。

验收标准：
1. 修改 QoS 后进程无需重启即可生效。
2. 热更失败不导致进程退出。

---

## 10. Phase 6：观测与安全

### 10.1 目标
具备可运维性和基础安全能力，满足线上定位和准入控制需求。

### 10.2 Step 拆分

#### Step 6.1：指标暴露
任务：
1. 指标：发送量、接收量、丢包估算、重传次数、队列长度、连接数、deadline miss。
2. 暴露方式：文本端点或 Prometheus exporter。

验收标准：
1. 可以采集到关键链路指标。

#### Step 6.2：结构化日志
任务：
1. 接入 `spdlog` 或统一日志接口。
2. 日志字段包括 `domain/topic/seq/peer/messageId`。

验收标准：
1. 可以按 messageId 追踪一条消息路径。

#### Step 6.3：安全基础
任务：
1. Payload 加密（对称加密先行）。
2. 节点鉴权（预共享密钥或证书最小集）。

验收标准：
1. 未授权节点无法完成通信。
2. 启用加密后功能正确，性能有量化报告。

---

## 11. Phase 7：IDL/类型系统扩展与工业化验证

### 11.1 目标
提升表达能力与跨语言支持，建立更完整工业化验证矩阵。

### 11.2 Step 拆分

#### Step 7.1：IDL 语法增量
优先级建议：
1. `enum`
2. 有界/无界 `sequence`
3. `union`（最后做）

验收标准：
1. 解析器 AST 正确表达新增语法。
2. C++ 生成器可生成可编译代码并通过 round-trip 序列化测试。

#### Step 7.2：多语言生成最小闭环
建议先选一种：
1. Rust 或 Python 二选一。

验收标准：
1. 至少支持：类型定义、序列化/反序列化、topic 常量。

#### Step 7.3：工业化测试矩阵
任务：
1. 多 OS 运行（Windows/Linux）。
2. 网络模拟（丢包、乱序、时延抖动）。
3. 与 OpenDDS 的协议互操作预研（如做则单列子项目）。

验收标准：
1. 产出基准报告：延迟、吞吐、丢包恢复能力。

---

## 12. 可直接派单模板（给 Codex）

每个任务单建议包含以下字段：
1. `Step 编号`：例如 `Step 1.3`。
2. `目标`：一句话。
3. `修改文件`：明确到文件路径。
4. `约束`：兼容性、是否允许 breaking change。
5. `验收命令`：构建命令 + 测试命令 + 预期关键输出。
6. `交付物`：代码、测试、文档变更列表。

示例：
1. Step：`1.3`
2. 目标：消息头加入 domainId 并实现接收过滤
3. 修改文件：
   `src/Lib/LDdsCore/LMessage.h`
   `src/Lib/LDdsCore/LMessage.cpp`
   `src/Lib/LDdsCore/LDds.cpp`
4. 约束：默认行为兼容旧配置；协议变化需注明
5. 验收命令：运行新增 domain smoke + 现有 stage3/4/7/56
6. 交付物：PR + 迁移说明 + 测试日志

---

## 13. 阶段验收总门槛（Definition of Done）

每个 Phase 结束必须同时满足：
1. 新能力测试全部通过。
2. 历史回归测试（至少 stage3/4/7/56/8）通过。
3. 无明显性能回退（吞吐和延迟有对比数据）。
4. 文档更新完成（配置项、行为变化、兼容性说明）。
5. 失败场景可回滚（开关、配置或分支策略已定义）。

---

## 14. 推荐执行节奏

1. 第一轮（1-2 周）：Phase 0 + Phase 1 全部完成。
2. 第二轮（2-3 周）：Phase 2 + Phase 3 MVP。
3. 第三轮（2-4 周）：Phase 4 + Phase 5。
4. 第四轮（持续）：Phase 6 + Phase 7 按业务优先级推进。

---

## 15. 备注
1. 若你希望“多 Codex 并行改造”，优先并行的组合是：
   `Step 1.1 + Step 1.2 + Step 0.x`，随后合并 `Step 1.3/1.4`。
2. 高耦合 Step（例如 3.1-3.3）建议串行，由同一 Codex 连续完成以减少冲突。
3. 所有新增配置都建议提供默认值，确保旧工程可直接运行。

