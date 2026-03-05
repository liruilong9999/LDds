#ifndef LDDSFRAMEWORK_LIDLPARSER_H_
#define LDDSFRAMEWORK_LIDLPARSER_H_

/**
 * @file LIdlParser.h
 * @brief LIDL 语法解析器与 AST 数据结构定义。
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

/**
 * @brief 解析错误级别。
 */
enum class ParseErrorLevel : uint32_t
{
    /** @brief 警告，不阻断生成流程。 */
    Warning = 0,
    /** @brief 错误，通常导致当前文件解析失败。 */
    Error   = 1,
    /** @brief 致命错误，建议立即终止后续处理。 */
    Fatal   = 2
};

/**
 * @brief 单条解析错误信息。
 */
struct ParseError
{
    /** @brief 错误级别。 */
    ParseErrorLevel level;
    /** @brief 错误描述文本。 */
    std::string     message;
    /** @brief 源文件路径。 */
    std::string     filePath;
    /** @brief 行号（1-based）。 */
    uint32_t        line;
    /** @brief 列号（1-based）。 */
    uint32_t        column;

    ParseError() noexcept
        : level(ParseErrorLevel::Error)
        , line(0)
        , column(0)
    {
    }
};

/**
 * @brief 解析选项。
 */
struct ParseOptions
{
    /** @brief 是否忽略注释内容。 */
    bool                     ignoreComments;
    /** @brief 是否启用严格模式。 */
    bool                     strictMode;
    /** @brief 是否保留注解信息。 */
    bool                     includeAnnotations;
    /** @brief include 最大递归深度。 */
    uint32_t                 maxIncludeDepth;
    /** @brief include 搜索路径列表。 */
    std::vector<std::string> includePaths;

    ParseOptions() noexcept
        : ignoreComments(false)
        , strictMode(false)
        , includeAnnotations(true)
        , maxIncludeDepth(32)
    {
    }
};

/**
 * @brief 字段属性（例如注解参数）。
 */
struct LIdlFieldAttribute
{
    std::string              name;
    std::string              defaultValue;
    std::string              comment;
    std::vector<std::string> args;
};

/**
 * @brief 结构体字段定义。
 */
struct LIdlField
{
    /** @brief 字段类型名（原始文本）。 */
    std::string                      typeName;
    /** @brief 字段名。 */
    std::string                      name;
    /** @brief 注释文本。 */
    std::string                      comment;
    /** @brief 属性列表。 */
    std::vector<LIdlFieldAttribute>  attributes;
    /** @brief 默认值文本（若存在）。 */
    std::string                      defaultValue;
    /** @brief 是否为 sequence 字段。 */
    bool                             isSequence;
    /** @brief sequence 元素类型。 */
    std::string                      sequenceElementType;
    /** @brief sequence 上界（-1 表示无界）。 */
    int32_t                          sequenceBound;
    /** @brief 定义行号。 */
    uint32_t                         line;

    LIdlField()
        : isSequence(false)
        , sequenceBound(-1)
        , line(0)
    {
    }
};

/**
 * @brief 枚举值定义。
 */
struct LIdlEnumValue
{
    std::string name;
    int64_t     value;
    bool        hasExplicitValue;
    std::string comment;
    uint32_t    line;

    LIdlEnumValue()
        : value(0)
        , hasExplicitValue(false)
        , line(0)
    {
    }
};

/**
 * @brief 枚举定义。
 */
struct LIdlEnum
{
    std::string                 name;
    std::string                 fullName;
    std::string                 packagePath;
    std::string                 comment;
    std::string                 sourceFile;
    uint32_t                    line;
    std::vector<LIdlEnumValue>  values;

    LIdlEnum()
        : line(0)
    {
    }
};

/**
 * @brief union 的单个 case 分支。
 */
struct LIdlUnionCase
{
    /** @brief case 标签集合，支持多标签映射同一字段。 */
    std::vector<int64_t> labels;
    /** @brief 是否为 default 分支。 */
    bool                 isDefault;
    /** @brief 分支字段定义。 */
    LIdlField            field;

    LIdlUnionCase()
        : isDefault(false)
    {
    }
};

/**
 * @brief union 定义。
 */
struct LIdlUnion
{
    std::string                 name;
    std::string                 fullName;
    std::string                 packagePath;
    std::string                 discriminatorType;
    std::string                 comment;
    std::string                 sourceFile;
    uint32_t                    line;
    std::vector<LIdlUnionCase>  cases;

    LIdlUnion()
        : line(0)
    {
    }
};

/**
 * @brief struct 定义。
 */
