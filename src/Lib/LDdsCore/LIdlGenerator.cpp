#include "LIdlGenerator.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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
        Enum,
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

std::string makeMacroToken(const std::string & text)
{
    return toUpper(sanitizeName(text));
}

std::string makeRuntimeTopicKey(const std::string & prefix, const std::string & topicName)
{
    return sanitizeName(prefix) + "::" + sanitizeName(topicName);
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

std::vector<std::string> collectDependencyPrefixes(const LIdlFile & file, const std::string & prefix);
bool isLocalDefinition(const LIdlFile & file, const std::string & sourceFile);

TypeInfo parseType(
    const std::string & rawType,
    const std::unordered_set<std::string> * enumTypeNames = nullptr)
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

    const std::string sequencePrefix = "sequence<";
    if (lowerType.rfind(sequencePrefix, 0) == 0 && !type.empty() && type.back() == '>')
    {
        const std::string inner = trim(type.substr(sequencePrefix.size(), type.size() - sequencePrefix.size() - 1));
        std::string elemRaw = inner;
        const auto commaPos = inner.find(',');
        if (commaPos != std::string::npos)
        {
            elemRaw = trim(inner.substr(0, commaPos));
        }

        TypeInfo elemInfo = parseType(elemRaw, enumTypeNames);
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

    if (enumTypeNames != nullptr)
    {
        if (enumTypeNames->find(type) != enumTypeNames->end())
        {
            info.kind = TypeInfo::Kind::Enum;
            info.cppType = type;
            return info;
        }
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
    const std::vector<std::string> dependencies = collectDependencyPrefixes(file, prefix);

    std::unordered_set<std::string> enumTypeNames;
    for (const auto & en : file.enums)
    {
        enumTypeNames.insert(en.fullName);
        enumTypeNames.insert(en.name);
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
    out << "#include \"LTypeRegistry.h\"\n\n";
    for (const auto & dependency : dependencies)
    {
        out << "#include \"" << dependency << "_define.h\"\n";
    }
    if (!dependencies.empty())
    {
        out << "\n";
    }
    out << "#ifndef LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H\n";
    out << "#define LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H\n\n";
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
    out << "template<typename T>\n";
    out << "inline typename std::enable_if<std::is_enum<T>::value, void>::type\n";
    out << "writeEnum(LByteBuffer & buffer, T value)\n";
    out << "{\n";
    out << "    const int32_t raw = static_cast<int32_t>(value);\n";
    out << "    writePod(buffer, raw);\n";
    out << "}\n\n";
    out << "template<typename T>\n";
    out << "inline typename std::enable_if<std::is_enum<T>::value, bool>::type\n";
    out << "readEnum(const std::vector<uint8_t> & data, size_t & offset, T & value)\n";
    out << "{\n";
    out << "    int32_t raw = 0;\n";
    out << "    if (!readPod(data, offset, raw)) { return false; }\n";
    out << "    value = static_cast<T>(raw);\n";
    out << "    return true;\n";
    out << "}\n";
    out << "} // namespace idl_detail\n";
    out << "} // namespace LDdsFramework\n\n";
    out << "#endif // LDDSFRAMEWORK_IDL_DETAIL_HELPERS_H\n\n";

    for (const auto & en : file.enums)
    {
        if (!isLocalDefinition(file, en.sourceFile))
        {
            continue;
        }
        const auto nsParts = splitNs(en.packagePath);
        openNamespaces(out, nsParts);
        if (!en.comment.empty())
        {
            out << "// " << en.comment << "\n";
        }
        out << "enum class " << en.name << " : int32_t\n";
        out << "{\n";
        if (en.values.empty())
        {
            out << "    Invalid = 0\n";
        }
        else
        {
            for (size_t i = 0; i < en.values.size(); ++i)
            {
                const auto & item = en.values[i];
                out << "    " << item.name << " = " << item.value;
                if (i + 1 < en.values.size())
                {
                    out << ",";
                }
                if (!item.comment.empty())
                {
                    out << " // " << item.comment;
                }
                out << "\n";
            }
        }
        out << "};\n";
        closeNamespaces(out, nsParts);
        out << "\n";
    }

    std::vector<LIdlStruct> localStructs;
    for (const auto & st : file.structs)
    {
        if (isLocalDefinition(file, st.sourceFile))
        {
            localStructs.push_back(st);
        }
    }

    const auto order = topologicalStructOrder(localStructs);
    for (size_t ordIdx = 0; ordIdx < order.size(); ++ordIdx)
    {
        const auto & st = localStructs[order[ordIdx]];
        const auto nsParts = splitNs(st.packagePath);
        openNamespaces(out, nsParts);

        if (!st.comment.empty())
        {
            out << "// " << st.comment << "\n";
        }

        out << "struct " << st.name;
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
            TypeInfo info = parseType(field.typeName, &enumTypeNames);
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
            TypeInfo info = parseType(field.typeName, &enumTypeNames);
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
            const TypeInfo info = parseType(field.typeName, &enumTypeNames);
            if (info.kind == TypeInfo::Kind::Primitive)
            {
                out << "        LDdsFramework::idl_detail::writePod(buffer, " << field.name << ");\n";
            }
            else if (info.kind == TypeInfo::Kind::Enum)
            {
                out << "        LDdsFramework::idl_detail::writeEnum(buffer, " << field.name << ");\n";
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
                else if (info.elementKind == TypeInfo::Kind::Enum)
                {
                    out << "            LDdsFramework::idl_detail::writeEnum(buffer, item);\n";
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
            const TypeInfo info = parseType(field.typeName, &enumTypeNames);
            if (info.kind == TypeInfo::Kind::Primitive)
            {
                out << "        if (!LDdsFramework::idl_detail::readPod(data, offset, " << field.name << ")) { return false; }\n";
            }
            else if (info.kind == TypeInfo::Kind::Enum)
            {
                out << "        if (!LDdsFramework::idl_detail::readEnum(data, offset, " << field.name << ")) { return false; }\n";
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
                else if (info.elementKind == TypeInfo::Kind::Enum)
                {
                    out << "            if (!LDdsFramework::idl_detail::readEnum(data, offset, item)) { return false; }\n";
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

        const auto topicIt = std::find_if(
            file.topics.begin(),
            file.topics.end(),
            [&st](const LIdlTopic & topic) {
                return topic.typeName == st.fullName;
            });
        const std::string topicKey =
            (topicIt == file.topics.end()) ? std::string() : makeRuntimeTopicKey(prefix, topicIt->name);
        out << "    " << st.name << " * get() noexcept { return this; }\n";
        out << "    const " << st.name << " * get() const noexcept { return this; }\n";
        out << "    static uint32_t getTypeId() noexcept { return LDdsFramework::LTypeRegistry::makeTopicId(getTopicKey()); }\n";
        out << "    static const char * getTypeName() noexcept { return \"" << st.fullName << "\"; }\n";
        out << "    static const char * getTopicKey() noexcept { return \"" << topicKey << "\"; }\n";

        out << "};\n";

        closeNamespaces(out, nsParts);
        out << "\n";
    }

    for (const auto & un : file.unions)
    {
        if (!isLocalDefinition(file, un.sourceFile))
        {
            continue;
        }
        const auto nsParts = splitNs(un.packagePath);
        openNamespaces(out, nsParts);
        if (!un.comment.empty())
        {
            out << "// " << un.comment << "\n";
        }

        const TypeInfo discriminatorInfo = parseType(un.discriminatorType, &enumTypeNames);

        out << "struct " << un.name << "\n{\n";
        out << "    " << discriminatorInfo.cppType << " discriminator{};\n";

        for (const auto & uc : un.cases)
        {
            const TypeInfo fieldInfo = parseType(uc.field.typeName, &enumTypeNames);
            out << "    " << fieldInfo.cppType << " " << uc.field.name << "{};\n";
        }
        out << "\n";

        out << "    void serialize(LDdsFramework::LByteBuffer & buffer) const\n";
        out << "    {\n";
        if (discriminatorInfo.kind == TypeInfo::Kind::Enum)
        {
            out << "        LDdsFramework::idl_detail::writeEnum(buffer, discriminator);\n";
        }
        else
        {
            out << "        LDdsFramework::idl_detail::writePod(buffer, discriminator);\n";
        }

        out << "        switch (static_cast<int64_t>(discriminator))\n";
        out << "        {\n";
        for (const auto & uc : un.cases)
        {
            if (uc.isDefault)
            {
                out << "        default:\n";
            }
            else
            {
                for (const int64_t label : uc.labels)
                {
                    out << "        case " << label << ":\n";
                }
            }

            const TypeInfo fieldInfo = parseType(uc.field.typeName, &enumTypeNames);
            if (fieldInfo.kind == TypeInfo::Kind::Primitive)
            {
                out << "            LDdsFramework::idl_detail::writePod(buffer, " << uc.field.name << ");\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::Enum)
            {
                out << "            LDdsFramework::idl_detail::writeEnum(buffer, " << uc.field.name << ");\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::String)
            {
                out << "            LDdsFramework::idl_detail::writeString(buffer, " << uc.field.name << ");\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::Vector)
            {
                out << "            buffer.writeUInt32(static_cast<uint32_t>(" << uc.field.name << ".size()));\n";
                out << "            for (const auto & item : " << uc.field.name << ")\n";
                out << "            {\n";
                if (fieldInfo.elementKind == TypeInfo::Kind::Primitive)
                {
                    out << "                LDdsFramework::idl_detail::writePod(buffer, item);\n";
                }
                else if (fieldInfo.elementKind == TypeInfo::Kind::Enum)
                {
                    out << "                LDdsFramework::idl_detail::writeEnum(buffer, item);\n";
                }
                else if (fieldInfo.elementKind == TypeInfo::Kind::String)
                {
                    out << "                LDdsFramework::idl_detail::writeString(buffer, item);\n";
                }
                else
                {
                    out << "                item.serialize(buffer);\n";
                }
                out << "            }\n";
            }
            else
            {
                out << "            " << uc.field.name << ".serialize(buffer);\n";
            }
            out << "            break;\n";
        }
        out << "        }\n";
        out << "    }\n\n";

        out << "    bool deserialize(const std::vector<uint8_t> & data, size_t & offset)\n";
        out << "    {\n";
        if (discriminatorInfo.kind == TypeInfo::Kind::Enum)
        {
            out << "        if (!LDdsFramework::idl_detail::readEnum(data, offset, discriminator)) { return false; }\n";
        }
        else
        {
            out << "        if (!LDdsFramework::idl_detail::readPod(data, offset, discriminator)) { return false; }\n";
        }

        out << "        switch (static_cast<int64_t>(discriminator))\n";
        out << "        {\n";
        for (const auto & uc : un.cases)
        {
            if (uc.isDefault)
            {
                out << "        default:\n";
            }
            else
            {
                for (const int64_t label : uc.labels)
                {
                    out << "        case " << label << ":\n";
                }
            }

            const TypeInfo fieldInfo = parseType(uc.field.typeName, &enumTypeNames);
            if (fieldInfo.kind == TypeInfo::Kind::Primitive)
            {
                out << "            if (!LDdsFramework::idl_detail::readPod(data, offset, " << uc.field.name << ")) { return false; }\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::Enum)
            {
                out << "            if (!LDdsFramework::idl_detail::readEnum(data, offset, " << uc.field.name << ")) { return false; }\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::String)
            {
                out << "            if (!LDdsFramework::idl_detail::readString(data, offset, " << uc.field.name << ")) { return false; }\n";
            }
            else if (fieldInfo.kind == TypeInfo::Kind::Vector)
            {
                out << "            uint32_t " << uc.field.name << "Size = 0;\n";
                out << "            if (!LDdsFramework::idl_detail::readPod(data, offset, " << uc.field.name << "Size)) { return false; }\n";
                out << "            " << uc.field.name << ".clear();\n";
                out << "            " << uc.field.name << ".reserve(" << uc.field.name << "Size);\n";
                out << "            for (uint32_t i = 0; i < " << uc.field.name << "Size; ++i)\n";
                out << "            {\n";
                out << "                " << fieldInfo.elementCppType << " item{};\n";
                if (fieldInfo.elementKind == TypeInfo::Kind::Primitive)
                {
                    out << "                if (!LDdsFramework::idl_detail::readPod(data, offset, item)) { return false; }\n";
                }
                else if (fieldInfo.elementKind == TypeInfo::Kind::Enum)
                {
                    out << "                if (!LDdsFramework::idl_detail::readEnum(data, offset, item)) { return false; }\n";
                }
                else if (fieldInfo.elementKind == TypeInfo::Kind::String)
                {
                    out << "                if (!LDdsFramework::idl_detail::readString(data, offset, item)) { return false; }\n";
                }
                else
                {
                    out << "                if (!item.deserialize(data, offset)) { return false; }\n";
                }
                out << "                " << uc.field.name << ".push_back(std::move(item));\n";
                out << "            }\n";
            }
            else
            {
                out << "            if (!" << uc.field.name << ".deserialize(data, offset)) { return false; }\n";
            }
            out << "            break;\n";
        }
        out << "        }\n";
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

        const auto topicIt = std::find_if(
            file.topics.begin(),
            file.topics.end(),
            [&un](const LIdlTopic & topic) {
                return topic.typeName == un.fullName;
            });
        const std::string topicKey =
            (topicIt == file.topics.end()) ? std::string() : makeRuntimeTopicKey(prefix, topicIt->name);
        out << "    " << un.name << " * get() noexcept { return this; }\n";
        out << "    const " << un.name << " * get() const noexcept { return this; }\n";
        out << "    static uint32_t getTypeId() noexcept { return LDdsFramework::LTypeRegistry::makeTopicId(getTopicKey()); }\n";
        out << "    static const char * getTypeName() noexcept { return \"" << un.fullName << "\"; }\n";
        out << "    static const char * getTopicKey() noexcept { return \"" << topicKey << "\"; }\n";
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
    const std::string macroPrefix = makeMacroToken(prefix);
    const std::string registerFn = "register" + toPascalCase(prefix) + "Types";
    const std::string moduleRegisterFn = "register" + toPascalCase(prefix) + "IdlModule";
    const std::string resolveIdFn = "tryResolve" + toPascalCase(prefix) + "TopicId";
    const std::string resolveNameFn = "tryResolve" + toPascalCase(prefix) + "TopicKey";
    std::vector<LIdlTopic> localTopics;
    for (const auto & topic : file.topics)
    {
        if (isLocalDefinition(file, topic.sourceFile))
        {
            localTopics.push_back(topic);
        }
    }

    std::ostringstream out;
    out << "#ifndef " << guard << "\n";
    out << "#define " << guard << "\n\n";
    out << "#include <cstdint>\n";
    out << "#include <string>\n\n";
    out << "#include \"" << prefix << "_export.h\"\n";
    out << "#include \"" << prefix << "_define.h\"\n";
    out << "#include \"LTypeRegistry.h\"\n\n";

    out << "namespace LDdsFramework {\n";

    for (const auto & topic : localTopics)
    {
        const std::string token = makeMacroToken(topic.name);
        const std::string topicKey = makeRuntimeTopicKey(prefix, topic.name);
        out << "#define " << macroPrefix << "_TOPIC_NAME_" << token
            << " \"" << topic.name << "\"\n";
        out << "#define " << macroPrefix << "_TOPIC_KEY_" << token
            << " \"" << topicKey << "\"\n";
        out << "#define " << macroPrefix << "_TOPIC_ID_" << token
            << " LDdsFramework::LTypeRegistry::makeTopicId(" << macroPrefix << "_TOPIC_KEY_" << token << ")\n";
    }
    if (!localTopics.empty())
    {
        out << "\n";
    }

    out << exportMacro << " bool " << registerFn << "(LTypeRegistry & registry);\n";
    out << "extern \"C\" " << exportMacro << " bool " << moduleRegisterFn << "(LTypeRegistry & registry);\n";
    out << "inline bool " << resolveIdFn
        << "(const std::string & topicKey, uint32_t & topicId)\n";
    out << "{\n";
    for (const auto & topic : localTopics)
    {
        const std::string topicKey = makeRuntimeTopicKey(prefix, topic.name);
        out << "    if (topicKey == \"" << topicKey << "\" || topicKey == \"" << topic.name << "\")\n";
        out << "    {\n";
        out << "        topicId = LTypeRegistry::makeTopicId(\"" << topicKey << "\");\n";
        out << "        return true;\n";
        out << "    }\n";
    }
    out << "    topicId = 0;\n";
    out << "    return false;\n";
    out << "}\n\n";

    out << "inline bool " << resolveNameFn
        << "(uint32_t topicId, const char * & topicKey)\n";
    out << "{\n";
    for (const auto & topic : localTopics)
    {
        const std::string topicKey = makeRuntimeTopicKey(prefix, topic.name);
        out << "    if (topicId == LTypeRegistry::makeTopicId(\"" << topicKey << "\"))\n";
        out << "    {\n";
        out << "        topicKey = \"" << topicKey << "\";\n";
        out << "        return true;\n";
        out << "    }\n";
    }
    out << "    topicKey = nullptr;\n";
    out << "    return false;\n";
    out << "}\n";
    out << "} // namespace LDdsFramework\n\n";
    out << "#endif // " << guard << "\n";
    return out.str();
}

std::string generateTopicCpp(const std::string & prefix, const LIdlFile & file)
{
    const std::string registerFn = "register" + toPascalCase(prefix) + "Types";
    const std::string moduleRegisterFn = "register" + toPascalCase(prefix) + "IdlModule";
    const std::string moduleName = sanitizeName(prefix);

    std::unordered_map<std::string, const LIdlTopic *> topicByType;
    for (const auto & topic : file.topics)
    {
        if (isLocalDefinition(file, topic.sourceFile))
        {
            topicByType[topic.typeName] = &topic;
        }
    }

    std::ostringstream out;
    out << "#include \"" << prefix << "_topic.h\"\n\n";
    out << "namespace LDdsFramework {\n";
    out << "bool " << registerFn << "(LTypeRegistry & registry)\n";
    out << "{\n";
    out << "    bool ok = true;\n";

    for (const auto & st : file.structs)
    {
        const auto it = topicByType.find(st.fullName);
        if (it == topicByType.end())
        {
            continue;
        }

        const auto * topic = it->second;
        out << "    ok = registry.registerTopicType<" << st.fullName << ">(\n";
        out << "        \"" << makeRuntimeTopicKey(prefix, topic->name) << "\",\n";
        out << "        \"" << st.fullName << "\",\n";
        out << "        [](const " << st.fullName << " & object, std::vector<uint8_t> & outPayload) -> bool {\n";
        out << "            outPayload = object.serialize();\n";
        out << "            return true;\n";
        out << "        },\n";
        out << "        [](const std::vector<uint8_t> & payload, " << st.fullName << " & object) -> bool {\n";
        out << "            return object.deserialize(payload);\n";
        out << "        }\n";
        out << "    ) && ok;\n";
    }

    for (const auto & un : file.unions)
    {
        const auto it = topicByType.find(un.fullName);
        if (it == topicByType.end())
        {
            continue;
        }

        const auto * topic = it->second;
        out << "    ok = registry.registerTopicType<" << un.fullName << ">(\n";
        out << "        \"" << makeRuntimeTopicKey(prefix, topic->name) << "\",\n";
        out << "        \"" << un.fullName << "\",\n";
        out << "        [](const " << un.fullName << " & object, std::vector<uint8_t> & outPayload) -> bool {\n";
        out << "            outPayload = object.serialize();\n";
        out << "            return true;\n";
        out << "        },\n";
        out << "        [](const std::vector<uint8_t> & payload, " << un.fullName << " & object) -> bool {\n";
        out << "            return object.deserialize(payload);\n";
        out << "        }\n";
        out << "    ) && ok;\n";
    }

    out << "    return ok;\n";
    out << "}\n\n";
    out << "extern \"C\" " << toUpper(prefix) << "_IDL_API bool " << moduleRegisterFn << "(LTypeRegistry & registry)\n";
    out << "{\n";
    out << "    return " << registerFn << "(registry);\n";
    out << "}\n";
    out << "} // namespace LDdsFramework\n";
    out << "\nnamespace {\n";
    out << "struct " << toPascalCase(prefix) << "AutoModuleRegistrar\n";
    out << "{\n";
    out << "    " << toPascalCase(prefix) << "AutoModuleRegistrar()\n";
    out << "    {\n";
    out << "        LDdsFramework::registerGeneratedModule(\n";
    out << "            \"" << moduleName << "\",\n";
    out << "            &LDdsFramework::" << moduleRegisterFn << ");\n";
    out << "    }\n";
    out << "};\n";
    out << "static " << toPascalCase(prefix) << "AutoModuleRegistrar g_" << sanitizeName(prefix) << "AutoModuleRegistrar;\n";
    out << "} // namespace\n";
    return out.str();
}

std::vector<std::string> collectDependencyPrefixes(const LIdlFile & file, const std::string & prefix)
{
    std::vector<std::string> dependencies;
    std::unordered_set<std::string> seen;
    const std::string self = sanitizeName(prefix);

    for (const auto & includeFile : file.includeFiles)
    {
        const std::string dependency = sanitizeName(fs::path(includeFile).stem().string());
        if (dependency.empty() || dependency == self || !seen.insert(dependency).second)
        {
            continue;
        }
        dependencies.push_back(dependency);
    }

    return dependencies;
}

bool isLocalDefinition(const LIdlFile & file, const std::string & sourceFile)
{
    if (file.sourcePath.empty() || sourceFile.empty())
    {
        return true;
    }

    std::error_code ec;
    const fs::path filePath = fs::weakly_canonical(file.sourcePath, ec);
    const fs::path sourcePath = fs::weakly_canonical(sourceFile, ec);
    if (!ec)
    {
        return filePath == sourcePath;
    }

    return fs::path(file.sourcePath).lexically_normal() == fs::path(sourceFile).lexically_normal();
}

uint64_t fnv1a64(const std::string & text)
{
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : text)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string makeGuid(const std::string & seed)
{
    const uint64_t hi = fnv1a64(seed);
    const uint64_t lo = fnv1a64(seed + ":guid");
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(8) << static_cast<uint32_t>((hi >> 32) & 0xFFFFFFFFULL)
        << "-"
        << std::setw(4) << static_cast<uint16_t>((hi >> 16) & 0xFFFFULL)
        << "-"
        << std::setw(4) << static_cast<uint16_t>(hi & 0xFFFFULL)
        << "-"
        << std::setw(4) << static_cast<uint16_t>((lo >> 48) & 0xFFFFULL)
        << "-"
        << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);
    return out.str();
}

std::string joinVcxprojList(const std::vector<std::string> & values, const std::string & suffix = std::string())
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            out << ";";
        }
        out << values[i] << suffix;
    }
    return out.str();
}

std::string toCMakePath(const fs::path & path)
{
    return path.generic_string();
}

std::string toVcxprojPath(const fs::path & path)
{
    fs::path preferred = path;
    preferred.make_preferred();
    return preferred.string();
}

std::string generateModuleCMakeLists(
    const std::string & prefix,
    const LIdlFile &    file,
    const fs::path &    lddsRoot,
    const fs::path &    installRoot)
{
    const std::vector<std::string> dependencies = collectDependencyPrefixes(file, prefix);
    const std::string targetName = sanitizeName(prefix);
    const std::string exportMacro = toUpper(prefix) + "_IDL_EXPORTS";

    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.16)\n";
    out << "project(" << targetName << " LANGUAGES CXX)\n\n";
    out << "set(CMAKE_CXX_STANDARD 17)\n";
    out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    out << "set(CMAKE_DEBUG_POSTFIX d)\n\n";
    out << "if(NOT DEFINED LDDS_ROOT)\n";
    out << "    if(DEFINED ENV{LDDS_ROOT})\n";
    out << "        set(LDDS_ROOT \"$ENV{LDDS_ROOT}\")\n";
    out << "    else()\n";
    out << "        set(LDDS_ROOT \"" << toCMakePath(lddsRoot) << "\")\n";
    out << "    endif()\n";
    out << "endif()\n";
    out << "if(NOT DEFINED LDDS_INSTALL_ROOT)\n";
    out << "    set(LDDS_INSTALL_ROOT \"" << toCMakePath(installRoot) << "\")\n";
    out << "endif()\n";
    out << "if(NOT LDDS_ROOT)\n";
    out << "    message(FATAL_ERROR \"Set LDDS_ROOT to the LDds root directory before building this module\")\n";
    out << "endif()\n\n";
    out << "add_library(" << targetName << " SHARED\n";
    out << "    " << prefix << "_topic.cpp\n";
    out << ")\n\n";
    out << "target_compile_definitions(" << targetName << " PRIVATE " << exportMacro << ")\n";
    out << "target_include_directories(" << targetName << " PUBLIC\n";
    out << "    \"${CMAKE_CURRENT_SOURCE_DIR}\"\n";
    out << "    \"${LDDS_ROOT}/src/Lib/LDdsCore\"\n";
    for (const auto & dependency : dependencies)
    {
        out << "    \"${LDDS_INSTALL_ROOT}/include/" << dependency << "\"\n";
    }
    out << ")\n";
    out << "target_link_directories(" << targetName << " PRIVATE\n";
    out << "    \"${LDDS_ROOT}/bin/lib\"\n";
    out << "    \"${LDDS_INSTALL_ROOT}/lib\"\n";
    out << ")\n";
    out << "target_link_libraries(" << targetName << " PRIVATE LDdsCore$<$<CONFIG:Debug>:d>";
    for (const auto & dependency : dependencies)
    {
        out << " " << dependency << "$<$<CONFIG:Debug>:d>";
    }
    out << ")\n\n";
    out << "set_target_properties(" << targetName << " PROPERTIES\n";
    out << "    ARCHIVE_OUTPUT_DIRECTORY \"${LDDS_INSTALL_ROOT}/lib\"\n";
    out << "    LIBRARY_OUTPUT_DIRECTORY \"${LDDS_INSTALL_ROOT}\"\n";
    out << "    RUNTIME_OUTPUT_DIRECTORY \"${LDDS_INSTALL_ROOT}\"\n";
    out << ")\n";
    out << "foreach(cfg Debug Release RelWithDebInfo MinSizeRel)\n";
    out << "    string(TOUPPER ${cfg} CFG)\n";
    out << "    set_target_properties(" << targetName << " PROPERTIES\n";
    out << "        ARCHIVE_OUTPUT_DIRECTORY_${CFG} \"${LDDS_INSTALL_ROOT}/lib\"\n";
    out << "        LIBRARY_OUTPUT_DIRECTORY_${CFG} \"${LDDS_INSTALL_ROOT}\"\n";
    out << "        RUNTIME_OUTPUT_DIRECTORY_${CFG} \"${LDDS_INSTALL_ROOT}\"\n";
    out << "    )\n";
    out << "endforeach()\n";
    return out.str();
}

std::string generateModuleVcxproj(
    const std::string & prefix,
    const LIdlFile &    file,
    const fs::path &    lddsRoot,
    const fs::path &    installRoot)
{
    const std::vector<std::string> dependencies = collectDependencyPrefixes(file, prefix);
    const std::string projectGuid = makeGuid(prefix);
    const std::string projectName = sanitizeName(prefix);
    const std::string exportMacro = toUpper(prefix) + "_IDL_EXPORTS";

    std::vector<std::string> includeDirs = {
        "$(ProjectDir)",
        "$(LDDS_ROOT)\\src\\Lib\\LDdsCore"
    };
    std::vector<std::string> libraryDirs = {
        "$(LDDS_ROOT)\\bin\\lib",
        "$(LDDS_INSTALL_ROOT)\\lib"
    };
    std::vector<std::string> debugDependencies = {
        "LDdsCored.lib"
    };
    std::vector<std::string> releaseDependencies = {
        "LDdsCore.lib"
    };

    for (const auto & dependency : dependencies)
    {
        includeDirs.push_back("$(LDDS_INSTALL_ROOT)\\include\\" + dependency);
        debugDependencies.push_back(dependency + "d.lib");
        releaseDependencies.push_back(dependency + ".lib");
    }

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    out << "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
    out << "  <ItemGroup Label=\"ProjectConfigurations\">\n";
    out << "    <ProjectConfiguration Include=\"Debug|x64\"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>\n";
    out << "    <ProjectConfiguration Include=\"Release|x64\"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>\n";
    out << "  </ItemGroup>\n";
    out << "  <PropertyGroup Label=\"Globals\">\n";
    out << "    <VCProjectVersion>15.0</VCProjectVersion>\n";
    out << "    <ProjectGuid>{" << projectGuid << "}</ProjectGuid>\n";
    out << "    <Keyword>Win32Proj</Keyword>\n";
    out << "    <RootNamespace>" << projectName << "</RootNamespace>\n";
    out << "    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\n";
    out << "  </PropertyGroup>\n";
    out << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n";
    out << "  <PropertyGroup>\n";
    out << "    <LDDS_ROOT>" << toVcxprojPath(lddsRoot) << "</LDDS_ROOT>\n";
    out << "    <LDDS_INSTALL_ROOT>" << toVcxprojPath(installRoot) << "</LDDS_INSTALL_ROOT>\n";
    out << "  </PropertyGroup>\n";
    out << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\" Label=\"Configuration\">\n";
    out << "    <ConfigurationType>DynamicLibrary</ConfigurationType>\n";
    out << "    <UseDebugLibraries>true</UseDebugLibraries>\n";
    out << "    <PlatformToolset>v141</PlatformToolset>\n";
    out << "  </PropertyGroup>\n";
    out << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\" Label=\"Configuration\">\n";
    out << "    <ConfigurationType>DynamicLibrary</ConfigurationType>\n";
    out << "    <UseDebugLibraries>false</UseDebugLibraries>\n";
    out << "    <PlatformToolset>v141</PlatformToolset>\n";
    out << "  </PropertyGroup>\n";
    out << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n";
    out << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n";
    out << "    <OutDir>$(LDDS_INSTALL_ROOT)\\</OutDir>\n";
    out << "    <IntDir>$(ProjectDir)build\\Debug\\</IntDir>\n";
    out << "    <TargetName>" << projectName << "d</TargetName>\n";
    out << "  </PropertyGroup>\n";
    out << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">\n";
    out << "    <OutDir>$(LDDS_INSTALL_ROOT)\\</OutDir>\n";
    out << "    <IntDir>$(ProjectDir)build\\Release\\</IntDir>\n";
    out << "    <TargetName>" << projectName << "</TargetName>\n";
    out << "  </PropertyGroup>\n";
    out << "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n";
    out << "    <ClCompile>\n";
    out << "      <LanguageStandard>stdcpp17</LanguageStandard>\n";
    out << "      <WarningLevel>Level3</WarningLevel>\n";
    out << "      <AdditionalIncludeDirectories>" << joinVcxprojList(includeDirs) << ";%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\n";
    out << "      <PreprocessorDefinitions>_DEBUG;_WINDOWS;_USRDLL;" << exportMacro << ";%(PreprocessorDefinitions)</PreprocessorDefinitions>\n";
    out << "    </ClCompile>\n";
    out << "    <Link>\n";
    out << "      <AdditionalLibraryDirectories>" << joinVcxprojList(libraryDirs) << ";%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>\n";
    out << "      <AdditionalDependencies>" << joinVcxprojList(debugDependencies) << ";%(AdditionalDependencies)</AdditionalDependencies>\n";
    out << "      <ImportLibrary>$(LDDS_INSTALL_ROOT)\\lib\\$(TargetName).lib</ImportLibrary>\n";
    out << "    </Link>\n";
    out << "  </ItemDefinitionGroup>\n";
    out << "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">\n";
    out << "    <ClCompile>\n";
    out << "      <LanguageStandard>stdcpp17</LanguageStandard>\n";
    out << "      <WarningLevel>Level3</WarningLevel>\n";
    out << "      <AdditionalIncludeDirectories>" << joinVcxprojList(includeDirs) << ";%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\n";
    out << "      <PreprocessorDefinitions>NDEBUG;_WINDOWS;_USRDLL;" << exportMacro << ";%(PreprocessorDefinitions)</PreprocessorDefinitions>\n";
    out << "    </ClCompile>\n";
    out << "    <Link>\n";
    out << "      <AdditionalLibraryDirectories>" << joinVcxprojList(libraryDirs) << ";%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>\n";
    out << "      <AdditionalDependencies>" << joinVcxprojList(releaseDependencies) << ";%(AdditionalDependencies)</AdditionalDependencies>\n";
    out << "      <ImportLibrary>$(LDDS_INSTALL_ROOT)\\lib\\$(TargetName).lib</ImportLibrary>\n";
    out << "    </Link>\n";
    out << "  </ItemDefinitionGroup>\n";
    out << "  <ItemGroup>\n";
    out << "    <ClCompile Include=\"" << prefix << "_topic.cpp\" />\n";
    out << "  </ItemGroup>\n";
    out << "  <ItemGroup>\n";
    out << "    <ClInclude Include=\"" << prefix << "_define.h\" />\n";
    out << "    <ClInclude Include=\"" << prefix << "_export.h\" />\n";
    out << "    <ClInclude Include=\"" << prefix << "_topic.h\" />\n";
    out << "  </ItemGroup>\n";
    out << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n";
    out << "</Project>\n";
    return out.str();
}

std::string generateModuleSln(const std::string & prefix)
{
    const std::string projectGuid = makeGuid(prefix);
    const std::string solutionGuid = makeGuid(prefix + ".sln");
    const std::string projectName = sanitizeName(prefix);

    std::ostringstream out;
    out << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
    out << "# Visual Studio 15\n";
    out << "VisualStudioVersion = 15.0.28307.1682\n";
    out << "MinimumVisualStudioVersion = 10.0.40219.1\n";
    out << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << projectName
        << "\", \"" << projectName << ".vcxproj\", \"{" << projectGuid << "}\"\n";
    out << "EndProject\n";
    out << "Global\n";
    out << "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
    out << "\t\tDebug|x64 = Debug|x64\n";
    out << "\t\tRelease|x64 = Release|x64\n";
    out << "\tEndGlobalSection\n";
    out << "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
    out << "\t\t{" << projectGuid << "}.Debug|x64.ActiveCfg = Debug|x64\n";
    out << "\t\t{" << projectGuid << "}.Debug|x64.Build.0 = Debug|x64\n";
    out << "\t\t{" << projectGuid << "}.Release|x64.ActiveCfg = Release|x64\n";
    out << "\t\t{" << projectGuid << "}.Release|x64.Build.0 = Release|x64\n";
    out << "\tEndGlobalSection\n";
    out << "\tGlobalSection(SolutionProperties) = preSolution\n";
    out << "\t\tHideSolutionNode = FALSE\n";
    out << "\tEndGlobalSection\n";
    out << "\tGlobalSection(ExtensibilityGlobals) = postSolution\n";
    out << "\t\tSolutionGuid = {" << solutionGuid << "}\n";
    out << "\tEndGlobalSection\n";
    out << "EndGlobal\n";
    return out.str();
}

std::string pythonTypeFromRaw(
    const std::string & rawType,
    const std::unordered_set<std::string> & enumTypeNames)
{
    const TypeInfo info = parseType(rawType, &enumTypeNames);
    if (info.kind == TypeInfo::Kind::String)
    {
        return "str";
    }
    if (info.kind == TypeInfo::Kind::Enum)
    {
        return info.cppType;
    }
    if (info.kind == TypeInfo::Kind::Vector)
    {
        const std::string elemType = pythonTypeFromRaw(info.elementType, enumTypeNames);
        return "List[" + elemType + "]";
    }
    if (info.kind == TypeInfo::Kind::Primitive)
    {
        const std::string lower = toLower(trim(rawType));
        if (lower == "bool")
        {
            return "bool";
        }
        if (lower == "float" || lower == "double")
        {
            return "float";
        }
        return "int";
    }
    return info.cppType;
}

std::string pythonDefaultForType(
    const std::string & rawType,
    const std::unordered_set<std::string> & enumTypeNames)
{
    const TypeInfo info = parseType(rawType, &enumTypeNames);
    if (info.kind == TypeInfo::Kind::String)
    {
        return "\"\"";
    }
    if (info.kind == TypeInfo::Kind::Vector)
    {
        return "dataclass_field(default_factory=list)";
    }
    if (info.kind == TypeInfo::Kind::Primitive)
    {
        const std::string lower = toLower(trim(rawType));
        if (lower == "bool")
        {
            return "False";
        }
        if (lower == "float" || lower == "double")
        {
            return "0.0";
        }
        return "0";
    }
    if (info.kind == TypeInfo::Kind::Enum)
    {
        return "0";
    }
    return "dataclass_field(default_factory=" + info.cppType + ")";
}

std::string generatePythonModule(const std::string & prefix, const LIdlFile & file)
{
    std::unordered_set<std::string> enumTypeNames;
    for (const auto & en : file.enums)
    {
        enumTypeNames.insert(en.fullName);
        enumTypeNames.insert(en.name);
    }

    std::ostringstream out;
    out << "from __future__ import annotations\n";
    out << "from dataclasses import dataclass, field as dataclass_field\n";
    out << "from enum import IntEnum\n";
    out << "from typing import Any, Dict, List, get_args, get_origin\n";
    out << "import inspect\n";
    out << "import json\n\n";

    out << "def _encode_value(value: Any) -> Any:\n";
    out << "    if isinstance(value, IntEnum):\n";
    out << "        return int(value)\n";
    out << "    if hasattr(value, \"to_dict\"):\n";
    out << "        return value.to_dict()\n";
    out << "    if isinstance(value, list):\n";
    out << "        return [_encode_value(v) for v in value]\n";
    out << "    return value\n\n";

    out << "def _decode_value(value: Any, typ: Any) -> Any:\n";
    out << "    origin = get_origin(typ)\n";
    out << "    if origin in (list, List):\n";
    out << "        elem = get_args(typ)[0] if get_args(typ) else Any\n";
    out << "        if value is None:\n";
    out << "            return []\n";
    out << "        return [_decode_value(v, elem) for v in value]\n";
    out << "    if inspect.isclass(typ) and issubclass(typ, IntEnum):\n";
    out << "        return typ(value)\n";
    out << "    if inspect.isclass(typ) and hasattr(typ, \"from_dict\"):\n";
    out << "        if isinstance(value, dict):\n";
    out << "            return typ.from_dict(value)\n";
    out << "        return typ()\n";
    out << "    return value\n\n";

    for (const auto & en : file.enums)
    {
        out << "class " << en.name << "(IntEnum):\n";
        if (en.values.empty())
        {
            out << "    Invalid = 0\n";
        }
        else
        {
            for (const auto & item : en.values)
            {
                out << "    " << item.name << " = " << item.value << "\n";
            }
        }
        out << "\n";
    }

    for (const auto & st : file.structs)
    {
        out << "@dataclass\n";
        out << "class " << st.name << ":\n";
        if (st.fields.empty())
        {
            out << "    _placeholder: int = 0\n";
        }
        else
        {
            for (const auto & field : st.fields)
            {
                const std::string pyType = pythonTypeFromRaw(field.typeName, enumTypeNames);
                const std::string pyDefault = pythonDefaultForType(field.typeName, enumTypeNames);
                out << "    " << field.name << ": " << pyType << " = " << pyDefault << "\n";
            }
        }
        out << "\n";
        out << "    def to_dict(self) -> Dict[str, Any]:\n";
        out << "        return {\n";
        for (const auto & field : st.fields)
        {
            out << "            \"" << field.name << "\": _encode_value(self." << field.name << "),\n";
        }
        out << "        }\n\n";
        out << "    @classmethod\n";
        out << "    def from_dict(cls, data: Dict[str, Any]) -> \"" << st.name << "\":\n";
        out << "        kwargs: Dict[str, Any] = {}\n";
        out << "        hints = cls.__annotations__\n";
        for (const auto & field : st.fields)
        {
            out << "        kwargs[\"" << field.name << "\"] = _decode_value(data.get(\"" << field.name
                << "\"), hints[\"" << field.name << "\"])\n";
        }
        out << "        return cls(**kwargs)\n\n";
        out << "    def serialize(self) -> bytes:\n";
        out << "        return json.dumps(self.to_dict(), separators=(\",\", \":\")).encode(\"utf-8\")\n\n";
        out << "    @classmethod\n";
        out << "    def deserialize(cls, payload: bytes) -> \"" << st.name << "\":\n";
        out << "        return cls.from_dict(json.loads(payload.decode(\"utf-8\")))\n\n";
    }

    for (const auto & un : file.unions)
    {
        out << "@dataclass\n";
        out << "class " << un.name << ":\n";
        out << "    discriminator: " << pythonTypeFromRaw(un.discriminatorType, enumTypeNames) << " = 0\n";
        if (un.cases.empty())
        {
            out << "    _placeholder: int = 0\n";
        }
        else
        {
            for (const auto & uc : un.cases)
            {
                out << "    " << uc.field.name << ": "
                    << pythonTypeFromRaw(uc.field.typeName, enumTypeNames)
                    << " = " << pythonDefaultForType(uc.field.typeName, enumTypeNames) << "\n";
            }
        }
        out << "\n";
        out << "    def to_dict(self) -> Dict[str, Any]:\n";
        out << "        return {\n";
        out << "            \"discriminator\": _encode_value(self.discriminator),\n";
        for (const auto & uc : un.cases)
        {
            out << "            \"" << uc.field.name << "\": _encode_value(self." << uc.field.name << "),\n";
        }
        out << "        }\n\n";
        out << "    @classmethod\n";
        out << "    def from_dict(cls, data: Dict[str, Any]) -> \"" << un.name << "\":\n";
        out << "        kwargs: Dict[str, Any] = {}\n";
        out << "        hints = cls.__annotations__\n";
        out << "        kwargs[\"discriminator\"] = _decode_value(data.get(\"discriminator\"), hints[\"discriminator\"])\n";
        for (const auto & uc : un.cases)
        {
            out << "        kwargs[\"" << uc.field.name << "\"] = _decode_value(data.get(\"" << uc.field.name
                << "\"), hints[\"" << uc.field.name << "\"])\n";
        }
        out << "        return cls(**kwargs)\n\n";
        out << "    def serialize(self) -> bytes:\n";
        out << "        return json.dumps(self.to_dict(), separators=(\",\", \":\")).encode(\"utf-8\")\n\n";
        out << "    @classmethod\n";
        out << "    def deserialize(cls, payload: bytes) -> \"" << un.name << "\":\n";
        out << "        return cls.from_dict(json.loads(payload.decode(\"utf-8\")))\n\n";
    }

    out << "TOPICS: Dict[str, int] = {\n";
    for (const auto & topic : file.topics)
    {
        out << "    \"" << topic.name << "\": " << topic.id << ",\n";
    }
    out << "}\n";
    for (const auto & topic : file.topics)
    {
        out << topic.name << " = TOPICS[\"" << topic.name << "\"]\n";
    }
    out << "TYPE_TO_TOPIC: Dict[str, int] = {\n";
    for (const auto & topic : file.topics)
    {
        out << "    \"" << topic.typeName << "\": TOPICS[\"" << topic.name << "\"],\n";
    }
    out << "}\n";
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

    if (m_target == TargetLanguage::Cpp)
    {
        const fs::path installRoot =
            m_options.installRoot.empty()
                ? outputDir.parent_path()
                : fs::path(m_options.installRoot);
        const fs::path lddsRoot =
            m_options.lddsRoot.empty()
                ? fs::current_path()
                : fs::path(m_options.lddsRoot);
        const fs::path installIncludeDir = installRoot / "include" / prefix;
        const fs::path installLibDir = installRoot / "lib";

        const std::string exportText = generateExportHeader(prefix);
        const std::string defineText = generateDefineHeader(prefix, *idlFile);
        const std::string topicHText = generateTopicHeader(prefix, *idlFile);
        const std::string topicCppText = generateTopicCpp(prefix, *idlFile);
        const std::string cmakeText = generateModuleCMakeLists(prefix, *idlFile, lddsRoot, installRoot);
        const std::string vcxprojText = generateModuleVcxproj(prefix, *idlFile, lddsRoot, installRoot);
        const std::string slnText = generateModuleSln(prefix);

        const fs::path exportPath = outputDir / (prefix + "_export.h");
        const fs::path definePath = outputDir / (prefix + "_define.h");
        const fs::path topicHPath = outputDir / (prefix + "_topic.h");
        const fs::path topicCppPath = outputDir / (prefix + "_topic.cpp");
        const fs::path cmakePath = outputDir / "CMakeLists.txt";
        const fs::path vcxprojPath = outputDir / (prefix + ".vcxproj");
        const fs::path slnPath = outputDir / (prefix + ".sln");
        fs::create_directories(installIncludeDir, ec);
        fs::create_directories(installLibDir, ec);
        if (ec)
        {
            result.messages.push_back("failed to create install directories");
            return result;
        }

        if (!writeTextFile(exportPath, exportText) ||
            !writeTextFile(definePath, defineText) ||
            !writeTextFile(topicHPath, topicHText) ||
            !writeTextFile(topicCppPath, topicCppText) ||
            !writeTextFile(cmakePath, cmakeText) ||
            !writeTextFile(vcxprojPath, vcxprojText) ||
            !writeTextFile(slnPath, slnText) ||
            !writeTextFile(installIncludeDir / (prefix + "_export.h"), exportText) ||
            !writeTextFile(installIncludeDir / (prefix + "_define.h"), defineText) ||
            !writeTextFile(installIncludeDir / (prefix + "_topic.h"), topicHText))
        {
            result.messages.push_back("failed to write generated files");
            return result;
        }

        result.success = true;
        result.outputPath = outputDir.string();
        result.generatedCode = defineText;
        result.linesGenerated = countLines(exportText) + countLines(defineText) +
                                countLines(topicHText) + countLines(topicCppText) +
                                countLines(cmakeText) + countLines(vcxprojText) + countLines(slnText);

        if (m_callback)
        {
            m_callback((outputDir / (prefix + "_define.h")).string(), 1, 7, "generated");
            m_callback((outputDir / (prefix + "_export.h")).string(), 2, 7, "generated");
            m_callback((outputDir / (prefix + "_topic.h")).string(), 3, 7, "generated");
            m_callback((outputDir / (prefix + "_topic.cpp")).string(), 4, 7, "generated");
            m_callback(cmakePath.string(), 5, 7, "generated");
            m_callback(vcxprojPath.string(), 6, 7, "generated");
            m_callback(slnPath.string(), 7, 7, "generated");
        }
    }
    else if (m_target == TargetLanguage::Python)
    {
        const std::string pythonText = generatePythonModule(prefix, *idlFile);
        const fs::path pythonPath = outputDir / (prefix + "_types.py");
        if (!writeTextFile(pythonPath, pythonText))
        {
            result.messages.push_back("failed to write generated python file");
            return result;
        }

        result.success = true;
        result.outputPath = pythonPath.string();
        result.generatedCode = pythonText;
        result.linesGenerated = countLines(pythonText);
        if (m_callback)
        {
            m_callback(pythonPath.string(), 1, 1, "generated");
        }
    }
    else
    {
        result.messages.push_back("target language is declared but not implemented yet");
        return result;
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
        return {"_define.h", "_export.h", "_topic.h", "_topic.cpp", "CMakeLists.txt", ".vcxproj", ".sln"};
    case TargetLanguage::CSharp:
        return {".cs"};
    case TargetLanguage::Java:
        return {".java"};
    case TargetLanguage::Python:
        return {"_types.py"};
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
