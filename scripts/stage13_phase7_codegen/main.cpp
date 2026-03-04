#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "LIdlGenerator.h"
#include "LIdlParser.h"

using namespace LDdsFramework;

namespace {

bool check(bool condition, const std::string & message)
{
    if (!condition)
    {
        std::cerr << "[stage13_codegen] FAIL(失败): " << message << "\n";
        return false;
    }
    return true;
}

std::string readTextFile(const std::filesystem::path & path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char ** argv)
{
    const std::filesystem::path outputDir =
        (argc > 1) ? std::filesystem::path(argv[1]) : std::filesystem::path("build/stage13_phase7_smoke/generated");
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    const std::string idlText =
        "package P7 {\n"
        "  enum Color {\n"
        "    Red = 1,\n"
        "    Green,\n"
        "    Blue = 7\n"
        "  }\n"
        "\n"
        "  struct SensorFrame {\n"
        "    Color color;\n"
        "    sequence<int32, 8> samples;\n"
        "    sequence<string> tags;\n"
        "  }\n"
        "\n"
        "  union Value switch(int32) {\n"
        "    case 0: int32 i32;\n"
        "    case 1: string text;\n"
        "    default: sequence<uint16> raw;\n"
        "  }\n"
        "}\n"
        "\n"
        "FRAME_TOPIC = P7::SensorFrame;\n"
        "VALUE_TOPIC = P7::Value;\n";

    bool ok = true;

    LIdlParser parser;
    ParseResult parseResult = parser.parseString(idlText, "phase7_sample.lidl");
    ok &= check(parseResult.success, "parseString 应成功");

    auto ast = std::dynamic_pointer_cast<LIdlFile>(parseResult.astRoot);
    ok &= check(ast != nullptr, "AST 应为 LIdlFile");
    if (!ast)
    {
        std::cout << "[stage13_codegen] result=fail 说明=失败\n";
        return 1;
    }

    ok &= check(ast->enums.size() == 1, "enum 数量应为 1");
    ok &= check(ast->unions.size() == 1, "union 数量应为 1");
    ok &= check(ast->structs.size() == 1, "struct 数量应为 1");

    if (!ast->structs.empty())
    {
        const auto & st = ast->structs.front();
        ok &= check(st.fields.size() == 3, "SensorFrame 字段数应为 3");
        if (st.fields.size() >= 2)
        {
            ok &= check(st.fields[1].isSequence, "samples 应为 sequence");
            ok &= check(st.fields[1].sequenceElementType == "int32", "samples 元素类型应为 int32");
            ok &= check(st.fields[1].sequenceBound == 8, "samples 上界应为 8");
        }
    }

    LIdlGenerator cppGenerator(TargetLanguage::Cpp);
    GenerationResult cppResult = cppGenerator.generate(parseResult, outputDir.string());
    ok &= check(cppResult.success, "C++ 代码生成应成功");

    LIdlGenerator pyGenerator(TargetLanguage::Python);
    GenerationResult pyResult = pyGenerator.generate(parseResult, outputDir.string());
    ok &= check(pyResult.success, "Python 代码生成应成功");

    const std::filesystem::path definePath = outputDir / "phase7_sample_define.h";
    const std::filesystem::path topicPath = outputDir / "phase7_sample_topic.h";
    const std::filesystem::path pythonPath = outputDir / "phase7_sample_types.py";

    ok &= check(std::filesystem::exists(definePath), "应生成 C++ define 头文件");
    ok &= check(std::filesystem::exists(topicPath), "应生成 C++ topic 头文件");
    ok &= check(std::filesystem::exists(pythonPath), "应生成 Python 文件");

    const std::string pythonText = readTextFile(pythonPath);
    ok &= check(pythonText.find("class SensorFrame") != std::string::npos, "Python 结果应包含 SensorFrame 类");
    ok &= check(pythonText.find("def serialize") != std::string::npos, "Python 结果应包含 serialize");
    ok &= check(pythonText.find("TOPICS") != std::string::npos, "Python 结果应包含 TOPICS 常量");

    std::cout << "[stage13_codegen] output_dir=" << outputDir.string() << "\n";
    std::cout << "[stage13_codegen] result=" << (ok ? "ok" : "fail")
              << " 说明=" << (ok ? "通过" : "失败") << "\n";
    return ok ? 0 : 1;
}
