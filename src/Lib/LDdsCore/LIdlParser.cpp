#include "LIdlParser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace LDdsFramework {
namespace {

namespace fs = std::filesystem;

struct ParseContext
{
    std::shared_ptr<LIdlFile>                      ast;
    std::unordered_set<std::string>                parsedFiles;
    std::unordered_map<std::string, std::string>   structOwners;
    std::unordered_map<std::string, std::string>   enumOwners;
    std::unordered_map<std::string, std::string>   unionOwners;
    std::unordered_map<std::string, std::string>   topicNameOwners;
    std::unordered_map<std::string, std::string>   topicTypeToName;
};

struct LineState
{
    std::vector<std::string>         packageStack;
    bool                             inStruct = false;
    bool                             inEnum = false;
    bool                             inUnion = false;
    bool                             waitingPackageBrace = false;
    bool                             waitingStructBrace = false;
    bool                             waitingEnumBrace = false;
    bool                             waitingUnionBrace = false;
    std::string                      waitingPackageName;
    LIdlStruct                       currentStruct;
    LIdlStruct                       pendingStruct;
    LIdlEnum                         currentEnum;
    LIdlEnum                         pendingEnum;
    int64_t                          nextEnumValue = 0;
    LIdlUnion                        currentUnion;
    LIdlUnion                        pendingUnion;
    std::vector<int64_t>             pendingUnionLabels;
    bool                             pendingUnionDefault = false;
    std::vector<LIdlFieldAttribute>  pendingFieldAttributes;
    std::string                      pendingComment;
    std::string                      pendingTopicComment;
};

std::string trim(const std::string & text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return std::string();
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string stripBom(const std::string & text)
{
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
    {
        return text.substr(3);
    }
    return text;
}

std::string joinPackage(const std::vector<std::string> & packageStack)
{
    std::string result;
    for (size_t i = 0; i < packageStack.size(); ++i)
    {
        if (i > 0)
        {
            result += "::";
        }
        result += packageStack[i];
    }
    return result;
}

std::vector<std::string> splitCommaRespectingQuotes(const std::string & text)
{
    std::vector<std::string> parts;
    std::string              current;
    bool                     inQuote = false;

    for (char ch : text)
    {
        if (ch == '"')
        {
            inQuote = !inQuote;
            current.push_back(ch);
            continue;
        }

        if (ch == ',' && !inQuote)
        {
            parts.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty() || (!text.empty() && text.back() == ','))
    {
        parts.push_back(trim(current));
    }

    return parts;
}

std::string unquote(const std::string & value)
{
    std::string out = trim(value);
    if (out.size() >= 2)
    {
        if ((out.front() == '"' && out.back() == '"') ||
            (out.front() == '\'' && out.back() == '\''))
        {
            out = out.substr(1, out.size() - 2);
        }
    }
    return out;
}

bool parseSignedInt64(const std::string & text, int64_t & valueOut)
{
    const std::string normalized = trim(text);
    if (normalized.empty())
    {
        return false;
    }

    try
    {
        size_t pos = 0;
        const long long parsed = std::stoll(normalized, &pos, 10);
        if (pos != normalized.size())
        {
            return false;
        }
        valueOut = static_cast<int64_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool parseSequenceType(
    const std::string & rawType,
    std::string &       elementTypeOut,
    int32_t &           boundOut)
{
    const std::string type = trim(rawType);
    const std::regex sequencePattern(
        R"(^sequence\s*<\s*([^,>]+(?:\s*::\s*[^,>]+)*)\s*(?:,\s*([0-9]+)\s*)?>\s*$)");
    std::smatch match;
    if (!std::regex_match(type, match, sequencePattern))
    {
        return false;
    }

    elementTypeOut = trim(match[1].str());
    boundOut = -1;
    if (match[2].matched)
    {
        int64_t parsedBound = -1;
        if (!parseSignedInt64(match[2].str(), parsedBound))
        {
            return false;
        }
        if (parsedBound < 0 || parsedBound > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
        {
            return false;
        }
        boundOut = static_cast<int32_t>(parsedBound);
    }
    return !elementTypeOut.empty();
}

std::string stripInlineComment(const std::string & line, std::string & comment)
{
    bool inQuote = false;
    for (size_t i = 0; i + 1 < line.size(); ++i)
    {
        const char ch = line[i];
        if (ch == '"')
        {
            inQuote = !inQuote;
        }

        if (!inQuote && ch == '/' && line[i + 1] == '/')
        {
            comment = trim(line.substr(i + 2));
            return trim(line.substr(0, i));
        }
    }

    comment.clear();
    return trim(line);
}

void appendComment(std::string & pendingComment, const std::string & comment)
{
    if (comment.empty())
    {
        return;
    }

    if (!pendingComment.empty())
    {
        pendingComment += "\n";
    }
    pendingComment += comment;
}

LIdlFieldAttribute parseAttributeRaw(const std::string & raw)
{
    LIdlFieldAttribute attribute;
    const auto tokens = splitCommaRespectingQuotes(raw);
    if (!tokens.empty())
    {
        attribute.name = unquote(tokens[0]);
    }

    if (tokens.size() > 1)
    {
        attribute.defaultValue = unquote(tokens[1]);
    }

    if (tokens.size() > 2)
    {
        attribute.comment = unquote(tokens[2]);
    }

    attribute.args.reserve(tokens.size());
    for (const auto & token : tokens)
    {
        attribute.args.push_back(unquote(token));
    }

    return attribute;
}

std::string resolveIncludePath(
    const std::string &      includeToken,
    const fs::path &         currentDir,
    const ParseOptions &     options)
{
    std::vector<fs::path> candidates;

    const fs::path includePath(includeToken);
    if (includePath.is_absolute())
    {
        candidates.push_back(includePath);
    }
    else
    {
        candidates.push_back(currentDir / includePath);
        for (const auto & path : options.includePaths)
        {
            candidates.push_back(fs::path(path) / includePath);
        }
        candidates.push_back(includePath);
    }

    for (const auto & candidate : candidates)
    {
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec)
        {
            continue;
        }

        const fs::path canonical = fs::weakly_canonical(candidate, ec);
        if (!ec)
        {
            return canonical.string();
        }
        return candidate.string();
    }

    return std::string();
}

LIdlPackage * ensurePackagePath(
    std::vector<LIdlPackage> & root,
    const std::vector<std::string> & packageStack)
{
    if (packageStack.empty())
    {
        return nullptr;
    }

    std::vector<LIdlPackage> * currentList = &root;
    LIdlPackage *             currentNode = nullptr;

    std::string full;
    for (const auto & segment : packageStack)
    {
        if (!full.empty())
        {
            full += "::";
        }
        full += segment;

        auto it = std::find_if(
            currentList->begin(),
            currentList->end(),
            [&segment](const LIdlPackage & item) {
                return item.name == segment;
            }
        );

        if (it == currentList->end())
        {
            LIdlPackage pkg;
            pkg.name = segment;
            pkg.fullName = full;
            currentList->push_back(pkg);
            it = std::prev(currentList->end());
        }

        currentNode = &(*it);
        currentList = &currentNode->children;
    }

    return currentNode;
}

std::vector<std::string> splitLines(const std::string & content)
{
    std::vector<std::string> lines;
    std::stringstream        stream(content);
    std::string              line;
    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }

    if (!content.empty() && content.back() == '\n')
    {
        // keep behavior stable for progress callback totals
    }

    return lines;
}

} // namespace

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

std::shared_ptr<LIdlFile> ParseResult::asIdlFile() const
{
    return std::dynamic_pointer_cast<LIdlFile>(astRoot);
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
        m_options = std::move(other.m_options);
        m_callback = std::move(other.m_callback);
        m_errors = std::move(other.m_errors);
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

namespace {

void addError(
    std::vector<ParseError> & errors,
    ParseErrorLevel          level,
    const std::string &      message,
    const std::string &      filePath,
    uint32_t                 line,
    uint32_t                 column)
{
    ParseError err;
    err.level = level;
    err.message = message;
    err.filePath = filePath;
    err.line = line;
    err.column = column;
    errors.push_back(std::move(err));
}

bool addStructWithConflictCheck(
    ParseContext &           context,
    std::vector<ParseError> & errors,
    const LIdlStruct &       st)
{
    const auto it = context.structOwners.find(st.fullName);
    if (it != context.structOwners.end())
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "duplicate struct definition: " + st.fullName,
            st.sourceFile,
            st.line,
            1
        );
        return false;
    }

    context.structOwners[st.fullName] = st.sourceFile;
    context.ast->structs.push_back(st);

    if (!st.packagePath.empty())
    {
        std::vector<std::string> packageStack;
        std::stringstream        stream(st.packagePath);
        std::string              part;
        while (std::getline(stream, part, ':'))
        {
            if (part.empty())
            {
                continue;
            }
            if (part == ":")
            {
                continue;
            }
            packageStack.push_back(part);
            if (stream.peek() == ':')
            {
                stream.get();
            }
        }

        if (!packageStack.empty())
        {
            LIdlPackage * pkg = ensurePackagePath(context.ast->packages, packageStack);
            if (pkg)
            {
                pkg->structNames.push_back(st.fullName);
            }
        }
    }

    return true;
}

bool addEnumWithConflictCheck(
    ParseContext &            context,
    std::vector<ParseError> & errors,
    const LIdlEnum &          en)
{
    const auto it = context.enumOwners.find(en.fullName);
    if (it != context.enumOwners.end())
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "duplicate enum definition: " + en.fullName,
            en.sourceFile,
            en.line,
            1
        );
        return false;
    }

    context.enumOwners[en.fullName] = en.sourceFile;
    context.ast->enums.push_back(en);
    return true;
}

bool addUnionWithConflictCheck(
    ParseContext &            context,
    std::vector<ParseError> & errors,
    const LIdlUnion &         un)
{
    const auto it = context.unionOwners.find(un.fullName);
    if (it != context.unionOwners.end())
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "duplicate union definition: " + un.fullName,
            un.sourceFile,
            un.line,
            1
        );
        return false;
    }

    context.unionOwners[un.fullName] = un.sourceFile;
    context.ast->unions.push_back(un);
    return true;
}

