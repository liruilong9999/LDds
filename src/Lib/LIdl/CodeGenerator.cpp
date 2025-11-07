#include "CodeGenerator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

using namespace std;

///////////////
/// 构造函数
/// outputDir 输出目录
/// by:李瑞龙
///////////////
CodeGenerator::CodeGenerator(const string& outputDir)
    : m_outputDir(outputDir)
{
    ensureOutputDir();
}

///////////////
/// 生成代码文件
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void CodeGenerator::generateCode(shared_ptr<IdlFile> idlFile)
{
    if (!idlFile)
    {
        return;
    }

    generateDefineHeader(idlFile);
    generateExportHeader(idlFile);
    generateTopicHeader(idlFile);
    generateTopicCpp(idlFile);
}

///////////////
/// 生成定义头文件
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void CodeGenerator::generateDefineHeader(shared_ptr<IdlFile> idlFile)
{
    string filePrefix = getFilePrefix(idlFile->m_fileName);
    string headerName = filePrefix + "_define.h";

    ostringstream oss;

    // 头文件保护
    oss << "#ifndef " << filePrefix << "_DEFINE_H" << endl;
    oss << "#define " << filePrefix << "_DEFINE_H" << endl;
    oss << endl;

    // 包含头文件
    oss << "#include <vector>" << endl;
    oss << "#include <string>" << endl;
    oss << "#include <cstdint>" << endl;
    oss << endl;

    // 按命名空间分组结构体
    map<vector<string>, vector<Struct>> namespaceStructs;
    for (const auto& struc : idlFile->m_structs)
    {
        namespaceStructs[struc.m_namespace].push_back(struc);
    }

    // 生成命名空间和结构体
    for (const auto& nsPair : namespaceStructs)
    {
        const auto& namespaces = nsPair.first;
        const auto& structs = nsPair.second;

        generateNamespaceBegin(namespaces, oss);

        for (const auto& struc : structs)
        {
            generateStructCode(struc, oss);
            oss << endl;
        }

        generateNamespaceEnd(namespaces, oss);
    }

    oss << "#endif // " << filePrefix << "_DEFINE_H" << endl;

    writeFile(headerName, oss.str());
}

///////////////
/// 生成导出头文件
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void CodeGenerator::generateExportHeader(shared_ptr<IdlFile> idlFile)
{
    string filePrefix = getFilePrefix(idlFile->m_fileName);
    string headerName = filePrefix + "_export.h";

    ostringstream oss;

    oss << "#ifndef " << filePrefix << "_EXPORT_H" << endl;
    oss << "#define " << filePrefix << "_EXPORT_H" << endl;
    oss << endl;

    // 导出宏定义
    oss << "#ifdef " << filePrefix << "_BUILD_DLL" << endl;
    oss << "#define " << filePrefix << "_EXPORT __declspec(dllexport)" << endl;
    oss << "#else" << endl;
    oss << "#define " << filePrefix << "_EXPORT __declspec(dllimport)" << endl;
    oss << "#endif" << endl;
    oss << endl;

    oss << "#endif // " << filePrefix << "_EXPORT_H" << endl;

    writeFile(headerName, oss.str());
}

