#ifndef IDL_PARSER_H
#define IDL_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "LidL_global.h"

///////////////
/// Token 结构体
/// 表示IDL文件中的词法单元
/// by:李瑞龙
///////////////
struct Token
{
    std::string m_type;    // 令牌类型
    std::string m_value;   // 令牌值
    int m_line;            // 行号

    Token(const std::string& type, const std::string& value, int line)
        : m_type(type), m_value(value), m_line(line) {
    }
};

///////////////
/// FieldAttribute 结构体
/// 表示字段的属性信息
/// by:李瑞龙
///////////////
struct FieldAttribute
{
    std::string m_name;            // 属性名称
    std::string m_defaultValue;    // 默认值
    std::string m_comment;         // 注释

    FieldAttribute() = default;
    FieldAttribute(const std::string& name, const std::string& defaultValue, const std::string& comment)
        : m_name(name), m_defaultValue(defaultValue), m_comment(comment) {
    }
};

///////////////
/// Field 结构体
/// 表示结构体中的字段
/// by:李瑞龙
///////////////
struct Field
{
    std::string m_type;                    // 字段类型
    std::string m_name;                    // 字段名称
    std::vector<FieldAttribute> m_attrs;   // 字段属性
    std::string m_comment;                 // 字段注释

    Field() = default;
    Field(const std::string& type, const std::string& name)
        : m_type(type), m_name(name) {
    }
};

///////////////
/// Struct 结构体
/// 表示IDL中的结构体定义
/// by:李瑞龙
///////////////
struct Struct
{
    std::string m_name;                    // 结构体名称
    std::string m_parent;                  // 父类名称
    std::vector<Field> m_fields;           // 字段列表
    std::string m_comment;                 // 结构体注释
    std::vector<std::string> m_namespace;  // 命名空间

    Struct() = default;
    Struct(const std::string& name) : m_name(name) {}
};

///////////////
/// Topic 结构体
/// 表示主题定义
/// by:李瑞龙
///////////////
struct Topic
{
    std::string m_name;                    // 主题名称
    std::string m_structName;              // 对应的结构体名称
    std::string m_comment;                 // 主题注释

    Topic() = default;
    Topic(const std::string& name, const std::string& structName)
        : m_name(name), m_structName(structName) {
    }
};

///////////////
/// IdlFile 类
/// 表示一个IDL文件的内容
/// by:李瑞龙
///////////////
class LIDL_EXPORT IdlFile
{
public:
    std::string m_fileName;                    // 文件名
    std::vector<std::string> m_includes;       // 包含的文件
    std::vector<std::vector<std::string>> m_namespaces; // 命名空间层次
    std::vector<Struct> m_structs;             // 结构体列表
    std::vector<Topic> m_topics;               // 主题列表

    IdlFile() = default;
    explicit IdlFile(const std::string& fileName) : m_fileName(fileName) {}
};

///////////////
/// IdlParser 类
/// IDL文件解析器
/// by:李瑞龙
///////////////
class LIDL_EXPORT IdlParser
{
private:
    std::vector<Token> m_tokens;               // 令牌列表
    size_t m_currentToken;                     // 当前令牌索引
    std::map<std::string, std::shared_ptr<IdlFile>> m_parsedFiles; // 已解析文件缓存

public:
    ///////////////
    /// 解析IDL文件
    /// fileName 文件名
    /// return 解析结果
    /// by:李瑞龙
    ///////////////
    std::shared_ptr<IdlFile> parseFile(const std::string& fileName);

    ///////////////
    /// 解析IDL内容
    /// content IDL内容
    /// fileName 文件名
    /// return 解析结果
    /// by:李瑞龙
    ///////////////
    std::shared_ptr<IdlFile> parseContent(const std::string& content, const std::string& fileName = "");

private:
    ///////////////
    /// 词法分析
    /// content 要分析的内容
    /// by:李瑞龙
    ///////////////
    void tokenize(const std::string& content);

    ///////////////
    /// 语法分析
    /// return 解析结果
    /// by:李瑞龙
    ///////////////
    std::shared_ptr<IdlFile> parse();

    ///////////////
    /// 获取当前令牌
    /// return 当前令牌
    /// by:李瑞龙
    ///////////////
    Token currentToken() const;

    ///////////////
    /// 获取下一个令牌
    /// return 下一个令牌
    /// by:李瑞龙
    ///////////////
    Token nextToken();

    ///////////////
    /// 检查令牌类型
    /// type 期望的类型
    /// return 是否匹配
    /// by:李瑞龙
    ///////////////
    bool checkToken(const std::string& type) const;

    ///////////////
    /// 消费令牌
    /// type 期望的类型
    /// by:李瑞龙
    ///////////////
    void consumeToken(const std::string& type);

    ///////////////
    /// 解析包定义
    /// currentNamespaces 当前命名空间
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void parsePackage(std::vector<std::string> currentNamespaces, std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 解析结构体定义
    /// currentNamespaces 当前命名空间
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void parseStruct(const std::vector<std::string>& currentNamespaces, std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 解析字段定义
    /// currentStruct 当前结构体
    /// by:李瑞龙
    ///////////////
    void parseField(Struct& currentStruct);

    ///////////////
    /// 解析字段属性
    /// return 字段属性
    /// by:李瑞龙
    ///////////////
    std::vector<FieldAttribute> parseFieldAttributes();

    ///////////////
    /// 解析主题定义
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void parseTopic(std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 解析注释
    /// return 注释内容
    /// by:李瑞龙
    ///////////////
    std::string parseComment();

    ///////////////
    /// 跳过直到分号
    /// by:李瑞龙
    ///////////////
    void skipUntilSemicolon();
};

#endif // IDL_PARSER_H