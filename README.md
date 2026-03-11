# LDds

LDds 是一个轻量级 DDS 风格通信框架，包含：

- `LDdsCore` 运行时库
- `LIdl` IDL 解析与代码生成工具
- `pub/sub` 示例程序
- `qos.xml`、`ddsRely.xml` 运行时配置

## 1. 目录说明

- `src/Lib/LDdsCore`
  运行时核心库，负责 QoS、传输、主题缓存、动态模块加载和发布订阅。
- `src/App/LIdl`
  IDL 工具入口。
- `src/App/pub`
  发布示例，使用安装后的 `file1/file2` 生成头文件。
- `src/App/sub`
  订阅示例，使用安装后的 `file1/file2` 生成头文件。
- `bin/config/qos.xml`
  默认 QoS 配置文件。
- `bin/config/ddsRely.xml`
  运行时模块依赖配置，`LDdsCore` 初始化时会自动加载。
- `bin/lidl`
  示例 `.lidl` 文件。

## 2. 构建仓库

### 2.1 Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
cmake --build build --config Release
```

### 2.2 运行前

Windows 下建议把 `bin` 放入 `PATH`：

```powershell
$env:PATH = "$PWD\bin;$env:PATH"
```

## 3. LIdl 使用方法

### 3.1 基本命令

默认输出根目录是 `bin/generate`，默认安装根目录是输出根目录的上一级。
如果不传 `-o`，仓库内默认会生成到 `bin/generate/<文件名>`，并安装到 `bin`。

```powershell
.\bin\LIdl.exe -V .\bin\lidl\file2.lidl
```

上面的命令会自动完成：

1. 解析 `file2.lidl`
2. 递归解析并先处理 `#include "file1.lidl"`
3. 生成 `bin/generate/file1`
4. 生成 `bin/generate/file2`
5. 自动编译 `LDdsCore`
6. 自动编译 `file1` 的 Debug/Release
7. 自动编译 `file2` 的 Debug/Release

### 3.2 常用参数

- `-o, --output <dir>`
  指定生成根目录。
- `--install <dir>`
  指定安装根目录。
- `-I, --include <path>`
  添加 include 搜索路径。
- `-l, --language <lang>`
  目标语言，当前主要使用 `cpp`。
- `-V, --verbose`
  打印详细过程。
- `-s, --strict`
  启用严格解析。
- `--platform x64`
  Windows 下指定 VS 平台。

### 3.3 生成和安装后的目录布局

以 `file2.lidl` 为例，默认执行：

```powershell
.\bin\LIdl.exe -V .\bin\lidl\file2.lidl
```

会得到以下结果：

- 生成工程目录
  - `bin/generate/file1`
  - `bin/generate/file2`
- 安装头文件
  - `bin/include/file1/file1_topic.h`
  - `bin/include/file2/file2_topic.h`
- 安装导入库
  - `bin/lib/file1d.lib`
  - `bin/lib/file1.lib`
  - `bin/lib/file2d.lib`
  - `bin/lib/file2.lib`
- 安装动态库
  - `bin/file1d.dll`
  - `bin/file1.dll`
  - `bin/file2d.dll`
  - `bin/file2.dll`

## 4. IDL 怎么写

### 4.1 基本规则

- `struct` 表示数据结构
- `package` 表示命名空间
- `#include` 表示依赖其他 `.lidl`
- `TOPIC_NAME = TypeName;` 表示主题绑定

### 4.2 示例

`file1.lidl`

```idl
package P1 {
    package P2 {
        struct Handle {
            int32 handle;
            int64 datatime;
        }
    }

    struct Param1 : extend P1::P2::Handle {
        string str1;
        double data1;
        int32 data2;
        vector<int32> data1Vec;
    }
}

HANDLE_TOPIC = P1::P2::Handle;
PARAM1_TOPIC = P1::Param1;
```

`file2.lidl`

```idl
#include "file1.lidl"

package P3 {
    struct TestParam : extend P1::P2::Handle {
        int32 a;
    }
}

TESTPARAM_TOPIC = P3::TestParam;
```

## 5. 业务代码怎么写

### 5.1 头文件和库依赖

生成并安装完成后，业务代码直接使用安装后的头文件和库：