///////////////
/// 生成主题头文件
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void CodeGenerator::generateTopicHeader(shared_ptr<IdlFile> idlFile)
{
    string filePrefix = getFilePrefix(idlFile->m_fileName);
    string headerName = filePrefix + "_topic.h";

    ostringstream oss;

    oss << "#ifndef " << filePrefix << "_TOPIC_H" << endl;
    oss << "#define " << filePrefix << "_TOPIC_H" << endl;
    oss << endl;

    oss << "#include \"" << filePrefix << "_define.h\"" << endl;
    oss << "#include \"" << filePrefix << "_export.h\"" << endl;
    oss << "#include <map>" << endl;
    oss << "#include <string>" << endl;
    oss << "#include <functional>" << endl;
    oss << endl;

    // 主题枚举
    oss << "enum class " << filePrefix << "Topic" << endl;
    oss << "{" << endl;
    for (size_t i = 0; i < idlFile->m_topics.size(); ++i)
    {
        const auto& topic = idlFile->m_topics[i];
        oss << "    " << getTopicEnumName(topic) << " = " << i;
        if (i < idlFile->m_topics.size() - 1)
        {
            oss << ",";
        }
        if (!topic.m_comment.empty())
        {
            oss << " // " << topic.m_comment;
        }
        oss << endl;
    }
    oss << "};" << endl;
    oss << endl;

    // 主题管理类
    oss << "class " << filePrefix << "_EXPORT " << filePrefix << "TopicManager" << endl;
    oss << "{" << endl;
    oss << "public:" << endl;
    oss << "    static " << filePrefix << "TopicManager& getInstance();" << endl;
    oss << endl;
    oss << "    void registerTopic(" << filePrefix << "Topic topic, const std::string& topicName);" << endl;
    oss << "    std::string getTopicName(" << filePrefix << "Topic topic) const;" << endl;
    oss << "    " << filePrefix << "Topic getTopic(const std::string& topicName) const;" << endl;
    oss << endl;
    oss << "private:" << endl;
    oss << "    std::map<" << filePrefix << "Topic, std::string> m_topicMap;" << endl;
    oss << "    std::map<std::string, " << filePrefix << "Topic> m_nameMap;" << endl;
    oss << "};" << endl;
    oss << endl;

    oss << "#endif // " << filePrefix << "_TOPIC_H" << endl;

    writeFile(headerName, oss.str());
}

///////////////
/// 生成主题实现文件
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void CodeGenerator::generateTopicCpp(shared_ptr<IdlFile> idlFile)
{
    string filePrefix = getFilePrefix(idlFile->m_fileName);
    string cppName = filePrefix + "_topic.cpp";

    ostringstream oss;

    oss << "#include \"" << filePrefix << "_topic.h\"" << endl;
    oss << endl;

    // 单例实现
    oss << filePrefix << "TopicManager& " << filePrefix << "TopicManager::getInstance()" << endl;
    oss << "{" << endl;
    oss << "    static " << filePrefix << "TopicManager instance;" << endl;
    oss << "    return instance;" << endl;
    oss << "}" << endl;
    oss << endl;

    // registerTopic实现
    oss << "void " << filePrefix << "TopicManager::registerTopic(" << filePrefix << "Topic topic, const std::string& topicName)" << endl;
    oss << "{" << endl;
    oss << "    m_topicMap[topic] = topicName;" << endl;
    oss << "    m_nameMap[topicName] = topic;" << endl;
    oss << "}" << endl;
    oss << endl;

    // getTopicName实现
    oss << "std::string " << filePrefix << "TopicManager::getTopicName(" << filePrefix << "Topic topic) const" << endl;
    oss << "{" << endl;
    oss << "    auto it = m_topicMap.find(topic);" << endl;
    oss << "    if (it != m_topicMap.end())" << endl;
    oss << "    {" << endl;
    oss << "        return it->second;" << endl;
    oss << "    }" << endl;
    oss << "    return \"\";" << endl;
    oss << "}" << endl;
    oss << endl;

    // getTopic实现
    oss << filePrefix << "Topic " << filePrefix << "TopicManager::getTopic(const std::string& topicName) const" << endl;
    oss << "{" << endl;
    oss << "    auto it = m_nameMap.find(topicName);" << endl;
    oss << "    if (it != m_nameMap.end())" << endl;
    oss << "    {" << endl;
    oss << "        return it->second;" << endl;
    oss << "    }" << endl;
    oss << "    return static_cast<" << filePrefix << "Topic>(-1);" << endl;
    oss << "}" << endl;
    oss << endl;

    // 静态初始化
    oss << "namespace" << endl;
    oss << "{" << endl;
    oss << "    class " << filePrefix << "TopicInitializer" << endl;
    oss << "    {" << endl;
    oss << "    public:" << endl;
    oss << "        " << filePrefix << "TopicInitializer()" << endl;
    oss << "        {" << endl;
    for (const auto& topic : idlFile->m_topics)
    {
        oss << "            " << filePrefix << "TopicManager::getInstance().registerTopic("
            << filePrefix << "Topic::" << getTopicEnumName(topic) << ", \""
            << topic.m_name << "\");" << endl;
    }
    oss << "        }" << endl;
    oss << "    };" << endl;
    oss << endl;
    oss << "    static " << filePrefix << "TopicInitializer initializer;" << endl;
    oss << "}" << endl;

    writeFile(cppName, oss.str());
}

