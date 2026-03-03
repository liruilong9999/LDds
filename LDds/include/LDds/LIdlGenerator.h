/**
 * @file LIdlGenerator.h
 * @brief IDL代码生成器组件
 *
 * 将IDL抽象语法树（AST）转换为各种目标语言代码。
 * 支持生成C++、C#、Java等语言的序列化和反序列化代码。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace LDdsFramework {

// 前向声明
struct AstNode;
struct ParseResult;
class LIdlParser;

/**
 * @brief 目标语言类型枚举
 */
enum class TargetLanguage : uint32_t
{
    Cpp         = 0,    ///< C++ 语言
    CSharp      = 1,    ///< C# 语言
    Java        = 2,    ///< Java 语言
    Python      = 3,    ///< Python 语言
    Go          = 4,    ///< Go 语言
    Rust        = 5,    ///< Rust 语言
    TypeScript  = 6,    ///< TypeScript 语言
    Custom      = 255   ///< 自定义目标语言
};

/**
 * @brief 代码生成选项结构
 */
struct GeneratorOptions
{
    bool generateComments;          ///< 是否生成注释
    bool generateSerialization;     ///< 是否生成序列化代码
    bool generateDeserialization;   ///< 是否生成反序列化代码
    bool generateConstructors;      ///< 是否生成构造函数
    bool generateOperators;         ///< 是否生成比较操作符
    bool useNamespace;              ///< 是否使用命名空间包裹
    bool usePragmaOnce;             ///< 是否使用#pragma once
    std::string indentStyle;        ///< 缩进风格（空格或制表符）
    uint32_t indentSize;            ///< 缩进大小
    std::string namespaceName;      ///< 目标命名空间名称
    std::string outputExtension;    ///< 输出文件扩展名

    /**
     * @brief 默认构造函数
     */
    GeneratorOptions() noexcept
        : generateComments(true)
        , generateSerialization(true)
        , generateDeserialization(true)
        , generateConstructors(true)
        , generateOperators(true)
        , useNamespace(true)
        , usePragmaOnce(true)
        , indentStyle(" ")
        , indentSize(4)
        , namespaceName("Generated")
        , outputExtension(".gen.h")
    {}
};

/**
 * @brief 代码生成结果结构
 */
struct GenerationResult
{
    bool success;                           ///< 生成是否成功
    std::string outputPath;                 ///< 生成的文件路径
    std::string generatedCode;              ///< 生成的代码内容
    std::vector<std::string> messages;    ///< 生成过程中的消息
    double generationTimeMs;                ///< 生成耗时（毫秒）
    size_t linesGenerated;                  ///< 生成的代码行数

    /**
     * @brief 默认构造函数
     */
    GenerationResult() noexcept
        : success(false)
        , generationTimeMs(0.0)
        , linesGenerated(0)
    {}

    /**
     * @brief 检查是否有警告或错误消息
     * @return true 有消息
     * @return false 无消息
     */
    bool hasMessages() const noexcept
    {
        return !messages.empty();
    }
};

/**
 * @brief 生成进度回调函数类型
 */
using GenerationProgressCallback = std::function<void(
    const std::string& currentFile,       /* 当前处理文件 */
    uint32_t currentItem,                   /* 当前项索引 */
    uint32_t totalItems,                    /* 总项数 */
    const std::string& message            /* 状态消息 */
)>;

/**
 * @class LIdlGenerator
 * @brief IDL代码生成器类
 *
 * 将IDL抽象语法树转换为各种目标语言的代码。
 * 支持自定义模板和代码生成选项。
 */
class LIdlGenerator final
{
public:
    /**
     * @brief 默认构造函数
     *
     * 使用默认选项创建生成器。
     */
    LIdlGenerator();

    /**
     * @brief 带目标语言构造函数
     *
     * 为指定目标语言创建生成器。
     *
     * @param[in] target 目标语言
     */
    explicit LIdlGenerator(TargetLanguage target);

    /**
     * @brief 析构函数
     */
    ~LIdlGenerator() noexcept;

    /**
     * @brief 禁止拷贝构造
     */
    LIdlGenerator(const LIdlGenerator& other) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    LIdlGenerator& operator=(const LIdlGenerator& other) = delete;

    /**
     * @brief 允许移动构造
     */
    LIdlGenerator(LIdlGenerator&& other) noexcept;

    /**
     * @brief 允许移动赋值
     */
    LIdlGenerator& operator=(LIdlGenerator&& other) noexcept;

    /**
     * @brief 设置目标语言
     * @param[in] target 目标语言枚举值
     */
    void setTargetLanguage(TargetLanguage target);

    /**
     * @brief 获取当前目标语言
     * @return 目标语言枚举值
     */
    TargetLanguage getTargetLanguage() const noexcept;

    /**
     * @brief 设置生成选项
     * @param[in] options 生成选项配置
     */
    void setOptions(const GeneratorOptions& options);

    /**
     * @brief 获取当前生成选项
     * @return 当前生成选项的常量引用
     */
    const GeneratorOptions& getOptions() const noexcept;

    /**
     * @brief 设置进度回调
     * @param[in] callback 进度回调函数
     */
    void setProgressCallback(const GenerationProgressCallback& callback);

    /**
     * @brief 从解析结果生成代码
     *
     * 根据解析生成的AST生成目标语言代码。
     *
     * @param[in] parseResult 解析结果，包含AST
     * @param[in] outputPath 输出文件路径
     * @return 生成结果，包含生成的代码和状态
     */
    GenerationResult generate(
        const ParseResult& parseResult,     /* 解析结果 */
        const std::string& outputPath        /* 输出路径 */
    );

    /**
     * @brief 从AST节点生成代码
     *
     * 直接对AST节点生成代码，无需完整解析结果。
     *
     * @param[in] astRoot AST根节点
     * @param[in] outputPath 输出文件路径
     * @return 生成结果
     */
    GenerationResult generateFromAst(
        const std::shared_ptr<AstNode>& astRoot,   /* AST根节点 */
        const std::string& outputPath               /* 输出路径 */
    );

    /**
     * @brief 批量生成多个文件
     *
     * 为多个解析结果批量生成代码文件。
     *
     * @param[in] parseResults 解析结果列表
     * @param[in] outputDirectory 输出目录
     * @return 生成结果列表
     */
    std::vector<GenerationResult> generateBatch(
        const std::vector<ParseResult>& parseResults,  /* 解析结果列表 */
        const std::string& outputDirectory               /* 输出目录 */
    );

    /**
     * @brief 获取支持的文件扩展名
     *
     * @return 该目标语言支持的文件扩展名列表
     */
    std::vector<std::string> getFileExtensions() const;

    /**
     * @brief 检查目标语言是否支持
     *
     * @param[in] language 要检查的目标语言
     * @return true 支持该目标语言
     * @return false 不支持
     */
    static bool isLanguageSupported(TargetLanguage language) noexcept;

    /**
     * @brief 获取支持的语言列表
     * @return 支持的目标语言枚举列表
     */
    static std::vector<TargetLanguage> getSupportedLanguages();

private:
    /**
     * @brief Target language
     */
    TargetLanguage m_target;

    /**
     * @brief Generator options
     */
    GeneratorOptions m_options;

    /**
     * @brief Progress callback
     */
    GenerationProgressCallback m_callback;
};

} // namespace LDdsFramework
