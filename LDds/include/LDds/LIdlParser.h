/**
 * @file LIdlParser.h
 * @brief IDL (Interface Definition Language) Parser Component
 *
 * Provides lexical, syntax, and semantic analysis for IDL files.
 * Parses IDL files into Abstract Syntax Tree (AST) for code generator.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace LDdsFramework {

// Forward declarations
struct AstNode;
struct IdlType;
struct IdlModule;
struct IdlStruct;
struct IdlInterface;

/**
 * @brief Parse error level enumeration
 */
enum class ParseErrorLevel : uint32_t
{
    Warning = 0,    ///< Warning, does not affect parsing
    Error   = 1,    ///< Error, may affect parsing result
    Fatal   = 2     ///< Fatal error, parsing terminated
};

/**
 * @brief Parse error information structure
 */
struct ParseError
{
    ParseErrorLevel level;      ///< Error level
    std::string message;        ///< Error description
    std::string filePath;       ///< Error file path
    uint32_t line;              ///< Line number
    uint32_t column;            ///< Column number

    /**
     * @brief Default constructor
     */
    ParseError() noexcept
        : level(ParseErrorLevel::Error)
        , line(0)
        , column(0)
    {}
};

/**
 * @brief Parse options structure
 */
struct ParseOptions
{
    bool ignoreComments;        ///< Whether to ignore comments
    bool strictMode;             ///< Strict mode, reports more warnings
    bool includeAnnotations;     ///< Whether to include annotation info
    uint32_t maxIncludeDepth;    ///< Maximum include nesting depth
    std::vector<std::string> includePaths;  ///< Include path list

    /**
     * @brief Default constructor, sets default values
     */
    ParseOptions() noexcept
        : ignoreComments(false)
        , strictMode(false)
        , includeAnnotations(true)
        , maxIncludeDepth(32)
    {}
};

/**
 * @brief Parse result structure
 */
struct ParseResult
{
    bool success;                           ///< Whether parsing succeeded
    std::shared_ptr<AstNode> astRoot;       ///< AST root node
    std::vector<ParseError> errors;        ///< Parse error list
    double parseTimeMs;                     ///< Parse time in milliseconds

    /**
     * @brief Default constructor
     */
    ParseResult() noexcept
        : success(false)
        , parseTimeMs(0.0)
    {}

    /**
     * @brief Check if has errors of specified level
     * @param[in] level Error level to check
     * @return true if has errors of this level
     */
    bool hasErrors(ParseErrorLevel level) const;

    /**
     * @brief Get error count of specified level
     * @param[in] level Error level to count
     * @return Error count
     */
    size_t getErrorCount(ParseErrorLevel level) const;
};

/**
 * @brief Parse progress callback function type
 */
using ParseProgressCallback = std::function<void(
    const std::string& file,      /* Current parsing file */
    uint32_t line,                  /* Current line number */
    uint32_t totalLines             /* Total lines in file */
)>;

/**
 * @class LIdlParser
 * @brief IDL Parser Class
 *
 * Provides complete IDL file parsing functionality including
 * lexical, syntax, and semantic analysis, generating AST.
 */
class LIdlParser final
{
public:
    /**
     * @brief Default constructor
     *
     * Creates parser with default parse options.
     */
    LIdlParser();

    /**
     * @brief Constructor with options
     *
     * Creates parser with specified parse options.
     *
     * @param[in] options Parse options configuration
     */
    explicit LIdlParser(const ParseOptions& options);

    /**
     * @brief Destructor
     */
    ~LIdlParser() noexcept;

    /**
     * @brief Copy constructor is deleted
     */
    LIdlParser(const LIdlParser& other) = delete;

    /**
     * @brief Copy assignment is deleted
     */
    LIdlParser& operator=(const LIdlParser& other) = delete;

    /**
     * @brief Move constructor
     */
    LIdlParser(LIdlParser&& other) noexcept;

    /**
     * @brief Move assignment
     */
    LIdlParser& operator=(LIdlParser&& other) noexcept;

    /**
     * @brief Set parse options
     *
     * Set before calling parse, overrides constructor options.
     *
     * @param[in] options New parse options
     */
    void setOptions(const ParseOptions& options);

    /**
     * @brief Get current parse options
     * @return Const reference to current parse options
     */
    const ParseOptions& getOptions() const noexcept;

    /**
     * @brief Set progress callback
     *
     * Reports progress during long parsing operations.
     *
     * @param[in] callback Progress callback function
     */
    void setProgressCallback(const ParseProgressCallback& callback);

    /**
     * @brief Parse single IDL file
     *
     * Parses specified IDL file and returns result.
     *
     * @param[in] filePath IDL file path
     * @return Parse result containing AST and error info
     */
    ParseResult parse(const std::string& filePath);

    /**
     * @brief Parse IDL content from string
     *
     * Directly parses IDL text in memory.
     *
     * @param[in] idlContent IDL text content
     * @param[in] sourceName Source name (for error reporting)
     * @return Parse result
     */
    ParseResult parseString(
        const std::string& idlContent,      /* IDL content */
        const std::string& sourceName       /* Source identifier name */
    );

    /**
     * @brief Parse multiple IDL files
     *
     * Batch parses multiple files with shared context.
     *
     * @param[in] filePaths File path list
     * @return Parse result containing merged AST
     */
    ParseResult parseMultiple(const std::vector<std::string>& filePaths);

    /**
     * @brief Get last parse error list
     * @return Const reference to error list
     */
    const std::vector<ParseError>& getLastErrors() const noexcept;

    /**
     * @brief Clear error list
     */
    void clearErrors() noexcept;

private:
    /**
     * @brief Parse options
     */
    ParseOptions m_options;

    /**
     * @brief Progress callback
     */
    ParseProgressCallback m_callback;

    /**
     * @brief Error list
     */
    std::vector<ParseError> m_errors;
};

} // namespace LDdsFramework
