#ifndef LIDL_PARSER_STRUCTS_H
#define LIDL_PARSER_STRUCTS_H

#include <string>
#include <vector>
#include <map>

///////////////
/// 结构体成员信息
///////////////
struct StructMember
{
    std::string m_name;          // 成员名
    std::string m_type;          // 成员类型
    std::string m_defaultValue;  // 默认值
    std::string m_comment;       // 注释
};

///////////////
/// 结构体信息
///////////////
struct StructInfo
{
    std::string m_name;                     // 结构体名
    std::string m_extend;                   // 父类名
    std::vector<StructMember> m_members;    // 成员列表
    std::string m_comment;                  // 结构体注释
    std::string m_namespace;                // 所属命名空间
};

///////////////
/// 主题绑定信息
///////////////
struct TopicInfo
{
    std::string m_name;       // 主题名
    std::string m_structFullName; // 对应结构体全名
};

#endif // LIDL_PARSER_STRUCTS_H
