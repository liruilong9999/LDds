#include "IdlParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace std;

///////////////
/// 解析IDL文件
/// fileName 文件名
/// return 解析结果
/// by:李瑞龙
///////////////
shared_ptr<IdlFile> IdlParser::parseFile(const string& fileName)
{
    if (m_parsedFiles.find(fileName) != m_parsedFiles.end())
    {
        return m_parsedFiles[fileName];
    }

    ifstream file(fileName);
    if (!file.is_open())
    {
        cerr << "Cannot open file: " << fileName << endl;
        return nullptr;
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string content = buffer.str();

    auto result = parseContent(content, fileName);
    if (result)
    {
        m_parsedFiles[fileName] = result;
    }

    return result;
}

///////////////
/// 解析IDL内容
/// content IDL内容
/// fileName 文件名
/// return 解析结果
/// by:李瑞龙
///////////////
shared_ptr<IdlFile> IdlParser::parseContent(const string& content, const string& fileName)
{
    m_tokens.clear();
    m_currentToken = 0;

    tokenize(content);

    auto idlFile = parse();
    if (idlFile)
    {
        idlFile->m_fileName = fileName;
    }

    return idlFile;
}

///////////////
/// 词法分析
/// content 要分析的内容
/// by:李瑞龙
///////////////
void IdlParser::tokenize(const string& content)
{
    size_t pos = 0;
    size_t length = content.length();
    int line = 1;

    auto isIdentifierChar = [](char c) {
        return isalnum(c) || c == '_';
        };

    while (pos < length)
    {
        char current = content[pos];

        // 跳过空白
        if (isspace(current))
        {
            if (current == '\n') line++;
            pos++;
            continue;
        }

        // 注释
        if (current == '/' && pos + 1 < length && content[pos + 1] == '/')
        {
            size_t start = pos;
            pos += 2; // 跳过 "//"
            while (pos < length && content[pos] != '\n')
            {
                pos++;
            }
            string comment = content.substr(start, pos - start);
            m_tokens.emplace_back("COMMENT", comment, line);
            continue;
        }

        // 字符串
        if (current == '"' || current == '\'')
        {
            size_t start = pos;
            char quote = current;
            pos++; // 跳过开引号

            while (pos < length && content[pos] != quote)
            {
                if (content[pos] == '\\' && pos + 1 < length)
                {
                    pos += 2; // 跳过转义序列
                }
                else
                {
                    pos++;
                }
            }

            if (pos < length) pos++; // 跳过闭引号
            string str = content.substr(start, pos - start);
            m_tokens.emplace_back("STRING", str, line);
            continue;
        }

        // 属性
        if (current == '[')
        {
            size_t start = pos;
            pos++; // 跳过 '['
            int bracketCount = 1;

            while (pos < length && bracketCount > 0)
            {
                if (content[pos] == '[') bracketCount++;
                else if (content[pos] == ']') bracketCount--;
                pos++;
            }

            string attr = content.substr(start, pos - start);
            m_tokens.emplace_back("ATTRIBUTE", attr, line);
            continue;
        }

        // 数字
        if (isdigit(current) || (current == '-' && pos + 1 < length && isdigit(content[pos + 1])))
        {
            size_t start = pos;
            if (current == '-') pos++;

            while (pos < length && isdigit(content[pos])) pos++;

            if (pos < length && content[pos] == '.')
            {
                pos++;
                while (pos < length && isdigit(content[pos])) pos++;
            }

            string num = content.substr(start, pos - start);
            m_tokens.emplace_back("NUMBER", num, line);
            continue;
        }

        // 标识符
        if (isalpha(current) || current == '_')
        {
            size_t start = pos;
            while (pos < length && isIdentifierChar(content[pos])) pos++;

            string id = content.substr(start, pos - start);
            m_tokens.emplace_back("IDENTIFIER", id, line);
            continue;
        }

        // 操作符和分隔符
        switch (current)
        {
        case ';': m_tokens.emplace_back("SEMICOLON", ";", line); pos++; break;
        case '{': m_tokens.emplace_back("LBRACE", "{", line); pos++; break;
        case '}': m_tokens.emplace_back("RBRACE", "}", line); pos++; break;
        case ':':
            if (pos + 1 < length && content[pos + 1] == ':')
            {
                m_tokens.emplace_back("SCOPE", "::", line);
                pos += 2;
            }
            else
            {
                m_tokens.emplace_back("COLON", ":", line);
                pos++;
            }
            break;
        case ',': m_tokens.emplace_back("COMMA", ",", line); pos++; break;
        case '=': m_tokens.emplace_back("EQUALS", "=", line); pos++; break;
        case '<': m_tokens.emplace_back("LT", "<", line); pos++; break;
        case '>': m_tokens.emplace_back("GT", ">", line); pos++; break;
        default:
            // 未知字符，跳过
            cerr << "Warning: Unknown character '" << current << "' at line " << line << endl;
            pos++;
            break;
        }
    }
}

///////////////
/// 语法分析
/// return 解析结果
/// by:李瑞龙
///////////////
shared_ptr<IdlFile> IdlParser::parse()
{
    auto idlFile = make_shared<IdlFile>();
    vector<string> currentNamespaces;

    while (m_currentToken < m_tokens.size())
    {
        if (checkToken("IDENTIFIER"))
        {
            string identifier = currentToken().m_value;

            if (identifier == "package")
            {
                parsePackage(currentNamespaces, idlFile);
            }
            else if (identifier == "struct")
            {
                parseStruct(currentNamespaces, idlFile);
            }
            else if (identifier == "include")
            {
                consumeToken("IDENTIFIER");
                if (checkToken("STRING"))
                {
                    string includeFile = currentToken().m_value;
                    includeFile = includeFile.substr(1, includeFile.length() - 2); // 去除引号
                    idlFile->m_includes.push_back(includeFile);
                    consumeToken("STRING");
                    consumeToken("SEMICOLON");
                }
            }
            else
            {
                // 可能是主题定义
                parseTopic(idlFile);
            }
        }
        else if (checkToken("COMMENT"))
        {
            // 跳过注释
            nextToken();
        }
        else
        {
            nextToken();
        }
    }

    return idlFile;
}

///////////////
/// 获取当前令牌
/// return 当前令牌
/// by:李瑞龙
///////////////
Token IdlParser::currentToken() const
{
    if (m_currentToken < m_tokens.size())
    {
        return m_tokens[m_currentToken];
    }
    return Token("EOF", "", -1);
}

///////////////
/// 获取下一个令牌
/// return 下一个令牌
/// by:李瑞龙
///////////////
Token IdlParser::nextToken()
{
    if (m_currentToken < m_tokens.size())
    {
        return m_tokens[m_currentToken++];
    }
    return Token("EOF", "", -1);
}

///////////////
/// 检查令牌类型
/// type 期望的类型
/// return 是否匹配
/// by:李瑞龙
///////////////
bool IdlParser::checkToken(const string& type) const
{
    return currentToken().m_type == type;
}

///////////////
/// 消费令牌
/// type 期望的类型
/// by:李瑞龙
///////////////
void IdlParser::consumeToken(const string& type)
{
    if (checkToken(type))
    {
        nextToken();
    }
    else
    {
        cerr << "Syntax error: Expected " << type << ", but got " << currentToken().m_type
            << " (" << currentToken().m_value << ") at line " << currentToken().m_line << endl;
        nextToken();
    }
}

///////////////
/// 解析包定义
/// currentNamespaces 当前命名空间
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void IdlParser::parsePackage(vector<string> currentNamespaces, shared_ptr<IdlFile> idlFile)
{
    consumeToken("IDENTIFIER"); // package

    vector<string> packageNames;
    while (checkToken("IDENTIFIER"))
    {
        packageNames.push_back(currentToken().m_value);
        nextToken();

        if (checkToken("LBRACE"))
        {
            break;
        }
        else if (checkToken("SEMICOLON"))
        {
            consumeToken("SEMICOLON");
            return;
        }
    }

    // 构建完整的命名空间路径
    vector<string> fullNamespace = currentNamespaces;
    fullNamespace.insert(fullNamespace.end(), packageNames.begin(), packageNames.end());
    idlFile->m_namespaces.push_back(fullNamespace);

    consumeToken("LBRACE");

    // 解析包内内容，使用新的命名空间
    vector<string> innerNamespaces = fullNamespace;

    while (!checkToken("RBRACE"))
    {
        if (checkToken("IDENTIFIER"))
        {
            string identifier = currentToken().m_value;

            if (identifier == "package")
            {
                parsePackage(innerNamespaces, idlFile);
            }
            else if (identifier == "struct")
            {
                parseStruct(innerNamespaces, idlFile);
            }
            else
            {
                nextToken();
            }
        }
        else if (checkToken("COMMENT"))
        {
            nextToken();
        }
        else
        {
            nextToken();
        }
    }

    consumeToken("RBRACE");
    consumeToken("SEMICOLON");
}

///////////////
/// 解析结构体定义
/// currentNamespaces 当前命名空间
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void IdlParser::parseStruct(const vector<string>& currentNamespaces, shared_ptr<IdlFile> idlFile)
{
    string comment = parseComment();

    consumeToken("IDENTIFIER"); // struct

    if (!checkToken("IDENTIFIER"))
    {
        cerr << "Syntax error: Expected struct name" << endl;
        return;
    }

    Struct struc(currentToken().m_value);
    struc.m_comment = comment;
    struc.m_namespace = currentNamespaces;

    consumeToken("IDENTIFIER");

    // 检查继承
    if (checkToken("COLON"))
    {
        consumeToken("COLON");
        if (checkToken("IDENTIFIER") && currentToken().m_value == "extend")
        {
            consumeToken("IDENTIFIER");
            if (checkToken("IDENTIFIER"))
            {
                struc.m_parent = currentToken().m_value;
                consumeToken("IDENTIFIER");

                // 处理作用域解析
                while (checkToken("SCOPE"))
                {
                    consumeToken("SCOPE");
                    if (checkToken("IDENTIFIER"))
                    {
                        struc.m_parent += "::" + currentToken().m_value;
                        consumeToken("IDENTIFIER");
                    }
                }
            }
        }
    }

    consumeToken("LBRACE");

    // 解析字段
    while (!checkToken("RBRACE"))
    {
        if (checkToken("ATTRIBUTE") || checkToken("IDENTIFIER") || checkToken("COMMENT"))
        {
            parseField(struc);
        }
        else
        {
            nextToken();
        }
    }

    consumeToken("RBRACE");
    consumeToken("SEMICOLON");

    idlFile->m_structs.push_back(struc);
}

///////////////
/// 解析字段定义
/// currentStruct 当前结构体
/// by:李瑞龙
///////////////
void IdlParser::parseField(Struct& currentStruct)
{
    string comment = parseComment();

    vector<FieldAttribute> attrs;
    if (checkToken("ATTRIBUTE"))
    {
        attrs = parseFieldAttributes();
    }

    if (!checkToken("IDENTIFIER"))
    {
        return;
    }

    string fieldType = currentToken().m_value;
    consumeToken("IDENTIFIER");

    // 处理vector类型
    if (fieldType == "vector" && checkToken("LT"))
    {
        consumeToken("LT"); // 吃掉 <

        // 解析vector内部的类型
        if (!checkToken("IDENTIFIER"))
        {
            cerr << "Error: Expected type inside vector<> at line " << currentToken().m_line << endl;
            return;
        }

        string innerType = currentToken().m_value;
        consumeToken("IDENTIFIER");

        // 吃掉 >
        if (!checkToken("GT"))
        {
            cerr << "Error: Expected '>' after vector type at line " << currentToken().m_line << endl;
            return;
        }
        consumeToken("GT");

        fieldType = "vector<" + innerType + ">";
    }

    if (!checkToken("IDENTIFIER"))
    {
        cerr << "Syntax error: Expected field name at line " << currentToken().m_line << endl;
        return;
    }

    string fieldName = currentToken().m_value;
    consumeToken("IDENTIFIER");

    if (!checkToken("SEMICOLON"))
    {
        cerr << "Syntax error: Expected ';' after field declaration, but got "
            << currentToken().m_type << " (" << currentToken().m_value
            << ") at line " << currentToken().m_line << endl;
        skipUntilSemicolon();
    }

    if (checkToken("SEMICOLON"))
    {
        consumeToken("SEMICOLON");
    }

    Field field(fieldType, fieldName);
    field.m_attrs = attrs;
    field.m_comment = comment;

    currentStruct.m_fields.push_back(field);
}

///////////////
/// 解析字段属性
/// return 字段属性
/// by:李瑞龙
///////////////
vector<FieldAttribute> IdlParser::parseFieldAttributes()
{
    vector<FieldAttribute> attrs;

    if (!checkToken("ATTRIBUTE"))
    {
        return attrs;
    }

    string attrStr = currentToken().m_value;
    consumeToken("ATTRIBUTE");

    // 解析属性内容 [name,value,"comment"]
    attrStr = attrStr.substr(1, attrStr.length() - 2); // 去除方括号

    vector<string> parts;
    string currentPart;
    bool inQuotes = false;
    char quoteChar = 0;

    for (char c : attrStr)
    {
        if ((c == '"' || c == '\'') && !inQuotes)
        {
            inQuotes = true;
            quoteChar = c;
            currentPart += c;
        }
        else if (c == quoteChar && inQuotes)
        {
            inQuotes = false;
            currentPart += c;
        }
        else if (c == ',' && !inQuotes)
        {
            parts.push_back(currentPart);
            currentPart.clear();
        }
        else
        {
            currentPart += c;
        }
    }

    if (!currentPart.empty())
    {
        parts.push_back(currentPart);
    }

    if (parts.size() >= 3)
    {
        string name = parts[0];
        string defaultValue = parts[1];
        string comment = parts[2];

        // 去除引号
        if (!defaultValue.empty() && defaultValue.front() == '"' && defaultValue.back() == '"')
        {
            defaultValue = defaultValue.substr(1, defaultValue.length() - 2);
        }
        if (!comment.empty() && comment.front() == '"' && comment.back() == '"')
        {
            comment = comment.substr(1, comment.length() - 2);
        }

        attrs.emplace_back(name, defaultValue, comment);
    }

    return attrs;
}

///////////////
/// 解析主题定义
/// idlFile IDL文件对象
/// by:李瑞龙
///////////////
void IdlParser::parseTopic(shared_ptr<IdlFile> idlFile)
{
    string comment = parseComment();

    if (!checkToken("IDENTIFIER"))
    {
        return;
    }

    string topicName = currentToken().m_value;
    consumeToken("IDENTIFIER");

    if (!checkToken("IDENTIFIER") || currentToken().m_value != "=")
    {
        return;
    }

    consumeToken("IDENTIFIER"); // =

    if (!checkToken("IDENTIFIER"))
    {
        return;
    }

    string structName = currentToken().m_value;
    consumeToken("IDENTIFIER");

    // 处理作用域解析
    while (checkToken("SCOPE"))
    {
        consumeToken("SCOPE");
        if (checkToken("IDENTIFIER"))
        {
            structName += "::" + currentToken().m_value;
            consumeToken("IDENTIFIER");
        }
    }

    consumeToken("SEMICOLON");

    Topic topic(topicName, structName);
    topic.m_comment = comment;
    idlFile->m_topics.push_back(topic);
}

///////////////
/// 解析注释
/// return 注释内容
/// by:李瑞龙
///////////////
string IdlParser::parseComment()
{
    string comment;

    // 向前查找注释
    size_t tempToken = m_currentToken;
    while (tempToken > 0 && tempToken < m_tokens.size())
    {
        tempToken--;
        if (m_tokens[tempToken].m_type == "COMMENT")
        {
            comment = m_tokens[tempToken].m_value;
            // 去除//前缀
            if (comment.length() > 2)
            {
                comment = comment.substr(2);
            }
            break;
        }
        else if (m_tokens[tempToken].m_type == "IDENTIFIER" ||
            m_tokens[tempToken].m_type == "SEMICOLON" ||
            m_tokens[tempToken].m_type == "RBRACE")
        {
            break;
        }
    }

    return comment;
}

///////////////
/// 跳过直到分号
/// by:李瑞龙
///////////////
void IdlParser::skipUntilSemicolon()
{
    while (m_currentToken < m_tokens.size() && !checkToken("SEMICOLON"))
    {
        nextToken();
    }
    if (checkToken("SEMICOLON"))
    {
        consumeToken("SEMICOLON");
    }
}