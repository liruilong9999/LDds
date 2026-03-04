#include "LIdlGenerator.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "LIdlParser.h"

namespace LDdsFramework {
namespace {

namespace fs = std::filesystem;

struct TypeInfo
{
    enum class Kind
    {
        Primitive,
        String,
        Vector,
        Custom
    };

    Kind        kind;
    std::string cppType;
    std::string elementType;
    Kind        elementKind;
    std::string elementCppType;

    TypeInfo()
        : kind(Kind::Custom)
        , cppType()
        , elementType()
        , elementKind(Kind::Custom)
        , elementCppType()
    {
    }
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

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string sanitizeName(const std::string & text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
        {
            out.push_back(ch);
        }
        else
        {
            out.push_back('_');
        }
    }

    while (!out.empty() && out.back() == '_')
    {
        out.pop_back();
    }

    if (out.empty())
    {
        out = "idl";
    }

    return out;
}

std::string toUpper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

std::string toPascalCase(const std::string & text)
{
    std::string out;
    bool upperNext = true;
    for (char ch : text)
    {
        if (!std::isalnum(static_cast<unsigned char>(ch)))
        {
            upperNext = true;
            continue;
        }

        if (upperNext)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            upperNext = false;
        }
        else
        {
            out.push_back(ch);
        }
    }

    if (out.empty())
    {
        out = "Idl";
    }

