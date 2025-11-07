# LDds 项目代码目录建议

以下是根据 README 中的设计要求（LIDL 描述语言、代码生成、简易 DDS 实现）生成的建议代码文件目录与说明。该目录以清晰的职责划分、可扩展性与便于单元测试为目标。

```
/src
  /Tools
    /LidlTool                # LIDL 到 C++ 代码生成器（可独立编译的工具）
      include/LidlTool/Lidl.h    # LIdl 工具的外部接口
      src/LidlTool.cpp            # LIDL 解析与生成入口
      CMakeLists.txt
  /Lib
    /LDds                    # DDS 核心实现库
      LDds.h                   # 现有单例接口（已存在）
      LDds.cpp                 # 现有实现（已存在）
      LDomain.h                # 域管理接口（文中提及）
      LDomain.cpp              # 域管理实现（主题缓存、查找集等）
      TopicCache.h             # 主题数据缓存与查找集定义
      TopicCache.cpp
      QoSConfig.h              # QoS 配置读取/保存（含配置文件支持）
      QoSConfig.cpp
      CMakeLists.txt
    /LIdl                    # 与 LIDL 相关的库（编译后供上层使用）
      LIdl.h                  # LIdl 类接口（将描述文件生成代码）
      LIdl.cpp                # LIdl 实现/包装
      codegen/                # 模板与辅助生成代码的位置
        templates.hpp
      CMakeLists.txt
  /Examples
    file1.lidl
    file2.lidl
    generated/               # 生成示例代码
      file1_define.h
      file1_export.h
      file1_topic.h
      file1_topic.cpp
  /Tests                    # 单元测试目录
    test_ldds.cpp
    test_lidl_codegen.cpp

/docs
  design.md                 # README 中的设计文档（已存在 README.md）

/CMakeLists.txt

```

说明（要点）：
- `Tools/LidlTool` 是独立的命令行工具，用于把 `.lidl` 文件生成 C++ 头/源文件（`_define.h`、`_topic.h`、`_export.h` 等）。生成代码会放到 `Examples/generated` 或项目指定输出目录。
- `Lib/LIdl` 提供程序化的代码生成接口（供其它工具/测试调用），并包含可复用模板。
- `Lib/LDds` 为运行时库，包含 `LDds` 单例、`LDomain`（域与主题数据管理）、`TopicCache`（主题数据暂存与迭代查找集）以及 `QoSConfig`（配置文件支持）。
- `Examples` 包含示例 `.lidl` 文件与生成后的示例代码，便于验证。
- `Tests` 包含单元测试及集成测试用例，建议覆盖发布/订阅、主题缓存、查找集迭代、QoS 配置读取与 LIDL 代码生成器。

建议下一步：基于该目录创建对应的 CMake/VCXPROJ 文件并添加基本的头文件/源文件 stub（接口与空实现），然后逐步实现 LIDL 解析、代码生成与 DDS 运行时功能。