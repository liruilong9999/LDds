#ifndef CODE_GENERATOR_H
#define CODE_GENERATOR_H

#include "IdlParser.h"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include "LidL_global.h"

///////////////
/// CodeGenerator 类
/// 代码生成器
/// by:李瑞龙
///////////////
class LIDL_EXPORT CodeGenerator
{
private:
    std::string m_outputDir;   // 输出目录

public:
    ///////////////
    /// 构造函数
    /// outputDir 输出目录
    /// by:李瑞龙
    ///////////////
    explicit CodeGenerator(const std::string& outputDir = ".");

    ///////////////
    /// 生成代码文件
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void generateCode(std::shared_ptr<IdlFile> idlFile);

private:
    ///////////////
    /// 生成定义头文件
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void generateDefineHeader(std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 生成导出头文件
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void generateExportHeader(std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 生成主题头文件
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void generateTopicHeader(std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 生成主题实现文件
    /// idlFile IDL文件对象
    /// by:李瑞龙
    ///////////////
    void generateTopicCpp(std::shared_ptr<IdlFile> idlFile);

    ///////////////
    /// 获取文件名前缀
    /// fileName 文件名
    /// return 文件名前缀
    /// by:李瑞龙
    ///////////////
    std::string getFilePrefix(const std::string& fileName) const;

    ///////////////
    /// 转换IDL类型到C++类型
    /// idlType IDL类型
    /// return C++类型
    /// by:李瑞龙
    ///////////////
    std::string convertTypeToCpp(const std::string& idlType) const;

    ///////////////
    /// 生成命名空间开始
    /// namespaces 命名空间列表
    /// oss 输出流
    /// by:李瑞龙
    ///////////////
    void generateNamespaceBegin(const std::vector<std::string>& namespaces, std::ostringstream& oss);

    ///////////////
    /// 生成命名空间结束
    /// namespaces 命名空间列表
    /// oss 输出流
    /// by:李瑞龙
    ///////////////
    void generateNamespaceEnd(const std::vector<std::string>& namespaces, std::ostringstream& oss);

    ///////////////
    /// 生成字段默认值
    /// field 字段
    /// return 默认值代码
    /// by:李瑞龙
    ///////////////
    std::string generateFieldDefaultValue(const Field& field) const;

    ///////////////
    /// 生成结构体代码
    /// struc 结构体
    /// oss 输出流
    /// by:李瑞龙
    ///////////////
    void generateStructCode(const Struct& struc, std::ostringstream& oss);

    ///////////////
    /// 获取主题枚举名称
    /// topic 主题
    /// return 枚举名称
    /// by:李瑞龙
    ///////////////
    std::string getTopicEnumName(const Topic& topic) const;

    ///////////////
    /// 写入文件
    /// fileName 文件名
    /// content 内容
    /// by:李瑞龙
    ///////////////
    void writeFile(const std::string& fileName, const std::string& content);

    ///////////////
    /// 确保输出目录存在
    /// by:李瑞龙
    ///////////////
    void ensureOutputDir() const;
};

#endif // CODE_GENERATOR_H