    return out;
}

std::vector<std::string> splitNs(const std::string & ns)
{
    std::vector<std::string> parts;
    if (ns.empty())
    {
        return parts;
    }

    size_t pos = 0;
    while (pos < ns.size())
    {
        size_t next = ns.find("::", pos);
        if (next == std::string::npos)
        {
            parts.push_back(ns.substr(pos));
            break;
        }
        parts.push_back(ns.substr(pos, next - pos));
        pos = next + 2;
    }
    return parts;
}

TypeInfo parseType(const std::string & rawType)
{
    static const std::unordered_map<std::string, std::string> primitives = {
        {"bool", "bool"},
        {"char", "char"},
        {"int8", "int8_t"},
        {"uint8", "uint8_t"},
        {"int16", "int16_t"},
        {"uint16", "uint16_t"},
        {"int32", "int32_t"},
        {"uint32", "uint32_t"},
        {"int64", "int64_t"},
        {"uint64", "uint64_t"},
        {"int", "int32_t"},
        {"unsigned", "uint32_t"},
        {"short", "int16_t"},
        {"long", "int64_t"},
        {"float", "float"},
        {"double", "double"}
    };

    TypeInfo info;
    const std::string type = trim(rawType);

    const std::string lowerType = toLower(type);
    if (lowerType == "string")
    {
        info.kind = TypeInfo::Kind::String;
        info.cppType = "std::string";
        return info;
    }

    const std::string vectorPrefix = "vector<";
    if (lowerType.rfind(vectorPrefix, 0) == 0 && !type.empty() && type.back() == '>')
    {
        const std::string elemRaw = trim(type.substr(vectorPrefix.size(), type.size() - vectorPrefix.size() - 1));
        TypeInfo elemInfo = parseType(elemRaw);

        info.kind = TypeInfo::Kind::Vector;
        info.elementType = elemRaw;
        info.elementKind = elemInfo.kind;
        info.elementCppType = elemInfo.cppType;
        info.cppType = "std::vector<" + elemInfo.cppType + ">";
        return info;
    }

    const auto it = primitives.find(lowerType);
    if (it != primitives.end())
    {
        info.kind = TypeInfo::Kind::Primitive;
        info.cppType = it->second;
        return info;
    }

    info.kind = TypeInfo::Kind::Custom;
    info.cppType = type;
    return info;
}

std::string toStringLiteral(const std::string & text)
{
    std::string out = "\"";
    out.reserve(text.size() + 2);
    for (char ch : text)
    {
        if (ch == '\\' || ch == '\"')
        {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('\"');
    return out;
}

std::string normalizeDefaultValue(const std::string & rawValue, const TypeInfo & typeInfo)
{
    if (rawValue.empty())
    {
        return std::string();
    }

    std::string value = trim(rawValue);
    if (typeInfo.kind == TypeInfo::Kind::String)
    {
        value.erase(std::remove(value.begin(), value.end(), '\"'), value.end());
        return toStringLiteral(value);
    }

    return value;
}

void openNamespaces(std::ostream & out, const std::vector<std::string> & nsParts)
{
    for (const auto & part : nsParts)
    {
        out << "namespace " << part << " {\n";
    }
}

void closeNamespaces(std::ostream & out, const std::vector<std::string> & nsParts)
{
    for (auto it = nsParts.rbegin(); it != nsParts.rend(); ++it)
    {
        out << "} // namespace " << *it << "\n";
    }
}

std::vector<size_t> topologicalStructOrder(const std::vector<LIdlStruct> & structs)
{
    std::unordered_map<std::string, size_t> indexByName;
    for (size_t i = 0; i < structs.size(); ++i)
    {
        indexByName[structs[i].fullName] = i;
    }

    std::vector<size_t> order;
    std::vector<int>    marks(structs.size(), 0);

    std::function<void(size_t)> dfs = [&](size_t idx) {
        if (marks[idx] == 2)
        {
            return;
        }
        if (marks[idx] == 1)
        {
            return;
        }

        marks[idx] = 1;
        const auto it = indexByName.find(structs[idx].parentType);
        if (it != indexByName.end())
        {
            dfs(it->second);
        }
        marks[idx] = 2;
        order.push_back(idx);
    };

    for (size_t i = 0; i < structs.size(); ++i)
    {
        dfs(i);
    }

    return order;
}

std::string generateExportHeader(const std::string & prefix)
{
    const std::string guard = toUpper(prefix) + "_EXPORT_H";
    const std::string macro = toUpper(prefix) + "_IDL_API";
    const std::string exports = toUpper(prefix) + "_IDL_EXPORTS";

    std::ostringstream out;
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#ifdef _WIN32\n";
    out << "#  ifdef " << exports << "\n";
    out << "#    define " << macro << " __declspec(dllexport)\n";
    out << "#  else\n";
    out << "#    define " << macro << " __declspec(dllimport)\n";
    out << "#  endif\n";
    out << "#else\n";
    out << "#  define " << macro << "\n";
    out << "#endif\n\n";
    out << "#endif // " << guard << "\n";
    return out.str();
}

std::string generateDefineHeader(
    const std::string & prefix,
    const LIdlFile &    file)
{
    const std::string guard = toUpper(prefix) + "_DEFINE_H";
    const std::string exportMacro = toUpper(prefix) + "_IDL_API";

    std::unordered_map<std::string, uint32_t> topicByType;
    for (const auto & topic : file.topics)
    {
        topicByType[topic.typeName] = topic.id;
    }

    std::ostringstream out;
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <cstdint>\n";
    out << "#include <cstring>\n";
    out << "#include <string>\n";
    out << "#include <type_traits>\n";
    out << "#include <vector>\n\n";
    out << "#include \"" << prefix << "_export.h\"\n";
    out << "#include \"LByteBuffer.h\"\n\n";
    out << "namespace LDdsFramework {\n";
    out << "namespace idl_detail {\n";
    out << "template<typename T>\n";
    out << "inline void writePod(LByteBuffer & buffer, const T & value)\n";
    out << "{\n";
    out << "    static_assert(std::is_trivially_copyable<T>::value, \"writePod requires POD type\");\n";
    out << "    buffer.writeBytes(&value, sizeof(T));\n";
    out << "}\n\n";
    out << "template<typename T>\n";
    out << "inline bool readPod(const std::vector<uint8_t> & data, size_t & offset, T & value)\n";
    out << "{\n";
    out << "    static_assert(std::is_trivially_copyable<T>::value, \"readPod requires POD type\");\n";
    out << "    if (offset + sizeof(T) > data.size()) { return false; }\n";
    out << "    std::memcpy(&value, data.data() + offset, sizeof(T));\n";
    out << "    offset += sizeof(T);\n";
    out << "    return true;\n";
    out << "}\n\n";
    out << "inline void writeString(LByteBuffer & buffer, const std::string & value)\n";
    out << "{\n";
    out << "    const uint32_t size = static_cast<uint32_t>(value.size());\n";
    out << "    buffer.writeUInt32(size);\n";
    out << "    if (size > 0) { buffer.writeBytes(value.data(), size); }\n";
    out << "}\n\n";
    out << "inline bool readString(const std::vector<uint8_t> & data, size_t & offset, std::string & value)\n";
    out << "{\n";
    out << "    uint32_t size = 0;\n";
    out << "    if (!readPod(data, offset, size)) { return false; }\n";
    out << "    if (offset + size > data.size()) { return false; }\n";
    out << "    value.assign(reinterpret_cast<const char *>(data.data() + offset), size);\n";
    out << "    offset += size;\n";
    out << "    return true;\n";
    out << "}\n";
    out << "} // namespace idl_detail\n";
    out << "} // namespace LDdsFramework\n\n";

    const auto order = topologicalStructOrder(file.structs);
    for (size_t ordIdx = 0; ordIdx < order.size(); ++ordIdx)
    {
        const auto & st = file.structs[order[ordIdx]];
        const auto nsParts = splitNs(st.packagePath);
        openNamespaces(out, nsParts);

        if (!st.comment.empty())
        {
            out << "// " << st.comment << "\n";
        }

        out << "struct " << exportMacro << " " << st.name;
        if (!st.parentType.empty())
        {
            out << " : public " << st.parentType;
        }
        out << "\n{\n";

        std::vector<std::pair<std::string, std::string>> ctorInits;
        if (!st.parentType.empty())
        {
            ctorInits.emplace_back(st.parentType + "()", std::string());
        }

        for (const auto & field : st.fields)
        {
            TypeInfo info = parseType(field.typeName);
            const std::string defaultValue = normalizeDefaultValue(field.defaultValue, info);
            if (!defaultValue.empty())
            {
                ctorInits.emplace_back(field.name + "(" + defaultValue + ")", std::string());
            }
        }

        if (ctorInits.empty())
        {
            out << "    " << st.name << "() = default;\n";
        }
        else
        {
            out << "    " << st.name << "() : ";
            for (size_t i = 0; i < ctorInits.size(); ++i)
            {
                if (i > 0)
                {
                    out << ", ";
                }
                out << ctorInits[i].first;
            }
            out << " {}\n";
        }

        for (const auto & field : st.fields)
        {
            TypeInfo info = parseType(field.typeName);
            out << "    " << info.cppType << " " << field.name << ";";
            if (!field.comment.empty())
            {
                out << " // " << field.comment;
            }
            out << "\n";
        }
        out << "\n";

        out << "    void serialize(LDdsFramework::LByteBuffer & buffer) const\n";
        out << "    {\n";
        if (!st.parentType.empty())
        {
            out << "        " << st.parentType << "::serialize(buffer);\n";
        }

        for (const auto & field : st.fields)
        {
            const TypeInfo info = parseType(field.typeName);
            if (info.kind == TypeInfo::Kind::Primitive)
            {
                out << "        LDdsFramework::idl_detail::writePod(buffer, " << field.name << ");\n";
            }
            else if (info.kind == TypeInfo::Kind::String)
            {
                out << "        LDdsFramework::idl_detail::writeString(buffer, " << field.name << ");\n";
            }
            else if (info.kind == TypeInfo::Kind::Vector)
            {
                out << "        buffer.writeUInt32(static_cast<uint32_t>(" << field.name << ".size()));\n";
                out << "        for (const auto & item : " << field.name << ")\n";
                out << "        {\n";
                if (info.elementKind == TypeInfo::Kind::Primitive)
                {
                    out << "            LDdsFramework::idl_detail::writePod(buffer, item);\n";
                }
                else if (info.elementKind == TypeInfo::Kind::String)
                {
                    out << "            LDdsFramework::idl_detail::writeString(buffer, item);\n";
                }
                else
                {
                    out << "            item.serialize(buffer);\n";
                }
                out << "        }\n";
            }
            else
            {
                out << "        " << field.name << ".serialize(buffer);\n";
            }
        }
        out << "    }\n\n";

        out << "    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)\n";
        out << "    {\n";
        if (!st.parentType.empty())
        {
            out << "        if (!" << st.parentType << "::deserialize(data, offset)) { return false; }\n";
        }

        for (const auto & field : st.fields)
        {
            const TypeInfo info = parseType(field.typeName);
            if (info.kind == TypeInfo::Kind::Primitive)
            {
                out << "        if (!LDdsFramework::idl_detail::readPod(data, offset, " << field.name << ")) { return false; }\n";
            }
            else if (info.kind == TypeInfo::Kind::String)
            {
                out << "        if (!LDdsFramework::idl_detail::readString(data, offset, " << field.name << ")) { return false; }\n";
            }
            else if (info.kind == TypeInfo::Kind::Vector)
            {
                out << "        uint32_t " << field.name << "Size = 0;\n";
                out << "        if (!LDdsFramework::idl_detail::readPod(data, offset, " << field.name << "Size)) { return false; }\n";
                out << "        " << field.name << ".clear();\n";
                out << "        " << field.name << ".reserve(" << field.name << "Size);\n";
                out << "        for (uint32_t i = 0; i < " << field.name << "Size; ++i)\n";
                out << "        {\n";
                out << "            " << info.elementCppType << " item{};\n";
                if (info.elementKind == TypeInfo::Kind::Primitive)
                {
                    out << "            if (!LDdsFramework::idl_detail::readPod(data, offset, item)) { return false; }\n";
                }
                else if (info.elementKind == TypeInfo::Kind::String)
                {
                    out << "            if (!LDdsFramework::idl_detail::readString(data, offset, item)) { return false; }\n";
                }
                else
                {
                    out << "            if (!item.deserialize(data, offset)) { return false; }\n";
                }
                out << "            " << field.name << ".push_back(std::move(item));\n";
                out << "        }\n";
            }
            else
            {
                out << "        if (!" << field.name << ".deserialize(data, offset)) { return false; }\n";
            }
        }
        out << "        return true;\n";
        out << "    }\n\n";

        out << "    bool deserialize(const std::vector<uint8_t> & data)\n";
        out << "    {\n";
        out << "        size_t offset = 0;\n";
        out << "        return deserialize(data, offset) && offset == data.size();\n";
        out << "    }\n\n";

        out << "    std::vector<uint8_t> serialize() const\n";
        out << "    {\n";
        out << "        LDdsFramework::LByteBuffer buffer;\n";
        out << "        serialize(buffer);\n";
        out << "        return std::vector<uint8_t>(buffer.data(), buffer.data() + buffer.size());\n";
        out << "    }\n\n";

        const auto topicIt = topicByType.find(st.fullName);
        const uint32_t topicId = (topicIt == topicByType.end()) ? 0U : topicIt->second;
        out << "    static uint32_t getTypeId() noexcept { return " << topicId << "U; }\n";
        out << "    static const char * getTypeName() noexcept { return \"" << st.fullName << "\"; }\n";

        out << "};\n";

        closeNamespaces(out, nsParts);
        out << "\n";
    }

    out << "#endif // " << guard << "\n";
    return out.str();
}

std::string generateTopicHeader(const std::string & prefix, const LIdlFile & file)
{
    const std::string guard = toUpper(prefix) + "_TOPIC_H";
    const std::string exportMacro = toUpper(prefix) + "_IDL_API";
    const std::string enumName = toPascalCase(prefix) + "TopicId";
    const std::string registerFn = "register" + toPascalCase(prefix) + "Types";

    std::ostringstream out;
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <cstdint>\n\n";
    out << "#include \"" << prefix << "_export.h\"\n";
    out << "#include \"" << prefix << "_define.h\"\n";
    out << "#include \"LTypeRegistry.h\"\n\n";

    out << "namespace LDdsFramework {\n";
    out << "enum class " << enumName << " : uint32_t\n";
    out << "{\n";
    if (file.topics.empty())
    {
        out << "    Invalid = 0\n";
    }
    else
    {
        out << "    Invalid = 0,\n";
        for (size_t i = 0; i < file.topics.size(); ++i)
        {
            const auto & topic = file.topics[i];
            out << "    " << topic.name << " = " << topic.id;
            if (i + 1 < file.topics.size())
            {
                out << ",";
            }
            if (!topic.comment.empty())
            {
                out << " // " << topic.comment;
            }
            out << "\n";
        }
    }
    out << "};\n\n";
    out << exportMacro << " void " << registerFn << "(LTypeRegistry & registry);\n";
    out << "} // namespace LDdsFramework\n\n";
    out << "#endif // " << guard << "\n";
    return out.str();
}

std::string generateTopicCpp(const std::string & prefix, const LIdlFile & file)
{
    const std::string enumName = toPascalCase(prefix) + "TopicId";
    const std::string registerFn = "register" + toPascalCase(prefix) + "Types";

    std::unordered_map<std::string, const LIdlTopic *> topicByType;
    for (const auto & topic : file.topics)
    {
        topicByType[topic.typeName] = &topic;
    }

    std::ostringstream out;
    out << "#include \"" << prefix << "_topic.h\"\n\n";
    out << "namespace LDdsFramework {\n";
    out << "void " << registerFn << "(LTypeRegistry & registry)\n";
    out << "{\n";

    for (const auto & st : file.structs)
    {
        const auto it = topicByType.find(st.fullName);
        if (it == topicByType.end())
        {
            continue;
        }

        const auto * topic = it->second;
        out << "    registry.registerType<" << st.fullName << ">(\n";
        out << "        \"" << st.fullName << "\",\n";
        out << "        static_cast<uint32_t>(" << enumName << "::" << topic->name << "),\n";
        out << "        [](const " << st.fullName << " & object, std::vector<uint8_t> & outPayload) -> bool {\n";
        out << "            outPayload = object.serialize();\n";
        out << "            return true;\n";
        out << "        },\n";
        out << "        [](const std::vector<uint8_t> & payload, " << st.fullName << " & object) -> bool {\n";
        out << "            return object.deserialize(payload);\n";
        out << "        }\n";
        out << "    );\n";
    }

    out << "}\n";
    out << "} // namespace LDdsFramework\n";
    return out.str();
}

size_t countLines(const std::string & text)
{
    return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1U;
}

bool writeTextFile(const fs::path & path, const std::string & text)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        return false;
    }

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

std::pair<fs::path, std::string> resolveOutputDirAndPrefix(
    const std::string & outputPath,
    const LIdlFile &    file)
{
    fs::path path(outputPath);
    fs::path outputDir;
    std::string prefix;

    if (!path.extension().empty())
    {
        outputDir = path.parent_path();
        prefix = path.stem().string();
    }
    else
    {
        outputDir = path;
    }

    if (prefix.empty())
    {
        fs::path src(file.sourcePath);
        prefix = src.stem().string();
    }

    const std::string lowerPrefix = toLower(prefix);
    if (lowerPrefix.size() > 5 && lowerPrefix.substr(lowerPrefix.size() - 5) == ".lidl")
    {
        prefix = prefix.substr(0, prefix.size() - 5);
    }

    prefix = sanitizeName(prefix);
    if (outputDir.empty())
    {
        outputDir = fs::current_path();
    }

    return {outputDir, prefix};
}

} // namespace

LIdlGenerator::LIdlGenerator()
    : m_target(TargetLanguage::Cpp)
    , m_options()
    , m_callback(nullptr)
{
}

LIdlGenerator::LIdlGenerator(TargetLanguage target)
    : m_target(target)
    , m_options()
    , m_callback(nullptr)
{
}

LIdlGenerator::~LIdlGenerator() noexcept = default;

LIdlGenerator::LIdlGenerator(LIdlGenerator && other) noexcept
    : m_target(other.m_target)
    , m_options(std::move(other.m_options))
    , m_callback(std::move(other.m_callback))
{
}

LIdlGenerator & LIdlGenerator::operator=(LIdlGenerator && other) noexcept
{
    if (this != &other)
    {
        m_target = other.m_target;
        m_options = std::move(other.m_options);
        m_callback = std::move(other.m_callback);
    }
    return *this;
}

void LIdlGenerator::setTargetLanguage(TargetLanguage target)
{
    m_target = target;
}

TargetLanguage LIdlGenerator::getTargetLanguage() const noexcept
{
    return m_target;
}

void LIdlGenerator::setOptions(const GeneratorOptions & options)
{
    m_options = options;
}

const GeneratorOptions & LIdlGenerator::getOptions() const noexcept
{
    return m_options;
}

void LIdlGenerator::setProgressCallback(const GenerationProgressCallback & callback)
{
    m_callback = callback;
}

GenerationResult LIdlGenerator::generate(const ParseResult & parseResult, const std::string & outputPath)
{
    if (!parseResult.astRoot)
    {
        GenerationResult result;
        result.messages.push_back("parseResult.astRoot is null");
        return result;
    }

    return generateFromAst(parseResult.astRoot, outputPath);
}

GenerationResult LIdlGenerator::generateFromAst(
    const std::shared_ptr<AstNode> & astRoot,
    const std::string &              outputPath)
{
    GenerationResult result;
    const auto start = std::chrono::steady_clock::now();

    if (m_target != TargetLanguage::Cpp)
    {
        result.messages.push_back("only C++ generation is implemented");
        return result;
    }

    const auto idlFile = std::dynamic_pointer_cast<LIdlFile>(astRoot);
    if (!idlFile)
    {
        result.messages.push_back("astRoot is not LIdlFile");
        return result;
    }

    const auto [outputDir, prefix] = resolveOutputDirAndPrefix(outputPath, *idlFile);

    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        result.messages.push_back("failed to create output directory: " + outputDir.string());
        return result;
    }