struct LIdlStruct
{
    std::string               name;
    std::string               fullName;
    std::string               packagePath;
    std::string               parentType;
    std::string               comment;
    std::string               sourceFile;
    uint32_t                  line;
    std::vector<LIdlField>    fields;

    LIdlStruct()
        : line(0)
    {
    }
};

/**
 * @brief package 节点。
 */
struct LIdlPackage
{
    std::string                name;
    std::string                fullName;
    std::string                comment;
    std::vector<LIdlPackage>   children;
    std::vector<std::string>   structNames;
};

/**
 * @brief topic 映射定义（`TOPIC_NAME = TypeName`）。
 */
struct LIdlTopic
{
    std::string name;
    std::string typeName;
    std::string comment;
    /**
     * @brief Topic id锛?0 琛ㄧず鐢辫В鏋愬櫒鑷姩鐢熸垚绋冲畾 ID锛涗篃鍙€氳繃 [id, N] 鏄惧紡鎸囧畾銆?
     */
    uint32_t    id;
    std::string sourceFile;
    uint32_t    line;

    LIdlTopic()
        : id(0)
        , line(0)
    {
    }
};

/**
 * @brief AST 基类。
 */
struct AstNode
{
    virtual ~AstNode() = default;
};

/**
 * @brief IDL 文件 AST 根节点。
 */
struct LIdlFile : AstNode
{
    std::string              sourcePath;
    std::vector<std::string> includeFiles;
    std::vector<LIdlPackage> packages;
    std::vector<LIdlEnum>    enums;
    std::vector<LIdlStruct>  structs;
    std::vector<LIdlUnion>   unions;
    std::vector<LIdlTopic>   topics;
};

/**
 * @brief IDL 解析结果。
 */
struct ParseResult
{
    /** @brief 是否解析成功。 */
    bool                     success;
    /** @brief AST 根节点（成功时通常为 `LIdlFile`）。 */
    std::shared_ptr<AstNode> astRoot;
    /** @brief 错误列表。 */
    std::vector<ParseError>  errors;
    /** @brief 解析耗时（毫秒）。 */
    double                   parseTimeMs;

    ParseResult() noexcept
        : success(false)
        , parseTimeMs(0.0)
    {
    }

    /** @brief 是否存在指定级别错误。 */
    bool hasErrors(ParseErrorLevel level) const;
    /** @brief 获取指定级别错误数量。 */
    size_t getErrorCount(ParseErrorLevel level) const;

    /** @brief 将 `astRoot` 安全转换为 `LIdlFile`。 */
    std::shared_ptr<LIdlFile> asIdlFile() const;
};

/**
 * @brief 解析进度回调。
 */
using ParseProgressCallback = std::function<void(
    const std::string & file,
    uint32_t            line,
    uint32_t            totalLines
    )>;

/**
 * @class LIdlParser
 * @brief LIDL 文本解析器。
 *
 * 支持从文件、字符串、文件列表解析并产出统一 AST。
 */
class LDDSCORE_EXPORT LIdlParser final
{
public:
    /** @brief 默认构造。 */
    LIdlParser();
    /** @brief 使用指定选项构造。 */
    explicit LIdlParser(const ParseOptions & options);
    ~LIdlParser() noexcept;

    LIdlParser(const LIdlParser & other) = delete;
    LIdlParser & operator=(const LIdlParser & other) = delete;

    LIdlParser(LIdlParser && other) noexcept;
    LIdlParser & operator=(LIdlParser && other) noexcept;

    /** @brief 设置解析选项。 */
    void setOptions(const ParseOptions & options);
    /** @brief 获取当前解析选项。 */
    const ParseOptions & getOptions() const noexcept;

    /** @brief 设置进度回调。 */
    void setProgressCallback(const ParseProgressCallback & callback);

    /**
     * @brief 解析文件。
     * @param filePath 文件路径。
     */
    ParseResult parse(const std::string & filePath);
    /**
     * @brief 解析内存中的 IDL 文本。
     */
    ParseResult parseString(const std::string & idlContent, const std::string & sourceName);
    /**
     * @brief 解析多个 IDL 文件并合并 AST。
     */
    ParseResult parseMultiple(const std::vector<std::string> & filePaths);

    /** @brief 获取最近一次解析错误列表。 */
    const std::vector<ParseError> & getLastErrors() const noexcept;
    /** @brief 清空内部错误缓存。 */
    void clearErrors() noexcept;

private:
    ParseOptions             m_options;
    ParseProgressCallback    m_callback;
    std::vector<ParseError>  m_errors;
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LIDLPARSER_H_
