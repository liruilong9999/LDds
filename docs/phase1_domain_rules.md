# Phase 1 Domain ID 规则与兼容性说明

## 1. 域来源优先级
`LDds::initialize` 最终生效域号按以下顺序确定：

1. `initialize(..., domainId 参数)`（当参数不是 `INVALID_DOMAIN_ID`）
2. `qos.domainId`
3. `DEFAULT_DOMAIN_ID`（即 0）

说明：当前实现中 `LQos` 默认 `domainId=0`，所以常规路径通常落在第 1 或第 2 条。

## 2. Domain ID 范围与默认值
- 范围：`0-255`
- 类型：`uint8_t`
- 默认值：`0`

`LQos::validate` 会显式校验域范围，XML 加载路径也会做范围检查。

## 3. 端口映射策略
当 `enableDomainPortMapping=true` 时，传输层端口按以下公式计算：

`effectivePort = basePort + domainId * domainPortOffset`

约束：
- `basePort > 0`
- `domainPortOffset > 0`
- 计算后的 `effectivePort <= 65535`

兼容性：
- `enableDomainPortMapping=false` 时，保持旧行为（不按 domain 自动改写端口）。

## 4. 消息头与跨域过滤
消息头新增：
- `protocolVersion`
- `domainId`

接收端行为：
- `message.domainId != localDomainId` 时直接丢弃（包括 heartbeat）。

兼容策略：
- 反序列化兼容旧头格式（旧包视为 `domainId=0`）。

## 5. QoS 兼容性检查增强
`LQos::isCompatibleWith` 至少覆盖如下字段：
- `transportType`
- `reliable`
- `historyDepth`
- `deadlineMs`
- `domainId`

额外：若启用端口映射，也会比较：
- `enableDomainPortMapping`
- `basePort`
- `domainPortOffset`

## 6. 验证入口
新增独立验证脚本（不修改主 CMake）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage1_domain_smoke.ps1
```

通过标准：输出 `domain_stage=ok`。
