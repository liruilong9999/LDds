#include <LIdl/IdlParser.h>
#include <LIdl/CodeGenerator.h>
#include <iostream>
#include <windows.h>


using namespace std;

///////////////
/// 主函数
/// argc 参数个数
/// argv 参数数组
/// return 程序退出码
/// by:李瑞龙
///////////////
int main(int argc, char* argv[])
{
    // 在main函数开头设置控制台输出编码为UTF-8
    SetConsoleOutputCP(CP_UTF8);
    system("pause");
    if (argc < 2)
    {
        cout << "用法: " << argv[0] << " <idl文件> [输出目录]" << endl;
        return 1;
    }

    string idlFile = argv[1];
    string outputDir = argc > 2 ? argv[2] : ".";

    IdlParser parser;
    CodeGenerator generator(outputDir);

    auto result = parser.parseFile(idlFile);
    if (result)
    {
        generator.generateCode(result);
        cout << "IDL文件解析完成!" << endl;
    }
    else
    {
        cerr << "IDL文件解析失败!" << endl;
        return 1;
    }

    return 0;
}