#ifndef LDDSFRAMEWORK_LIDLGENERATOR_H_
#define LDDSFRAMEWORK_LIDLGENERATOR_H_

/**
 * @file LIdlGenerator.h
 * @brief IDL AST 代码生成器接口定义。
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

struct AstNode;
struct ParseResult;

/**
 * @brief 目标代码语言。
 */
enum class TargetLanguage : uint32_t
{
    /** @brief 生成 C++ 代码。 */
    Cpp        = 0,
    /** @brief 生成 C# 代码（预留）。 */
    CSharp     = 1,
    /** @brief 生成 Java 代码（预留）。 */
    Java       = 2,
    /** @brief 生成 Python 代码。 */
    Python     = 3,
    /** @brief 生成 Go 代码（预留）。 */
    Go         = 4,
    /** @brief 生成 Rust 代码（预留）。 */
    Rust       = 5,
    /** @brief 生成 TypeScript 代码（预留）。 */
    TypeScript = 6,
    /** @brief 自定义生成通道（扩展位）。 */
    Custom     = 255
};

/**
 * @brief 代码生成选项。
 */
struct GeneratorOptions
{
    /** @brief 是否生成注释。 */
    bool        generateComments;
    /** @brief 是否生成序列化代码。 */
    bool        generateSerialization;
    /** @brief 是否生成反序列化代码。 */
    bool        generateDeserialization;
    /** @brief 是否生成构造函数。 */
    bool        generateConstructors;
    /** @brief 是否生成比较/赋值等辅助函数。 */
    bool        generateOperators;
    /** @brief 是否使用命名空间包装。 */
    bool        useNamespace;
    /** @brief 是否在生成文件中使用 #pragma once（仅作用于生成文件）。 */
    bool        usePragmaOnce;
    /** @brief 缩进字符（例如 " " 或 "\t"）。 */
    std::string indentStyle;
    /** @brief 缩进宽度。 */
    uint32_t    indentSize;
    /** @brief 默认命名空间名。 */
    std::string namespaceName;
    /** @brief 输出文件扩展名。 */
    std::string outputExtension;

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
    {
    }
};

/**
 * @brief 单次生成结果。
 */
struct GenerationResult
{
    /** @brief 是否成功。 */
    bool                     success;
    /** @brief 输出目录或主输出文件路径。 */
    std::string              outputPath;
    /** @brief 生成代码文本（如生成器选择返回内存文本时使用）。 */
    std::string              generatedCode;
    /** @brief 生成期间消息（错误/警告/提示）。 */
    std::vector<std::string> messages;
    /** @brief 生成耗时（毫秒）。 */
    double                   generationTimeMs;
    /** @brief 估算的代码行数。 */
    size_t                   linesGenerated;

    GenerationResult() noexcept
        : success(false)
        , generationTimeMs(0.0)
        , linesGenerated(0)
    {
    }

    /**
     * @brief 是否包含过程消息。
     */
    bool hasMessages() const noexcept
    {
        return !messages.empty();
    }
};

/**
 * @brief 生成进度回调。
 * @param currentFile 当前处理文件。
 * @param currentItem 当前条目序号。
 * @param totalItems 条目总数。
 * @param message 进度说明。
 */
using GenerationProgressCallback = std::function<void(
    const std::string & currentFile,
    uint32_t            currentItem,
    uint32_t            totalItems,
    const std::string & message
    )>;

/**
 * @class LIdlGenerator
 * @brief IDL 代码生成器。
 *
 * 负责将解析后的 AST 生成目标语言代码，支持单文件与批量生成。
 */
class LDDSCORE_EXPORT LIdlGenerator final
{
public:
    /** @brief 默认构造（目标语言默认 C++）。 */
    LIdlGenerator();
    /** @brief 指定目标语言构造。 */
    explicit LIdlGenerator(TargetLanguage target);
    ~LIdlGenerator() noexcept;

    LIdlGenerator(const LIdlGenerator & other) = delete;
    LIdlGenerator & operator=(const LIdlGenerator & other) = delete;

    LIdlGenerator(LIdlGenerator && other) noexcept;
    LIdlGenerator & operator=(LIdlGenerator && other) noexcept;

    /** @brief 设置目标语言。 */
    void setTargetLanguage(TargetLanguage target);
    /** @brief 获取当前目标语言。 */
    TargetLanguage getTargetLanguage() const noexcept;

    /** @brief 设置生成选项。 */
    void setOptions(const GeneratorOptions & options);
    /** @brief 获取当前生成选项。 */
    const GeneratorOptions & getOptions() const noexcept;

    /** @brief 设置进度回调。 */
    void setProgressCallback(const GenerationProgressCallback & callback);

    /**
     * @brief 从解析结果生成代码。
     * @param parseResult 解析结果。
     * @param outputPath 输出路径（目录或文件前缀，取决于实现）。
     */
    GenerationResult generate(const ParseResult & parseResult, const std::string & outputPath);

    /**
     * @brief 从 AST 根节点直接生成代码。
     */
    GenerationResult generateFromAst(
        const std::shared_ptr<AstNode> & astRoot,
        const std::string & outputPath
    );

    /**
     * @brief 批量生成。
     */
    std::vector<GenerationResult> generateBatch(
        const std::vector<ParseResult> & parseResults,
        const std::string & outputDirectory
    );

    /** @brief 返回当前目标语言对应的文件扩展名集合。 */
    std::vector<std::string> getFileExtensions() const;

    /** @brief 判断给定语言是否受支持。 */
    static bool isLanguageSupported(TargetLanguage language) noexcept;
    /** @brief 返回所有支持语言。 */
    static std::vector<TargetLanguage> getSupportedLanguages();

private:
    TargetLanguage             m_target;
    GeneratorOptions           m_options;
    GenerationProgressCallback m_callback;
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LIDLGENERATOR_H_
