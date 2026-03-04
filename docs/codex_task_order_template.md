# Codex 可直接派单模板

本文档用于给 Codex 下发单个 Step 任务，目标是减少任务描述歧义，提高一次交付成功率。

## 1. 通用模板（复制即用）

```markdown
## 任务单
1. Step 编号：`Step x.y`
2. 目标：一句话描述任务目标。
3. 修改文件：
   `path/to/file_a`
   `path/to/file_b`
4. 约束：
   - 是否允许 breaking change：`是/否`
   - 兼容性要求：`例如保持旧配置可运行`
   - 崩溃处理策略：`失败立即停止并输出日志 / 可重试 N 次后停止`
5. 验收命令：
   - 构建命令：`cmake --build build --config Debug`
   - 测试命令：`powershell -ExecutionPolicy Bypass -File .\scripts\xxx.ps1`
   - 预期关键输出：`[stageX] result=ok`
6. 交付物：
   - 代码变更列表
   - 测试与日志摘要
   - 文档/迁移说明
```

## 2. 示例 A（Step 1.3）

```markdown
## 任务单
1. Step 编号：`Step 1.3`
2. 目标：消息头加入 domainId 并实现接收过滤。
3. 修改文件：
   `src/Lib/LDdsCore/LMessage.h`
   `src/Lib/LDdsCore/LMessage.cpp`
   `src/Lib/LDdsCore/LDds.cpp`
4. 约束：
   - 是否允许 breaking change：`否`
   - 兼容性要求：默认行为兼容旧配置；协议变化需注明。
   - 崩溃处理策略：出现崩溃立即停止、保存日志并输出定位结论。
5. 验收命令：
   - 构建命令：`cmake --build build --config Debug`
   - 测试命令：`powershell -ExecutionPolicy Bypass -File .\scripts\run_stage1_domain_smoke.ps1`
   - 回归命令：`powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1`
   - 预期关键输出：`[stage1_domain] domain_stage=ok`，`[stage-run] PASS all stages`
6. 交付物：
   - PR + 迁移说明 + 测试日志
```

## 3. 示例 B（Step 2.2）

```markdown
## 任务单
1. Step 编号：`Step 2.2`
2. 目标：将 TCP 重连参数配置化并验证断线恢复能力。
3. 修改文件：
   `src/Lib/LDdsCore/ITransport.h`
   `src/Lib/LDdsCore/LTcpTransport.h`
   `src/Lib/LDdsCore/LTcpTransport.cpp`
4. 约束：
   - 是否允许 breaking change：`否`
   - 兼容性要求：旧配置默认值行为不变。
   - 崩溃处理策略：若重连压测期间异常退出，立即停止并输出重连状态机日志。
5. 验收命令：
   - 构建命令：`cmake --build build --config Debug`
   - 测试命令：`powershell -ExecutionPolicy Bypass -File .\scripts\run_stage2_tcp_reconnect_smoke.ps1`
   - 回归命令：`powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1`
   - 预期关键输出：`[stage2_tcp] result=ok`，`[stage-run] PASS all stages`
6. 交付物：
   - 代码 + 参数说明文档 + 测试日志
```

## 4. 派单建议

1. 每个任务单只覆盖一个 Step，避免多目标混合导致回归范围失控。
2. 验收命令中必须同时包含“新增能力测试 + 历史回归测试”。
3. 涉及协议格式调整时，必须写明“兼容策略、开关策略、回滚方式”。