bool addTopicWithConflictCheck(
    ParseContext &            context,
    std::vector<ParseError> & errors,
    const LIdlTopic &         topic)
{
    const auto nameIt = context.topicNameOwners.find(topic.name);
    if (nameIt != context.topicNameOwners.end())
    {
        const auto exists = std::find_if(
            context.ast->topics.begin(),
            context.ast->topics.end(),
            [&topic](const LIdlTopic & item) {
                return item.name == topic.name;
            }
        );

        if (exists != context.ast->topics.end() && exists->typeName == topic.typeName)
        {
            return true;
        }

        addError(
            errors,
            ParseErrorLevel::Error,
            "topic name conflict: " + topic.name,
            topic.sourceFile,
            topic.line,
            1
        );
        return false;
    }

    const auto typeIt = context.topicTypeToName.find(topic.typeName);
    if (typeIt != context.topicTypeToName.end() && typeIt->second != topic.name)
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "type already bound to another topic: " + topic.typeName,
            topic.sourceFile,
            topic.line,
            1
        );
        return false;
    }

    context.topicNameOwners[topic.name] = topic.sourceFile;
    context.topicTypeToName[topic.typeName] = topic.name;
    context.ast->topics.push_back(topic);
    return true;
}

bool parseContent(
    const std::string &       content,
    const std::string &       sourceName,
    const fs::path &          sourceDir,
    uint32_t                  depth,
    const ParseOptions &      options,
    const ParseProgressCallback & callback,
    std::vector<ParseError> & errors,
    ParseContext &            context,
    const std::function<bool(const std::string &, uint32_t)> & parseIncludeFile)
{
    LineState state;

    const auto lines = splitLines(stripBom(content));
    const uint32_t totalLines = static_cast<uint32_t>(lines.size());

    const std::regex includePattern(R"(^\s*#include\s*[<\"]([^>\"]+)[>\"]\s*;?\s*$)");
    const std::regex packagePattern(R"(^\s*package\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\{)?\s*;?\s*$)");
    const std::regex structPattern(R"(^\s*struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*extend\s+([A-Za-z_][A-Za-z0-9_:]*))?\s*(\{)?\s*$)");
    const std::regex enumPattern(R"(^\s*enum\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\{)?\s*$)");
    const std::regex enumValuePattern(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([-+]?[0-9]+))?\s*,?\s*$)");
    const std::regex unionPattern(R"(^\s*union\s+([A-Za-z_][A-Za-z0-9_]*)\s+switch\s*\(\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)\s*(\{)?\s*$)");
    const std::regex caseLabelPattern(R"(^\s*case\s+([-+]?[0-9]+)\s*:\s*$)");
    const std::regex defaultLabelPattern(R"(^\s*default\s*:\s*$)");
    const std::regex caseInlineFieldPattern(
        R"(^\s*case\s+([-+]?[0-9]+)\s*:\s*([^;]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*$)");
    const std::regex defaultInlineFieldPattern(
        R"(^\s*default\s*:\s*([^;]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*$)");
    const std::regex topicPattern(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_:]*)\s*;?\s*$)");
    const std::regex fieldPattern(R"(^\s*([^;]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;\s*$)");

    for (uint32_t idx = 0; idx < totalLines; ++idx)
    {
        const uint32_t lineNo = idx + 1;
        if (callback)
        {
            callback(sourceName, lineNo, totalLines);
        }

        std::string rawLine = lines[idx];
        std::string inlineComment;
        std::string code = stripInlineComment(rawLine, inlineComment);

        if (!options.ignoreComments && !inlineComment.empty() && code.empty())
        {
            appendComment(state.pendingComment, inlineComment);
            continue;
        }

        if (state.waitingPackageBrace)
        {
            if (code == "{")
            {
                state.packageStack.push_back(state.waitingPackageName);
                LIdlPackage * pkg = ensurePackagePath(context.ast->packages, state.packageStack);
                if (pkg && pkg->comment.empty() && !state.pendingComment.empty())
                {
                    pkg->comment = state.pendingComment;
                }
                state.waitingPackageBrace = false;
                state.waitingPackageName.clear();
                state.pendingComment.clear();
                continue;
            }
        }

        if (state.waitingStructBrace)
        {
            if (code == "{")
            {
                state.currentStruct = state.pendingStruct;
                state.pendingStruct = LIdlStruct();
                state.waitingStructBrace = false;
                state.inStruct = true;
                continue;
            }
        }

        if (state.waitingEnumBrace)
        {
            if (code == "{")
            {
                state.currentEnum = state.pendingEnum;
                state.pendingEnum = LIdlEnum();
                state.waitingEnumBrace = false;
                state.inEnum = true;
                state.nextEnumValue = 0;
                continue;
            }
        }

        if (state.waitingUnionBrace)
        {
            if (code == "{")
            {
                state.currentUnion = state.pendingUnion;
                state.pendingUnion = LIdlUnion();
                state.waitingUnionBrace = false;
                state.inUnion = true;
                state.pendingUnionLabels.clear();
                state.pendingUnionDefault = false;
                continue;
            }
        }

        if (code.empty())
        {
            if (!options.ignoreComments && !inlineComment.empty())
            {
                appendComment(state.pendingComment, inlineComment);
            }
            continue;
        }

        if (state.inStruct)
        {
            if (code[0] == '[')
            {
                const auto closePos = code.find(']');
                if (closePos == std::string::npos)
                {
                    addError(errors, ParseErrorLevel::Error, "unterminated attribute", sourceName, lineNo, 1);
                    continue;
                }

                const std::string rawAttr = code.substr(1, closePos - 1);
                state.pendingFieldAttributes.push_back(parseAttributeRaw(rawAttr));
                continue;
            }

            if (code[0] == '}')
            {
                addStructWithConflictCheck(context, errors, state.currentStruct);
                state.currentStruct = LIdlStruct();
                state.pendingFieldAttributes.clear();
                state.inStruct = false;
                continue;
            }

            std::smatch fieldMatch;
            if (std::regex_match(code, fieldMatch, fieldPattern))
            {
                LIdlField field;
                field.typeName = trim(fieldMatch[1].str());
                field.name = trim(fieldMatch[2].str());
                field.line = lineNo;
                field.attributes = state.pendingFieldAttributes;
                std::string sequenceElementType;
                int32_t sequenceBound = -1;
                if (parseSequenceType(field.typeName, sequenceElementType, sequenceBound))
                {
                    field.isSequence = true;
                    field.sequenceElementType = sequenceElementType;
                    field.sequenceBound = sequenceBound;
                }

                for (const auto & attr : field.attributes)
                {
                    if (field.defaultValue.empty() && !attr.defaultValue.empty())
                    {
                        field.defaultValue = attr.defaultValue;
                    }
                    if (field.comment.empty() && !attr.comment.empty())
                    {
                        field.comment = attr.comment;
                    }
                }

                if (!inlineComment.empty())
                {
                    field.comment = inlineComment;
                }
                else if (field.comment.empty() && !state.pendingComment.empty())
                {
                    field.comment = state.pendingComment;
                }

                state.currentStruct.fields.push_back(std::move(field));
                state.pendingFieldAttributes.clear();
                state.pendingComment.clear();
                continue;
            }

            if (options.strictMode)
            {
                addError(
                    errors,
                    ParseErrorLevel::Warning,
                    "unrecognized struct line: " + code,
                    sourceName,
                    lineNo,
                    1
                );
            }
            continue;
        }

        if (state.inEnum)
        {
            if (!code.empty() && code[0] == '}')
            {
                addEnumWithConflictCheck(context, errors, state.currentEnum);
                state.currentEnum = LIdlEnum();
                state.inEnum = false;
                state.nextEnumValue = 0;
                continue;
            }

            std::smatch enumValueMatch;
            if (std::regex_match(code, enumValueMatch, enumValuePattern))
            {
                LIdlEnumValue enumValue;
                enumValue.name = trim(enumValueMatch[1].str());
                enumValue.line = lineNo;
                enumValue.comment = inlineComment;

                if (enumValueMatch[2].matched)
                {
                    int64_t parsedValue = 0;
                    if (!parseSignedInt64(enumValueMatch[2].str(), parsedValue))
                    {
                        addError(
                            errors,
                            ParseErrorLevel::Error,
                            "invalid enum value: " + enumValueMatch[2].str(),
                            sourceName,
                            lineNo,
                            1);
                        continue;
                    }
                    enumValue.hasExplicitValue = true;
                    enumValue.value = parsedValue;
                    state.nextEnumValue = parsedValue + 1;
                }
                else
                {
                    enumValue.value = state.nextEnumValue++;
                    enumValue.hasExplicitValue = false;
                }

                state.currentEnum.values.push_back(std::move(enumValue));
                continue;
            }

            if (options.strictMode)
            {
                addError(
                    errors,
                    ParseErrorLevel::Warning,
                    "unrecognized enum line: " + code,
                    sourceName,
                    lineNo,
                    1);
            }
            continue;
        }

        if (state.inUnion)
        {
            if (!code.empty() && code[0] == '}')
            {
                addUnionWithConflictCheck(context, errors, state.currentUnion);
                state.currentUnion = LIdlUnion();
                state.inUnion = false;
                state.pendingUnionLabels.clear();
                state.pendingUnionDefault = false;
                continue;
            }

            std::smatch unionMatch;
            if (std::regex_match(code, unionMatch, caseInlineFieldPattern) ||
                std::regex_match(code, unionMatch, defaultInlineFieldPattern))
            {
                LIdlUnionCase unionCase;
                std::string typeText;
                std::string nameText;

                if (unionMatch.size() == 4)
                {
                    int64_t labelValue = 0;
                    if (!parseSignedInt64(unionMatch[1].str(), labelValue))
                    {
                        addError(
                            errors,
                            ParseErrorLevel::Error,
                            "invalid union case label: " + unionMatch[1].str(),
                            sourceName,
                            lineNo,
                            1);
                        continue;
                    }
                    unionCase.labels.push_back(labelValue);
                    typeText = trim(unionMatch[2].str());
                    nameText = trim(unionMatch[3].str());
                }
                else if (unionMatch.size() == 3)
                {
                    unionCase.isDefault = true;
                    typeText = trim(unionMatch[1].str());
                    nameText = trim(unionMatch[2].str());
                }

                unionCase.field.typeName = typeText;
                unionCase.field.name = nameText;
                unionCase.field.line = lineNo;
                std::string sequenceElementType;
                int32_t sequenceBound = -1;
                if (parseSequenceType(unionCase.field.typeName, sequenceElementType, sequenceBound))
                {
                    unionCase.field.isSequence = true;
                    unionCase.field.sequenceElementType = sequenceElementType;
                    unionCase.field.sequenceBound = sequenceBound;
                }
                state.currentUnion.cases.push_back(std::move(unionCase));
                continue;
            }

            if (std::regex_match(code, unionMatch, caseLabelPattern))
            {
                int64_t labelValue = 0;
                if (!parseSignedInt64(unionMatch[1].str(), labelValue))
                {
                    addError(
                        errors,
                        ParseErrorLevel::Error,
                        "invalid union case label: " + unionMatch[1].str(),
                        sourceName,
                        lineNo,
                        1);
                    continue;
                }
                state.pendingUnionLabels.push_back(labelValue);
                state.pendingUnionDefault = false;
                continue;
            }

            if (std::regex_match(code, unionMatch, defaultLabelPattern))
            {
                state.pendingUnionDefault = true;
                continue;
            }

            if (std::regex_match(code, unionMatch, fieldPattern))
            {
                if (state.pendingUnionLabels.empty() && !state.pendingUnionDefault)
                {
                    addError(
                        errors,
                        ParseErrorLevel::Error,
                        "union field requires case/default label",
                        sourceName,
                        lineNo,
                        1);
                    continue;
                }

                LIdlUnionCase unionCase;
                unionCase.labels = state.pendingUnionLabels;
                unionCase.isDefault = state.pendingUnionDefault;
                unionCase.field.typeName = trim(unionMatch[1].str());
                unionCase.field.name = trim(unionMatch[2].str());
                unionCase.field.line = lineNo;
                unionCase.field.comment = inlineComment;
                std::string sequenceElementType;
                int32_t sequenceBound = -1;
                if (parseSequenceType(unionCase.field.typeName, sequenceElementType, sequenceBound))
                {
                    unionCase.field.isSequence = true;
                    unionCase.field.sequenceElementType = sequenceElementType;
                    unionCase.field.sequenceBound = sequenceBound;
                }

                state.currentUnion.cases.push_back(std::move(unionCase));
                state.pendingUnionLabels.clear();
                state.pendingUnionDefault = false;
                continue;
            }

            if (options.strictMode)
            {
                addError(
                    errors,
                    ParseErrorLevel::Warning,
                    "unrecognized union line: " + code,
                    sourceName,
                    lineNo,
                    1);
            }
            continue;
        }

        std::smatch match;
        if (std::regex_match(code, match, includePattern))
        {
            if (depth >= options.maxIncludeDepth)
            {
                addError(
                    errors,
                    ParseErrorLevel::Fatal,
                    "include depth exceeded",
                    sourceName,
                    lineNo,
                    1
                );
                return false;
            }

            const std::string includeToken = trim(match[1].str());
            const std::string includePath = resolveIncludePath(includeToken, sourceDir, options);
            if (includePath.empty())
            {
                addError(
                    errors,
                    ParseErrorLevel::Error,
                    "include file not found: " + includeToken,
                    sourceName,
                    lineNo,
                    1
                );
                continue;
            }

            context.ast->includeFiles.push_back(includePath);
            if (!parseIncludeFile(includePath, depth + 1))
            {
                return false;
            }
            continue;
        }

        if (std::regex_match(code, match, packagePattern))
        {
            const std::string packageName = trim(match[1].str());
            const bool hasBrace = match[2].matched;
            if (hasBrace)
            {
                state.packageStack.push_back(packageName);
                LIdlPackage * pkg = ensurePackagePath(context.ast->packages, state.packageStack);
                if (pkg && pkg->comment.empty() && !state.pendingComment.empty())
                {
                    pkg->comment = state.pendingComment;
                }
                state.pendingComment.clear();
            }
            else
            {
                state.waitingPackageBrace = true;
                state.waitingPackageName = packageName;
            }
            continue;
        }

        if (std::regex_match(code, match, structPattern))
        {
            LIdlStruct st;
            st.name = trim(match[1].str());
            st.parentType = match[2].matched ? trim(match[2].str()) : std::string();
            st.packagePath = joinPackage(state.packageStack);
            st.fullName = st.packagePath.empty() ? st.name : (st.packagePath + "::" + st.name);
            st.sourceFile = sourceName;
            st.line = lineNo;
            st.comment = state.pendingComment;
            state.pendingComment.clear();

            const bool hasBrace = match[3].matched;
            if (hasBrace)
            {
                state.currentStruct = std::move(st);
                state.inStruct = true;
                state.pendingFieldAttributes.clear();
            }
            else
            {
                state.pendingStruct = std::move(st);
                state.waitingStructBrace = true;
            }
            continue;
        }

        if (std::regex_match(code, match, enumPattern))
        {
            LIdlEnum en;
            en.name = trim(match[1].str());
            en.packagePath = joinPackage(state.packageStack);
            en.fullName = en.packagePath.empty() ? en.name : (en.packagePath + "::" + en.name);
            en.sourceFile = sourceName;
            en.line = lineNo;
            en.comment = state.pendingComment;
            state.pendingComment.clear();

            const bool hasBrace = match[2].matched;
            if (hasBrace)
            {
                state.currentEnum = std::move(en);
                state.inEnum = true;
                state.nextEnumValue = 0;
            }
            else
            {
                state.pendingEnum = std::move(en);
                state.waitingEnumBrace = true;
            }
            continue;
        }

        if (std::regex_match(code, match, unionPattern))
        {
            LIdlUnion un;
            un.name = trim(match[1].str());
            un.discriminatorType = trim(match[2].str());
            un.packagePath = joinPackage(state.packageStack);
            un.fullName = un.packagePath.empty() ? un.name : (un.packagePath + "::" + un.name);
            un.sourceFile = sourceName;
            un.line = lineNo;
            un.comment = state.pendingComment;
            state.pendingComment.clear();

            const bool hasBrace = match[3].matched;
            if (hasBrace)
            {
                state.currentUnion = std::move(un);
                state.inUnion = true;
                state.pendingUnionLabels.clear();
                state.pendingUnionDefault = false;
            }
            else
            {
                state.pendingUnion = std::move(un);
                state.waitingUnionBrace = true;
            }
            continue;
        }

        if (code[0] == '[')
        {
            const auto closePos = code.rfind(']');
            if (closePos != std::string::npos)
            {
                const std::string bracketText = trim(code.substr(1, closePos - 1));
                auto tokens = splitCommaRespectingQuotes(bracketText);
                if (!tokens.empty())
                {
                    state.pendingTopicComment = unquote(tokens[0]);
                }
                else
                {
                    state.pendingTopicComment = unquote(bracketText);
                }
                continue;
            }
        }

        if (std::regex_match(code, match, topicPattern))
        {
            LIdlTopic topic;
            topic.name = trim(match[1].str());
            topic.typeName = trim(match[2].str());
            topic.sourceFile = sourceName;
            topic.line = lineNo;

            if (!inlineComment.empty())
            {
                topic.comment = inlineComment;
            }
            else if (!state.pendingTopicComment.empty())
            {
                topic.comment = state.pendingTopicComment;
            }
            else
            {
                topic.comment = state.pendingComment;
            }

            state.pendingTopicComment.clear();
            state.pendingComment.clear();
            addTopicWithConflictCheck(context, errors, topic);
            continue;
        }

        if (code[0] == '}')
        {
            if (!state.packageStack.empty())
            {
                state.packageStack.pop_back();
            }
            continue;
        }

        if (!options.ignoreComments && !inlineComment.empty())
        {
            appendComment(state.pendingComment, inlineComment);
        }

        if (options.strictMode)
        {
            addError(
                errors,
                ParseErrorLevel::Warning,
                "unrecognized line: " + code,
                sourceName,
                lineNo,
                1
            );
        }
    }

    if (state.inStruct)
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "unterminated struct declaration: " + state.currentStruct.fullName,
            sourceName,
            state.currentStruct.line,
            1
        );
    }
    if (state.inEnum)
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "unterminated enum declaration: " + state.currentEnum.fullName,
            sourceName,
            state.currentEnum.line,
            1
        );
    }
    if (state.inUnion)
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "unterminated union declaration: " + state.currentUnion.fullName,
            sourceName,
            state.currentUnion.line,
            1
        );
    }
    if (state.waitingStructBrace || state.waitingEnumBrace || state.waitingUnionBrace)
    {
        addError(
            errors,
            ParseErrorLevel::Error,
            "declaration missing opening brace",
            sourceName,
            totalLines,
            1
        );
    }

    return true;
}