    const std::string exportText = generateExportHeader(prefix);
    const std::string defineText = generateDefineHeader(prefix, *idlFile);
    const std::string topicHText = generateTopicHeader(prefix, *idlFile);
    const std::string topicCppText = generateTopicCpp(prefix, *idlFile);

    const fs::path exportPath = outputDir / (prefix + "_export.h");
    const fs::path definePath = outputDir / (prefix + "_define.h");
    const fs::path topicHPath = outputDir / (prefix + "_topic.h");
    const fs::path topicCppPath = outputDir / (prefix + "_topic.cpp");

    if (!writeTextFile(exportPath, exportText) ||
        !writeTextFile(definePath, defineText) ||
        !writeTextFile(topicHPath, topicHText) ||
        !writeTextFile(topicCppPath, topicCppText))
    {
        result.messages.push_back("failed to write generated files");
        return result;
    }

    result.success = true;
    result.outputPath = outputDir.string();
    result.generatedCode = defineText;
    result.linesGenerated = countLines(exportText) + countLines(defineText) +
                            countLines(topicHText) + countLines(topicCppText);

    if (m_callback)
    {
        m_callback((outputDir / (prefix + "_define.h")).string(), 1, 4, "generated");
        m_callback((outputDir / (prefix + "_export.h")).string(), 2, 4, "generated");
        m_callback((outputDir / (prefix + "_topic.h")).string(), 3, 4, "generated");
        m_callback((outputDir / (prefix + "_topic.cpp")).string(), 4, 4, "generated");
    }

