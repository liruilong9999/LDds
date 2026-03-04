# LDds

轻量级 DDS 框架实现，包含传输层、调度层、缓存层、IDL 编译器、类型注册与 QoS 管理。

## 覆盖检查

当前代码已覆盖你要求的最终系统能力：

1. `Transport 层`  
支持 `UDP/TCP` 两种传输协议，且由 QoS `transportType` 动态切换。
2. `DDS 调度层`  
`LDds` 完成初始化、发布、接收、订阅回调分发、Domain 写入与 QoS 线程管理。
3. `Domain 缓存层`  
`LDomain` 使用 `std::map<int, std::deque<std::vector<uint8_t>>>` 按 `historyDepth` 做线程安全历史缓存。
4. `IDL 编译器`  
`LIdlParser` 支持 `package/struct/extend/include/字段/属性/topic/注释` 并生成 AST；`LIdlGenerator` 生成 C++ 代码。
5. `类型注册机制`  
`LTypeRegistry` 提供线程安全 `registerType/createByTopic/getTopicByTypeName` 与序列化反序列化入口。
6. `QoS 管理`  
支持 `historyDepth/deadlineMs/reliable(预留)`、心跳消息、超时检测线程、XML 配置加载。

## 工程结构

1. `src/Lib/LDdsCore`  
核心库，包含传输、调度、缓存、IDL、QoS、注册中心等实现。
2. `src/App/LIdl`  
IDL 命令行工具 `LIdl.exe`。
3. `src/App/LTransportTest`  
传输层独立测试工具 `LTransportTest.exe`。
4. `build/stage*_smoke`  
各阶段烟雾测试工程（stage3/4/7/56/8）。
5. `qos.xml`  
QoS 配置文件。
6. `file1.lidl`、`file2.lidl`  
IDL 示例输入文件。

## 核心模块说明

1. `ITransport / LUdpTransport / LTcpTransport`  
统一传输抽象，具体协议实现基于 Qt Network（`QUdpSocket/QTcpSocket/QTcpServer`）。
2. `LDds`  
负责 QoS 生效、Transport 创建、消息发布接收、订阅回调、Domain 缓存、deadline/heartbeat 线程。
3. `LDomain / LFindSet`  
维护每个 topic 的历史队列并提供快照遍历（从新到旧，遍历不受新写入影响）。
4. `LTypeRegistry`  
维护类型名与 topic 映射，并提供工厂、序列化、反序列化回调。
5. `LIdlParser / LIdlGenerator`  
解析 IDL 生成 AST，再输出 `xx_define.h / xx_export.h / xx_topic.h / xx_topic.cpp`。
6. `LQos`  
统一管理传输类型、历史深度、deadline、reliable 预留字段，支持 XML 加载与校验。

## 接口调用示例（C++）

```cpp
#include "LDds.h"

using namespace LDdsFramework;

struct MsgA { int value; };

int main() {
    LQos qos;
    qos.transportType = TransportType::UDP; // 或 TCP
    qos.historyDepth = 8;
    qos.deadlineMs = 500;

    TransportConfig cfg;
    cfg.bindAddress = "127.0.0.1";
    cfg.bindPort = 20001;
    cfg.remoteAddress = "127.0.0.1";
    cfg.remotePort = 20002;

    LDds dds;
    dds.registerType<MsgA>("MsgA", 101);
    dds.subscribeTopic<MsgA>(101, [](const MsgA& msg) {
        // 订阅回调
    });

    if (!dds.initialize(qos, cfg)) {
        return 1;
    }

    dds.publishTopicByTopic<MsgA>(101, MsgA{42});
    dds.shutdown();
    return 0;
}
```

## IDL 命令行

`LIdl` 用法：

```bash
LIdl.exe [options] <input-files...>
```

常用参数：

1. `-o, --output <dir>` 输出目录
2. `-l, --language <lang>` 目标语言，默认 `cpp`
3. `-I, --include <path>` include 搜索路径
4. `-s, --strict` 严格模式
5. `-V, --verbose` 详细输出

### 已实测示例 1：生成 file1/file2 代码

```powershell
.\bin\LIdl.exe -V -o .\build\idl_out file1.lidl file2.lidl
```

期望关键输出：

1. `Processing: file1.lidl`
2. `Processing: file2.lidl`
3. `Generated: .\build\idl_out (...)`

生成文件示例：

1. `build/idl_out/file1_define.h`
2. `build/idl_out/file1_export.h`
3. `build/idl_out/file1_topic.h`
4. `build/idl_out/file1_topic.cpp`
5. `build/idl_out/file2_define.h`
6. `build/idl_out/file2_export.h`
7. `build/idl_out/file2_topic.h`
8. `build/idl_out/file2_topic.cpp`