- 头文件
  - `bin/include/file1`
  - `bin/include/file2`
- 库
  - `bin/lib/LDdsCore[d].lib`
  - `bin/lib/file1[d].lib`
  - `bin/lib/file2[d].lib`

运行时还需要：

- `bin/LDdsCore[d].dll`
- `bin/file1[d].dll`
- `bin/file2[d].dll`

### 5.2 发布端代码写法

```cpp
#include "LDds.h"
#include "file1_topic.h"
#include "file2_topic.h"

using namespace LDdsFramework;

LDds publisher;
publisher.initialize();

P1::Param1 param1;
param1.handle = 1001;
param1.datatime = 123456;
param1.str1 = "hello";
param1.data1 = 3.14;
param1.data2 = 7;
param1.data1Vec = {1, 2, 3};

publisher.publish(FILE1_TOPIC_KEY_PARAM1_TOPIC, param1.get());

P3::TestParam testParam;
testParam.handle = 2001;
testParam.datatime = 123456;
testParam.a = 88;

publisher.publish(FILE2_TOPIC_KEY_TESTPARAM_TOPIC, testParam.get());
```

### 5.3 订阅端代码写法

最稳的写法是从 `Domain` 缓存里取 payload，再用生成结构体自己的 `deserialize()` 还原：

```cpp
#include "LDds.h"
#include "file1_topic.h"
#include "file2_topic.h"

using namespace LDdsFramework;

LDds subscriber;
subscriber.initialize();

std::vector<uint8_t> handlePayload;
if (subscriber.domain().getTopicData(FILE1_TOPIC_ID_HANDLE_TOPIC, handlePayload))
{
    P1::P2::Handle data;
    if (data.deserialize(handlePayload))
    {
        data.handle;
    }
}
```

如果业务希望按缓存轮询，也可以这样写：

```cpp
LFindSet * handleSet = subscriber.sub(FILE1_TOPIC_KEY_HANDLE_TOPIC);
if (handleSet)
{
    P1::P2::Handle * data = handleSet->getFirstData<P1::P2::Handle>();
    if (data)
    {
        data->handle;
    }
}
```

### 5.4 同机测试和跨机部署

- 同机测试
  建议在代码里给 `TransportConfig` 显式指定不同 `bindPort`，避免本机端口冲突。
- 跨机部署
  可以直接使用 `LDds::initialize()`，域号和 QoS 由 `qos.xml` 统一控制。

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

字段说明：

- `transport`
  当前常用 `udp`
- `domainId`
  域号，代码里不需要再写死
- `enableDomainPortMapping`
  是否启用按域映射端口
- `basePort`
  基础端口
- `domainPortOffset`
  域偏移量
- `historyDepth`
  每个 topic 的缓存深度
- `deadlineMs`
  deadline 监控周期
- `reliable`
  是否启用可靠语义

## 7. ddsRely.xml 怎么写

默认文件是 `bin/config/ddsRely.xml`。

它的作用是告诉 `LDdsCore` 运行时要先加载哪些生成模块。

示例：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ddsRely>
    <library name="file1" path="../file1d.dll" required="true" />
    <library name="file2" path="../file2d.dll" required="true" />
</ddsRely>
```

注意：

- 顺序必须按依赖顺序写，`file2` 依赖 `file1` 时，`file1` 必须在前
- Debug 运行时写 `file1d.dll`、`file2d.dll`
- Release 运行时改成 `file1.dll`、`file2.dll`

## 8. 示例程序

### 8.1 重新生成并安装 file1/file2

```powershell
.\bin\LIdl.exe -V .\bin\lidl\file2.lidl
```

### 8.2 编译 pub/sub

```powershell
cmake --build build --config Debug --target pub sub
```

### 8.3 运行

先运行订阅端：

```powershell
.\bin\sub.exe
```

再运行发布端：

```powershell
.\bin\pub.exe
```

## 9. 当前约定

- `app/pub` 和 `app/sub` 使用安装后的 `file1/file2` 产物
- `LDdsCore` 自己加载 `qos.xml`
- `LDdsCore` 自己加载 `ddsRely.xml`
- 代码中不再手写 domainId
- 主题统一使用字符串 `topicKey`