    const auto end = std::chrono::steady_clock::now();
    result.generationTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

std::vector<GenerationResult> LIdlGenerator::generateBatch(
    const std::vector<ParseResult> & parseResults,
    const std::string &              outputDirectory)
{
    std::vector<GenerationResult> results;
    results.reserve(parseResults.size());

    for (const auto & parseResult : parseResults)
    {
        results.push_back(generate(parseResult, outputDirectory));
    }

    return results;
}

std::vector<std::string> LIdlGenerator::getFileExtensions() const
{
    switch (m_target)
    {
    case TargetLanguage::Cpp:
        return {"_define.h", "_export.h", "_topic.h", "_topic.cpp"};
    case TargetLanguage::CSharp:
        return {".cs"};
    case TargetLanguage::Java:
        return {".java"};
    case TargetLanguage::Python:
        return {".py"};
    case TargetLanguage::Go:
        return {".go"};
    case TargetLanguage::Rust:
        return {".rs"};
    case TargetLanguage::TypeScript:
        return {".ts"};
    default:
        return {};
    }
}

bool LIdlGenerator::isLanguageSupported(TargetLanguage language) noexcept
{
    switch (language)
    {
    case TargetLanguage::Cpp:
    case TargetLanguage::CSharp:
    case TargetLanguage::Java:
    case TargetLanguage::Python:
    case TargetLanguage::Go:
    case TargetLanguage::Rust:
    case TargetLanguage::TypeScript:
        return true;
    default:
        return false;
    }
}

std::vector<TargetLanguage> LIdlGenerator::getSupportedLanguages()
{
    return {
        TargetLanguage::Cpp,
        TargetLanguage::CSharp,
        TargetLanguage::Java,
        TargetLanguage::Python,
        TargetLanguage::Go,
        TargetLanguage::Rust,
        TargetLanguage::TypeScript
    };
}

} // namespace LDdsFramework
