# LDds

LDds 是一个轻量级 DDS 风格通信框架，包含：

- `LDdsCore` 运行时库
- `LIdl` IDL 解析和代码生成工具
- `pub/sub` 示例程序
- `qos.xml`、`ddsRely.xml` 运行时配置

## 1. 目录说明

- `src/Lib/LDdsCore`
  运行时核心库，负责 QoS、传输、主题缓存、动态模块加载和发布订阅。
- `src/App/LIdl`
  IDL 工具入口。
- `src/App/pub`
  发布示例，当前使用 `LTopType` 和 `LCoreRuntime` 生成产物。
- `src/App/sub`
  订阅示例，当前使用 `LTopType` 和 `LCoreRuntime` 生成产物。
- `bin/config/qos.xml`
  默认 QoS 配置文件。
- `bin/config/ddsRely.xml`
  运行时动态库依赖加载顺序。
- `bin/lidl`
  当前示例 `.lidl` 文件。

## 2. 构建仓库

### 2.1 Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

### 2.2 运行前

建议把 `bin` 加入 `PATH`：

```powershell
$env:PATH = "$PWD\bin;$env:PATH"
```

### 2.3 Windows PowerShell 兼容

如果机器上没有安装 PowerShell 7，系统里可能只有 `powershell.exe` 而没有 `pwsh.exe`。

仓库已经做了兼容处理：

- 根工程 `cmake --build` 可兼容 `pwsh.exe`
- `LIdl` 生成出来的工程也可兼容 `pwsh.exe`

所以不需要手动修改 vcpkg 配置。

## 3. LIdl 使用方法

### 3.1 基本命令

当前示例以 `LCoreRuntime.lidl` 为入口，它会递归包含 `LTopType.lidl`。

```powershell
.\bin\LIdl.exe -V .\bin\lidl\LCoreRuntime.lidl
```

这条命令会自动完成：

1. 解析 `LCoreRuntime.lidl`
2. 递归解析并先处理 `LTopType.lidl`
3. 生成 `bin/generate/LTopType`
4. 生成 `bin/generate/LCoreRuntime`
5. 自动编译生成模块的 Debug/Release
6. 安装头文件、`.lib`、`.dll` 到默认安装目录

### 3.2 常用参数

- `-o, --output <dir>`
  指定生成根目录，默认是 `bin/generate`
- `--install <dir>`
  指定安装根目录，默认是输出根目录的上一级
- `-I, --include <path>`
  添加 include 搜索路径
- `-l, --language <lang>`
  指定目标语言，当前主要使用 `cpp`
- `-V, --verbose`
  打印详细过程
- `-s, --strict`
  启用严格解析
- `--platform x64`
  Windows 下指定 VS 平台

### 3.3 默认生成和安装布局

执行：

```powershell
.\bin\LIdl.exe -V .\bin\lidl\LCoreRuntime.lidl
```

默认会得到：

- 生成工程目录
  - `bin/generate/LTopType`
  - `bin/generate/LCoreRuntime`
- 安装头文件
  - `bin/include/LTopType/LTopType_topic.h`
  - `bin/include/LCoreRuntime/LCoreRuntime_topic.h`
- 安装导入库
  - `bin/lib/LTopTyped.lib`
  - `bin/lib/LTopType.lib`
  - `bin/lib/LCoreRuntimed.lib`
  - `bin/lib/LCoreRuntime.lib`
- 安装动态库
  - `bin/LTopTyped.dll`
  - `bin/LTopType.dll`
  - `bin/LCoreRuntimed.dll`
  - `bin/LCoreRuntime.dll`

## 4. 当前 IDL 示例

### 4.1 `LTopType.lidl`

它定义了基础类型和主题：

- `P1::P2::Handle`
- `P1::Param1`
- `LTopType::HANDLE_TOPIC`
- `LTopType::PARAM1_TOPIC`

### 4.2 `LCoreRuntime.lidl`

它依赖 `LTopType.lidl`，并定义：

- `P3::TestParam`
- `LCoreRuntime::TESTPARAM_TOPIC`

## 5. 业务代码怎么写

### 5.1 头文件和库

生成并安装完成后，业务工程直接使用：

