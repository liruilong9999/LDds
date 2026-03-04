# LDds

轻量级 DDS 风格通信框架，包含传输层、发布订阅调度层、Domain 缓存层、QoS 管理、IDL 解析与代码生成，以及阶段化验证脚本。

当前发布版本标签：`0.0.1`

## 1. 主要能力

1. 传输层支持 `UDP/TCP`。
2. 支持类型注册、主题发布/订阅、回调分发。
3. 支持 Domain 概念与缓存历史深度控制。
4. 支持 QoS（可靠性、历史、deadline、domain 等）与 XML 加载。
5. 内置 IDL 解析器与代码生成工具（`LIdl`）。
6. 提供阶段化 smoke/stress 脚本与统一测试入口。

## 2. 工程结构

1. `src/Lib/LDdsCore`
核心库（动态库），包含传输、调度、QoS、IDL、类型系统等。

2. `src/App`
可执行程序目录：
- `LIdl`：IDL 编译工具。
- `LTransportTest`：传输层测试工具。
- `Example*`：调用示例程序。

3. `src/test`
单元测试目录：
- `LDdsCoreUnitTests`：核心库单元测试可执行程序。

4. `scripts`
阶段验证脚本与辅助脚本：
- `run_all_stage_tests.ps1`：统一阶段测试入口。
- `run_src_unit_tests.ps1`：`src/test` 单元测试入口并生成 Markdown 报告。
- `run_stage*.ps1`：对应阶段 smoke 验证脚本。

5. `cmake`
工程级 CMake 模块：
- `module.cmake`：统一 `CreateTarget(...)` 目标创建逻辑。
- `module_qt.cmake`：Qt 组件查找与链接逻辑（支持 Qt6/Qt5 自动回退）。

## 3. CMake 设计说明

在保留现有目录结构和 `CreateTarget(...)` 用法的前提下，已做跨平台改造：

1. 顶层 `CMakeLists.txt` 统一 C++ 标准、编译选项、输出行为。
2. `module.cmake` 统一目标创建流程，减少子目录重复 CMake 样板。
3. `module_qt.cmake` 优先查找 Qt6，找不到自动回退 Qt5。
4. stage 子工程 CMake 去除硬编码绝对路径，改为相对仓库根目录推导。
5. stage 子工程按平台设置导入库路径：
- Windows：`.lib + .dll`
- Linux：`.so`
- macOS：`.dylib`

## 4. 环境要求

1. CMake `>= 3.16`
2. C++17 编译器
- Windows：Visual Studio/MSVC
- Linux：GCC 或 Clang
3. Qt（Qt6 或 Qt5，至少包含 `Core`、`Network`，部分目标需要 `Xml`）

可选：

1. 设置环境变量 `QT_DIR` 指向 Qt 安装根目录或 `lib/cmake` 上级目录。
2. Windows 运行时将 `bin` 加入 `PATH`。
3. Linux 运行时设置 `LD_LIBRARY_PATH` 指向 `./bin/lib`。

## 5. 构建方式

### 5.1 Windows（PowerShell）

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

### 5.2 Linux（bash）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## 6. 运行示例

### 6.1 Windows

```powershell
$env:PATH = "$PWD\bin;$env:PATH"
.\bin\ExampleQosInit.exe qos.example.xml
.\bin\ExampleUdpBytesPubSub.exe
.\bin\ExampleTypedPubSub.exe
.\bin\ExampleIdlPipeline.exe
```

### 6.2 Linux

```bash
export LD_LIBRARY_PATH="$PWD/bin/lib:$LD_LIBRARY_PATH"
./bin/ExampleQosInit qos.example.xml
./bin/ExampleUdpBytesPubSub
./bin/ExampleTypedPubSub
./bin/ExampleIdlPipeline
```

## 7. 测试与验证

### 7.1 src 单元测试（推荐）

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_src_unit_tests.ps1 -Config Debug
```

输出：

1. 执行 `LDdsCoreUnitTests`。
2. 生成报告：`reports/src_unit_test_report.md`。

### 7.2 阶段测试总入口

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1 -Config Debug
```

默认串行执行并汇总：

1. stage3
2. stage4
3. stage7
4. stage56
5. stage8

可选参数示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_all_stage_tests.ps1 -Config Release -SkipStage8
```

### 7.3 Phase 专项脚本

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage1_domain_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage2_tcp_reconnect_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage10_discovery_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage11_phase5_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage12_phase6_smoke.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_stage13_phase7_smoke.ps1
```

## 8. IDL 工具使用

`LIdl` 示例：

```powershell
.\bin\LIdl.exe -V -o .\build\idl_out file1.lidl file2.lidl
```

常用参数：

1. `-o, --output <dir>`：输出目录
2. `-l, --language <lang>`：目标语言（默认 `cpp`）
3. `-I, --include <path>`：include 路径
4. `-s, --strict`：严格模式
5. `-V, --verbose`：详细输出

## 9. 相关文档

1. [Domain 规则说明](docs/phase1_domain_rules.md)
2. [TCP 重连设计说明](docs/phase2_tcp_reconnect.md)
3. [阶段验收清单](docs/phase_definition_of_done_checklist.md)
4. [任务派单模板](docs/codex_task_order_template.md)

## 10. 常见问题

1. Qt 找不到
- 设置 `QT_DIR`，或将 Qt 安装路径加入 `CMAKE_PREFIX_PATH`。

2. Windows 运行时找不到 DLL
- 将仓库 `bin` 目录加入 `PATH`。

3. Linux 运行时找不到 `libLDdsCore*.so`
- 设置 `LD_LIBRARY_PATH=$PWD/bin/lib:$LD_LIBRARY_PATH`。

4. stage 独立工程链接失败
- 先构建根工程生成 `bin/lib` 中的 `LDdsCore` 库，再运行 stage 脚本。