bool parseFileRecursive(
    const std::string &       filePath,
    uint32_t                  depth,
    const ParseOptions &      options,
    const ParseProgressCallback & callback,
    std::vector<ParseError> & errors,
    ParseContext &            context)
{
    std::error_code ec;
    const fs::path normalized = fs::weakly_canonical(fs::path(filePath), ec);
    const std::string key = ec ? fs::path(filePath).lexically_normal().string() : normalized.string();

    if (context.parsedFiles.find(key) != context.parsedFiles.end())
    {
        return true;
    }

    if (depth > options.maxIncludeDepth)
    {
        addError(errors, ParseErrorLevel::Fatal, "include depth exceeded", filePath, 0, 0);
        return false;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open())
    {
        addError(errors, ParseErrorLevel::Fatal, "failed to open file", filePath, 0, 0);
        return false;
    }

    std::string content(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );

    context.parsedFiles.insert(key);
    if (context.ast->sourcePath.empty())
    {
        context.ast->sourcePath = key;
    }

    const fs::path currentDir = fs::path(filePath).parent_path();

    auto includeParser = [&](const std::string & includePath, uint32_t includeDepth) -> bool {
        return parseFileRecursive(
            includePath,
            includeDepth,
            options,
            callback,
            errors,
            context
        );
    };

    return parseContent(
        content,
        key,
        currentDir,
        depth,
        options,
        callback,
        errors,
        context,
        includeParser
    );
}

