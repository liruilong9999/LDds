#pragma once

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
    Warning = 0,
    Error   = 1,
    Fatal   = 2
};

struct ParseError
{
    ParseErrorLevel level;
    std::string     message;
    std::string     filePath;
    uint32_t        line;
    uint32_t        column;

    ParseError() noexcept
        : level(ParseErrorLevel::Error)
        , line(0)
        , column(0)
    {
    }
};

/**
 * @brief 解析器可选项。
 */
struct ParseOptions
{
    bool                     ignoreComments;
    bool                     strictMode;
    bool                     includeAnnotations;
    uint32_t                 maxIncludeDepth;
    std::vector<std::string> includePaths;

    ParseOptions() noexcept
        : ignoreComments(false)
        , strictMode(false)
        , includeAnnotations(true)
        , maxIncludeDepth(32)
    {
    }
};

struct LIdlFieldAttribute
{
    std::string              name;
    std::string              defaultValue;
    std::string              comment;
    std::vector<std::string> args;
};

struct LIdlField
{
    std::string                      typeName;
    std::string                      name;
    std::string                      comment;
    std::vector<LIdlFieldAttribute>  attributes;
    std::string                      defaultValue;
    uint32_t                         line;

    LIdlField()
        : line(0)
    {
    }
};

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

struct LIdlPackage
{
    std::string                name;
    std::string                fullName;
    std::string                comment;
    std::vector<LIdlPackage>   children;
    std::vector<std::string>   structNames;
};

struct LIdlTopic
{
    std::string name;
    std::string typeName;
    std::string comment;
    uint32_t    id;
    std::string sourceFile;
    uint32_t    line;

    LIdlTopic()
        : id(0)
        , line(0)
    {
    }
};

struct AstNode
{
    virtual ~AstNode() = default;
};

struct LIdlFile : AstNode
{
    std::string              sourcePath;
    std::vector<std::string> includeFiles;
    std::vector<LIdlPackage> packages;
    std::vector<LIdlStruct>  structs;
    std::vector<LIdlTopic>   topics;
};

/**
 * @brief IDL 解析结果。
 */
struct ParseResult
{
    bool                     success;
    std::shared_ptr<AstNode> astRoot;
    std::vector<ParseError>  errors;
    double                   parseTimeMs;

    ParseResult() noexcept
        : success(false)
        , parseTimeMs(0.0)
    {
    }

    bool hasErrors(ParseErrorLevel level) const;
    size_t getErrorCount(ParseErrorLevel level) const;

    std::shared_ptr<LIdlFile> asIdlFile() const;
};

using ParseProgressCallback = std::function<void(
    const std::string & file,
    uint32_t            line,
    uint32_t            totalLines
    )>;

class LDDSCORE_EXPORT LIdlParser final
{
public:
    LIdlParser();
    explicit LIdlParser(const ParseOptions & options);
    ~LIdlParser() noexcept;

    LIdlParser(const LIdlParser & other) = delete;
    LIdlParser & operator=(const LIdlParser & other) = delete;

    LIdlParser(LIdlParser && other) noexcept;
    LIdlParser & operator=(LIdlParser && other) noexcept;

    void setOptions(const ParseOptions & options);
    const ParseOptions & getOptions() const noexcept;

    void setProgressCallback(const ParseProgressCallback & callback);

    ParseResult parse(const std::string & filePath);
    /**
     * @brief 解析内存中的 IDL 文本。
     */
    ParseResult parseString(const std::string & idlContent, const std::string & sourceName);
    /**
     * @brief 解析多个 IDL 文件并合并 AST。
     */
    ParseResult parseMultiple(const std::vector<std::string> & filePaths);

    const std::vector<ParseError> & getLastErrors() const noexcept;
    void clearErrors() noexcept;

private:
    ParseOptions          m_options;
    ParseProgressCallback m_callback;
    std::vector<ParseError> m_errors;
};

} // namespace LDdsFramework
