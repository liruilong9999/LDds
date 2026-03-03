/**
 * @file LIdlParser.h
 * @brief IDL（接口定义语言）解析器组件
 *
 * 为IDL文件提供词法、语法和语义分析。
 * 将IDL文件解析为抽象语法树（AST），供代码生成器使用。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "LDds_Global.h"

namespace LDdsFramework {

// 前向声明
struct AstNode;
struct IdlType;
struct IdlModule;
struct IdlStruct;
struct IdlInterface;

/**
 * @brief 解析错误级别枚举
 */
enum class ParseErrorLevel : uint32_t
{
    Warning = 0, ///< 警告，不影响解析
    Error   = 1, ///< 错误，可能影响解析结果
    Fatal   = 2  ///< 致命错误，解析终止
};

/**
 * @brief 解析错误信息结构
 */
struct ParseError
{
    ParseErrorLevel level;    ///< 错误级别
    std::string     message;  ///< 错误描述
    std::string     filePath; ///< 错误文件路径
    uint32_t        line;     ///< 行号
    uint32_t        column;   ///< 列号

    /**
     * @brief 默认构造函数
     */
    ParseError() noexcept
        : level(ParseErrorLevel::Error)
        , line(0)
        , column(0)
    {}
};

/**
 * @brief 解析选项结构
 */
struct ParseOptions
{
    bool                     ignoreComments;     ///< 是否忽略注释
    bool                     strictMode;         ///< 严格模式，报告更多警告
    bool                     includeAnnotations; ///< 是否包含注解信息
    uint32_t                 maxIncludeDepth;    ///< 最大包含嵌套深度
    std::vector<std::string> includePaths;       ///< 包含路径列表

    /**
     * @brief 默认构造函数，设置默认值
     */
    ParseOptions() noexcept
        : ignoreComments(false)
        , strictMode(false)
        , includeAnnotations(true)
        , maxIncludeDepth(32)
    {}
};

/**
 * @brief 解析结果结构
 */
struct ParseResult
{
    bool                     success;     ///< 解析是否成功
    std::shared_ptr<AstNode> astRoot;     ///< AST根节点
    std::vector<ParseError>  errors;      ///< 解析错误列表
    double                   parseTimeMs; ///< 解析耗时（毫秒）

    /**
     * @brief 默认构造函数
     */
    ParseResult() noexcept
        : success(false)
        , parseTimeMs(0.0)
    {}

    /**
     * @brief 检查是否有指定级别的错误
     * @param[in] level 要检查的错误级别
     * @return true 有此级别的错误
     */
    bool hasErrors(ParseErrorLevel level) const;

    /**
     * @brief 获取指定级别错误的数量
     * @param[in] level 要统计的错误级别
     * @return 错误数量
     */
    size_t getErrorCount(ParseErrorLevel level) const;
};

/**
 * @brief 解析进度回调函数类型
 */
using ParseProgressCallback = std::function<void(
    const std::string & file,      /* 当前解析文件 */
    uint32_t            line,      /* 当前行号 */
    uint32_t            totalLines /* 文件总行数 */
    )>;

/**
 * @class LIdlParser
 * @brief IDL解析器类
 *
 * 提供完整的IDL文件解析功能，包括
 * 词法、语法和语义分析，生成AST。
 */
class LDDSCORE_EXPORT LIdlParser final
{
public:
    /**
     * @brief 默认构造函数
     *
     * 使用默认解析选项创建解析器。
     */
    LIdlParser();

    /**
     * @brief 带选项的构造函数
     *
     * 使用指定解析选项创建解析器。
     *
     * @param[in] options 解析选项配置
     */
    explicit LIdlParser(const ParseOptions & options);

    /**
     * @brief 析构函数
     */
    ~LIdlParser() noexcept;

    /**
     * @brief 禁止拷贝构造
     */
    LIdlParser(const LIdlParser & other) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    LIdlParser & operator=(const LIdlParser & other) = delete;

    /**
     * @brief 允许移动构造
     */
    LIdlParser(LIdlParser && other) noexcept;

    /**
     * @brief 允许移动赋值
     */
    LIdlParser & operator=(LIdlParser && other) noexcept;

    /**
     * @brief 设置解析选项
     *
     * 在调用parse之前设置，覆盖构造函数选项。
     *
     * @param[in] options 新的解析选项
     */
    void setOptions(const ParseOptions & options);

    /**
     * @brief 获取当前解析选项
     * @return 当前解析选项的常量引用
     */
    const ParseOptions & getOptions() const noexcept;

    /**
     * @brief 设置进度回调
     *
     * 在长时间解析操作中报告进度。
     *
     * @param[in] callback 进度回调函数
     */
    void setProgressCallback(const ParseProgressCallback & callback);

    /**
     * @brief 解析单个IDL文件
     *
     * 解析指定的IDL文件并返回结果。
     *
     * @param[in] filePath IDL文件路径
     * @return 包含AST和错误信息的解析结果
     */
    ParseResult parse(const std::string & filePath);

    /**
     * @brief 从字符串解析IDL内容
     *
     * 直接解析内存中的IDL文本。
     *
     * @param[in] idlContent IDL文本内容
     * @param[in] sourceName 源名称（用于错误报告）
     * @return 解析结果
     */
    ParseResult parseString(
        const std::string & idlContent, /* IDL内容 */
        const std::string & sourceName  /* 源标识名称 */
    );

    /**
     * @brief 解析多个IDL文件
     *
     * 批量解析多个文件，共享上下文。
     *
     * @param[in] filePaths 文件路径列表
     * @return 包含合并AST的解析结果
     */
    ParseResult parseMultiple(const std::vector<std::string> & filePaths);

    /**
     * @brief 获取上次解析错误列表
     * @return 错误列表的常量引用
     */
    const std::vector<ParseError> & getLastErrors() const noexcept;

    /**
     * @brief 清空错误列表
     */
    void clearErrors() noexcept;

private:
    /**
     * @brief 解析选项
     */
    ParseOptions m_options;

    /**
     * @brief 进度回调
     */
    ParseProgressCallback m_callback;

    /**
     * @brief 错误列表
     */
    std::vector<ParseError> m_errors;
};

} // namespace LDdsFramework