## 测试与验证

以下命令在当前仓库已执行通过：

### 已实测示例 2：阶段 3（UDP/TCP 可切换 + 多 topic）

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
.\build\stage3_smoke\out\Debug\stage3_smoke.exe
```

期望关键输出：

1. `udp=ok tcp=ok`

### 已实测示例 3：阶段 4（缓存历史）

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
.\build\stage4_smoke\out\Debug\stage4_smoke.exe
```

期望关键输出：

1. `stage4=ok`

### 已实测示例 4：阶段 7（deadline + 心跳 + QoS XML）

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
.\build\stage7_smoke\out\Debug\stage7_smoke.exe
```

期望关键输出：

1. `stage7=ok`

### 已实测示例 5：阶段 5/6（IDL 解析 + 代码生成 + 注册 +序列化）

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
.\build\stage56_smoke\out\Debug\stage56_smoke.exe
```

期望关键输出：

1. `stage56=ok`

### 已实测示例 6：阶段 8（集成压测）

UDP：

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
powershell -ExecutionPolicy Bypass -File .\build\stage8_stress\run_stage8.ps1 -Protocol udp -DurationSec 20 -Topics 8 -PublisherThreads 4 -SubscribersPerTopic 4 -RatePerThread 150
```

TCP：

```powershell
$env:PATH='C:\code\LDds\code\bin;' + $env:PATH
powershell -ExecutionPolicy Bypass -File .\build\stage8_stress\run_stage8.ps1 -Protocol tcp -DurationSec 20 -Topics 8 -PublisherThreads 4 -SubscribersPerTopic 4 -RatePerThread 150
```

期望关键输出：

1. `senderExit=0 receiverExit=0`
2. `totalFail=0`
3. 各 topic 回调/发送统计均为非零

## 可扩展性与工业化演进建议

该实现已经具备“可扩展、支持多进程、支持跨主机”的基础形态。继续演进到工业级 DDS，建议优先补齐：

1. 可靠传输完整语义（重传、ACK/NACK、窗口、乱序重组）
2. 更细粒度 QoS（durability、ownership、liveliness、resource limits）
3. 安全能力（鉴权、加密、访问控制）
4. 观测与运维（指标、链路追踪、告警、动态配置热更新）
5. 完整互操作与兼容性测试矩阵（多平台、多版本、异常网络场景）

## Phase 0 基线治理补充

### 统一测试入口（stage3/4/7/56/8）
可使用统一脚本串行运行全部阶段测试，并输出统一格式：`PASS/FAIL + stage name + duration`。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1
```

可选参数：
1. `-Config Debug|Release|RelWithDebInfo|MinSizeRel`（默认 `Debug`）
2. `-SkipStage8`（跳过 stage8 压测）

脚本日志目录：`build/stage_runs`。

### QoS 示例配置
新增示例文件：`qos.example.xml`。

推荐流程：
1. 从示例复制一份作为本地配置。
2. 按环境修改 `transport/historyDepth/deadlineMs/reliable`。
3. 通过 `loadFromXmlFile` 直接加载。

```powershell
Copy-Item .\qos.example.xml .\qos.local.xml
```

说明：仓库中的 `qos.xml` 保持现有行为不变，`qos.example.xml` 仅作为开发配置模板。

### 统一日志前缀约定
阶段测试关键输出统一为带前缀格式，便于脚本和 CI 正则抓取：
1. `stage3`：`[stage3] ...`
2. `stage4`：`[stage4] ...`
3. `stage7`：`[stage7] ...`
4. `stage56`：`[stage56] ...`
5. `stage8`：`[stage8][SENDER] ...` / `...[RECEIVER] ...`
## Phase 1 Domain 验证入口

1. 运行 Domain 阶段冒烟：
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage1_domain_smoke.ps1
```
2. 成功标志：`[stage1_domain] domain_stage=ok`
3. 规则文档：`docs/phase1_domain_rules.md`

## 统一测试脚本新增参数

`run_all_stage_tests.ps1` 新增：
1. `-SkipBuild`：跳过 stage 可执行程序重编译
2. `-StopOnCrash`：遇到崩溃退出码（负值）时立即停止

默认行为为“先重编译再执行”，用于避免 DLL/EXE ABI 不一致导致的崩溃。
4. 若环境存在应用控制策略导致 `stage8_stress.exe` 无法启动，可先使用 `-SkipStage8` 完成其余阶段回归。