///////////////
/// 获取文件名前缀
/// fileName 文件名
/// return 文件名前缀
/// by:李瑞龙
///////////////
string CodeGenerator::getFilePrefix(const string& fileName) const
{
    string prefix = fileName;

    // 提取文件名（不含路径）
    size_t lastSlash = prefix.find_last_of("/\\");
    if (lastSlash != string::npos)
    {
        prefix = prefix.substr(lastSlash + 1);
    }

    // 去除扩展名
    size_t dotPos = prefix.find_last_of('.');
    if (dotPos != string::npos)
    {
        prefix = prefix.substr(0, dotPos);
    }

    // 转换为大驼峰
    bool makeUpper = true;
    for (char& c : prefix)
    {
        if (makeUpper)
        {
            c = toupper(c);
            makeUpper = false;
        }
        else if (c == '_' || c == ' ')
        {
            makeUpper = true;
        }
        else if (c == '\\' || c == '/')
        {
            // 跳过路径分隔符
            makeUpper = true;
        }
    }

    return prefix;
}

///////////////
/// 转换IDL类型到C++类型
/// idlType IDL类型
/// return C++类型
/// by:李瑞龙
///////////////
string CodeGenerator::convertTypeToCpp(const string& idlType) const
{
    if (idlType == "int")
    {
        return "int";
    }
    else if (idlType == "int64")
    {
        return "long long";
    }
    else if (idlType == "double")
    {
        return "double";
    }
    else if (idlType == "string")
    {
        return "std::string";
    }
    else if (idlType.find("vector<") == 0)
    {
        string elementType = idlType.substr(7, idlType.length() - 8);
        return "std::vector<" + convertTypeToCpp(elementType) + ">";
    }

    return idlType;
}

///////////////
/// 生成命名空间开始
/// namespaces 命名空间列表
/// oss 输出流
/// by:李瑞龙
///////////////
void CodeGenerator::generateNamespaceBegin(const vector<string>& namespaces, ostringstream& oss)
{
    if (namespaces.empty())
    {
        return;
    }

    oss << "namespace " << namespaces[0];
    for (size_t i = 1; i < namespaces.size(); ++i)
    {
        oss << " { namespace " << namespaces[i];
    }
    oss << " {" << endl << endl;
}

///////////////
/// 生成命名空间结束
/// namespaces 命名空间列表
/// oss 输出流
/// by:李瑞龙
///////////////
void CodeGenerator::generateNamespaceEnd(const vector<string>& namespaces, ostringstream& oss)
{
    if (namespaces.empty())
    {
        return;
    }

    for (size_t i = 0; i < namespaces.size(); ++i)
    {
        oss << "}";
        if (i == 0)
        {
            oss << " // namespace " << namespaces[0];
        }
        else
        {
            oss << " // namespace " << namespaces[i];
        }
        oss << endl;
    }
    oss << endl;
}