void assignTopicIds(std::vector<LIdlTopic> & topics)
{
    uint32_t nextId = 1;
    for (auto & topic : topics)
    {
        topic.id = nextId++;
    }
}

bool hasFatalOrError(const std::vector<ParseError> & errors)
{
    return std::any_of(
        errors.begin(),
        errors.end(),
        [](const ParseError & err) {
            return err.level == ParseErrorLevel::Error || err.level == ParseErrorLevel::Fatal;
        }
    );
}

} // namespace

ParseResult LIdlParser::parse(const std::string & filePath)
{
    m_errors.clear();
    ParseResult result;

    const auto start = std::chrono::steady_clock::now();

    ParseContext context;
    context.ast = std::make_shared<LIdlFile>();

    parseFileRecursive(filePath, 0, m_options, m_callback, m_errors, context);

    assignTopicIds(context.ast->topics);

    result.astRoot = context.ast;
    result.errors = m_errors;
    result.success = !hasFatalOrError(result.errors);

    const auto end = std::chrono::steady_clock::now();
    result.parseTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

ParseResult LIdlParser::parseString(const std::string & idlContent, const std::string & sourceName)
{
    m_errors.clear();
    ParseResult result;

    const auto start = std::chrono::steady_clock::now();

    ParseContext context;
    context.ast = std::make_shared<LIdlFile>();
    context.ast->sourcePath = sourceName;

    const fs::path sourceDir = fs::path(sourceName).parent_path();

    auto includeParser = [&](const std::string & includePath, uint32_t includeDepth) -> bool {
        return parseFileRecursive(
            includePath,
            includeDepth,
            m_options,
            m_callback,
            m_errors,
            context
        );
    };

    parseContent(
        idlContent,
        sourceName,
        sourceDir,
        0,
        m_options,
        m_callback,
        m_errors,
        context,
        includeParser
    );

    assignTopicIds(context.ast->topics);

    result.astRoot = context.ast;
    result.errors = m_errors;
    result.success = !hasFatalOrError(result.errors);

    const auto end = std::chrono::steady_clock::now();
    result.parseTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

ParseResult LIdlParser::parseMultiple(const std::vector<std::string> & filePaths)
{
    m_errors.clear();
    ParseResult result;

    const auto start = std::chrono::steady_clock::now();

    ParseContext context;
    context.ast = std::make_shared<LIdlFile>();

    for (const auto & filePath : filePaths)
    {
        parseFileRecursive(filePath, 0, m_options, m_callback, m_errors, context);
    }

    assignTopicIds(context.ast->topics);

    result.astRoot = context.ast;
    result.errors = m_errors;
    result.success = !hasFatalOrError(result.errors);

    const auto end = std::chrono::steady_clock::now();
    result.parseTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
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
