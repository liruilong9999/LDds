/**
 * @file LIdlParser.cpp
 * @brief LIdlParser class implementation
 */

#include "LIdlParser.h"

namespace LDdsFramework {

// Forward declare AST node
struct AstNode
{
    virtual ~AstNode() = default;
};

// ParseResult implementation
bool ParseResult::hasErrors(ParseErrorLevel level) const
{
    for (const auto & error : errors)
    {
        if (error.level == level)
        {
            return true;
        }
    }
    return false;
}

size_t ParseResult::getErrorCount(ParseErrorLevel level) const
{
    size_t count = 0;
    for (const auto & error : errors)
    {
        if (error.level == level)
        {
            ++count;
        }
    }
    return count;
}

LIdlParser::LIdlParser()
    : m_options()
    , m_callback(nullptr)
    , m_errors()
{
}

LIdlParser::LIdlParser(const ParseOptions & options)
    : m_options(options)
    , m_callback(nullptr)
    , m_errors()
{
}

LIdlParser::~LIdlParser() noexcept = default;

LIdlParser::LIdlParser(LIdlParser && other) noexcept
    : m_options(std::move(other.m_options))
    , m_callback(std::move(other.m_callback))
    , m_errors(std::move(other.m_errors))
{
}

LIdlParser & LIdlParser::operator=(LIdlParser && other) noexcept
{
    if (this != &other)
    {
        m_options  = std::move(other.m_options);
        m_callback = std::move(other.m_callback);
        m_errors   = std::move(other.m_errors);
    }
    return *this;
}

void LIdlParser::setOptions(const ParseOptions & options)
{
    m_options = options;
}

const ParseOptions & LIdlParser::getOptions() const noexcept
{
    return m_options;
}

void LIdlParser::setProgressCallback(const ParseProgressCallback & callback)
{
    m_callback = callback;
}

ParseResult LIdlParser::parse(const std::string & filePath)
{
    (void)filePath;
    ParseResult result;
    result.success = false;
    return result;
}

ParseResult LIdlParser::parseString(
    const std::string & idlContent,
    const std::string & sourceName)
{
    (void)idlContent;
    (void)sourceName;
    ParseResult result;
    result.success = false;
    return result;
}

ParseResult LIdlParser::parseMultiple(const std::vector<std::string> & filePaths)
{
    (void)filePaths;
    ParseResult result;
    result.success = false;
    return result;
}

const std::vector<ParseError> & LIdlParser::getLastErrors() const noexcept
{
    return m_errors;
}

void LIdlParser::clearErrors() noexcept
{
    m_errors.clear();
}

} // namespace LDdsFramework