///////////////
/// 生成字段默认值
/// field 字段
/// return 默认值代码
/// by:李瑞龙
///////////////
string CodeGenerator::generateFieldDefaultValue(const Field& field) const
{
    // 查找属性中的默认值
    for (const auto& attr : field.m_attrs)
    {
        if (!attr.m_defaultValue.empty())
        {
            string cppType = convertTypeToCpp(field.m_type);
            if (cppType == "std::string")
            {
                return "{\"" + attr.m_defaultValue + "\"}";
            }
            else if (cppType.find("std::vector<") == 0)
            {
                return "{}"; // 容器类型不使用属性中的默认值
            }
            else
            {
                return "{" + attr.m_defaultValue + "}";
            }
        }
    }

    // 根据类型生成默认值
    string cppType = convertTypeToCpp(field.m_type);
    if (cppType == "std::string")
    {
        return "{}";
    }
    else if (cppType.find("std::vector<") == 0)
    {
        return "{}";
    }
    else
    {
        return "{}";
    }
}

///////////////
/// 生成结构体代码
/// struc 结构体
/// oss 输出流
/// by:李瑞龙
///////////////
void CodeGenerator::generateStructCode(const Struct& struc, ostringstream& oss)
{
    // 结构体注释
    if (!struc.m_comment.empty())
    {
        oss << "///////////////" << endl;
        oss << "/// " << struc.m_comment << endl;
        oss << "/// by:李瑞龙" << endl;
        oss << "///////////////" << endl;
    }

    // 结构体定义开始
    oss << "struct " << struc.m_name;
    if (!struc.m_parent.empty())
    {
        oss << " : public " << struc.m_parent;
    }
    oss << endl;
    oss << "{" << endl;

    // 字段定义
    for (const auto& field : struc.m_fields)
    {
        string cppType = convertTypeToCpp(field.m_type);
        string defaultValue = generateFieldDefaultValue(field);

        oss << "    " << cppType << " " << field.m_name << defaultValue << ";";

        // 字段注释
        string fieldComment;
        for (const auto& attr : field.m_attrs)
        {
            if (!attr.m_comment.empty())
            {
                fieldComment = attr.m_comment;
                break;
            }
        }
        if (fieldComment.empty())
        {
            fieldComment = field.m_comment;
        }

        if (!fieldComment.empty())
        {
            oss << " // " << fieldComment;
        }
        oss << endl;
    }
    oss << endl;

    // get方法
    oss << "    ///////////////" << endl;
    oss << "    /// 获取结构体指针" << endl;
    oss << "    /// return 结构体指针" << endl;
    oss << "    /// by:李瑞龙" << endl;
    oss << "    ///////////////" << endl;
    oss << "    void* get() { return static_cast<void*>(this); }" << endl;

    oss << "};" << endl;
}

///////////////
/// 获取主题枚举名称
/// topic 主题
/// return 枚举名称
/// by:李瑞龙
///////////////
string CodeGenerator::getTopicEnumName(const Topic& topic) const
{
    string enumName = topic.m_name;

    // 转换为大写，用下划线分隔
    string result;
    for (size_t i = 0; i < enumName.length(); ++i)
    {
        if (isupper(enumName[i]) && i > 0)
        {
            result += '_';
        }
        result += toupper(enumName[i]);
    }

    return result;
}

///////////////
/// 写入文件
/// fileName 文件名
/// content 内容
/// by:李瑞龙
///////////////
void CodeGenerator::writeFile(const string& fileName, const string& content)
{
    string fullPath = m_outputDir + "/" + fileName;

    // 在Windows上，确保使用正斜杠
#ifdef _WIN32
    string normalizedPath = fullPath;
    replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    fullPath = normalizedPath;
#endif

    ofstream file(fullPath);
    if (file.is_open())
    {
        file << content;
        file.close();
        cout << "Generated file: " << fullPath << endl;
    }
    else
    {
        cerr << "Cannot write file: " << fullPath << endl;
    }
}

///////////////
/// 确保输出目录存在
/// by:李瑞龙
///////////////
void CodeGenerator::ensureOutputDir() const
{
#ifdef _WIN32
    _mkdir(m_outputDir.c_str());
#else
    mkdir(m_outputDir.c_str(), 0755);
#endif
}