- 头文件
  - `bin/include/LTopType`
  - `bin/include/LCoreRuntime`
- 库
  - `bin/lib/LDdsCore[d].lib`
  - `bin/lib/LTopType[d].lib`
  - `bin/lib/LCoreRuntime[d].lib`

运行时需要：

- `bin/LDdsCore[d].dll`
- `bin/LTopType[d].dll`
- `bin/LCoreRuntime[d].dll`

### 5.2 单例接口

现在已经支持进程内单例，不需要手动构造 `LDds`：

```cpp
using namespace LDdsFramework;

initialize();
publish(topicKey, object.get());
LFindSet * set = sub(topicKey);
shutdown();
```

也可以显式取单例对象：

```cpp
LDds::instance().initialize();
LDds::instance().publish(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handle.get());
```

### 5.3 发布端代码

```cpp
#include "LDds.h"
#include "LCoreRuntime_topic.h"
#include "LTopType_topic.h"

using namespace LDdsFramework;

initialize();

P1::P2::Handle handle;
handle.handle = 1001;
handle.datatime = 123456;
publish(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC, handle.get());

P3::TestParam testParam;
testParam.handle = 2001;
testParam.datatime = 123456;
testParam.a = 88;
publish(LCORERUNTIME_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get());
```

### 5.4 订阅端代码

```cpp
#include "LDds.h"
#include "LCoreRuntime_topic.h"
#include "LTopType_topic.h"

using namespace LDdsFramework;

initialize();

LFindSet * handleSet = sub(LTOPTYPE_TOPIC_KEY_HANDLE_TOPIC);
if (handleSet)
{
    P1::P2::Handle * data = handleSet->getFirstData<P1::P2::Handle>();
    if (data)
    {
        data->handle;
    }
}
```

这套写法下：

- 不需要在 `main` 里手写序列化
- 不需要在 `main` 里手写反序列化
- 不需要自己构造 `LDds`

## 6. qos.xml 怎么写

默认文件是 `bin/config/qos.xml`。

示例：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<qos>
    <transport type="udp" />
    <domainId>0</domainId>
    <enableDomainPortMapping>true</enableDomainPortMapping>
    <basePort>26000</basePort>
    <domainPortOffset>20</domainPortOffset>
    <historyDepth>8</historyDepth>
    <deadlineMs>1000</deadlineMs>
    <reliable>false</reliable>
</qos>
```

含义：

- `domainId`
  统一域号，代码里不需要再写死
- `basePort` + `domainPortOffset`
  默认端口映射规则
- `historyDepth`
  topic 缓存深度
- `deadlineMs`
  deadline 检查周期

## 7. ddsRely.xml 怎么写

默认文件是 `bin/config/ddsRely.xml`。

当前示例：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ddsRely>
    <library name="LTopType" path="../LTopTyped.dll" required="true" />
    <library name="LCoreRuntime" path="../LCoreRuntimed.dll" required="true" />
</ddsRely>
```

注意：

- 顺序必须按依赖顺序写
- `LCoreRuntime` 依赖 `LTopType`，所以 `LTopType` 必须在前
- Release 运行时改成：
  - `LTopType.dll`
  - `LCoreRuntime.dll`

## 8. 当前示例程序

### 8.1 先生成

```powershell
.\bin\LIdl.exe -V .\bin\lidl\LCoreRuntime.lidl
```

### 8.2 再编译

```powershell
cmake --build build --config Debug --target pub sub
```

### 8.3 运行

先运行：

```powershell
.\bin\sub.exe
```

再运行：

```powershell
.\bin\pub.exe
```

当前联调结果：

- 发布端发送 `LTopType::HANDLE_TOPIC`
- 发布端发送 `LCoreRuntime::TESTPARAM_TOPIC`
- 订阅端可正确收到并还原：
  - `handle.handle=1001`
  - `testParam.handle=2001`
  - `testParam.a=88`

## 9. 当前约定

- `app/pub` 和 `app/sub` 使用安装后的 `LTopType/LCoreRuntime` 产物
- `LDdsCore` 自己加载 `qos.xml`
- `LDdsCore` 自己加载 `ddsRely.xml`
- 代码里不再手写 `domainId`
- 默认使用单例接口
- 主题统一使用字符串 `topicKey`
