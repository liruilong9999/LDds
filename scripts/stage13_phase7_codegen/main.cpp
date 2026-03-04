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
        std::cerr << "[stage13_codegen] FAIL: " << message << "\n";
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
    ok &= check(parseResult.success, "parseString should succeed");

    auto ast = std::dynamic_pointer_cast<LIdlFile>(parseResult.astRoot);
    ok &= check(ast != nullptr, "AST should be LIdlFile");
    if (!ast)
    {
        std::cout << "[stage13_codegen] result=fail\n";
        return 1;
    }

    ok &= check(ast->enums.size() == 1, "enum count should be 1");
    ok &= check(ast->unions.size() == 1, "union count should be 1");
    ok &= check(ast->structs.size() == 1, "struct count should be 1");

    if (!ast->structs.empty())
    {
        const auto & st = ast->structs.front();
        ok &= check(st.fields.size() == 3, "SensorFrame should have 3 fields");
        if (st.fields.size() >= 2)
        {
            ok &= check(st.fields[1].isSequence, "samples should be sequence");
            ok &= check(st.fields[1].sequenceElementType == "int32", "samples element type should be int32");
            ok &= check(st.fields[1].sequenceBound == 8, "samples bound should be 8");
        }
    }

    LIdlGenerator cppGenerator(TargetLanguage::Cpp);
    GenerationResult cppResult = cppGenerator.generate(parseResult, outputDir.string());
    ok &= check(cppResult.success, "C++ generation should succeed");

    LIdlGenerator pyGenerator(TargetLanguage::Python);
    GenerationResult pyResult = pyGenerator.generate(parseResult, outputDir.string());
    ok &= check(pyResult.success, "Python generation should succeed");

    const std::filesystem::path definePath = outputDir / "phase7_sample_define.h";
    const std::filesystem::path topicPath = outputDir / "phase7_sample_topic.h";
    const std::filesystem::path pythonPath = outputDir / "phase7_sample_types.py";

    ok &= check(std::filesystem::exists(definePath), "generated C++ define header should exist");
    ok &= check(std::filesystem::exists(topicPath), "generated C++ topic header should exist");
    ok &= check(std::filesystem::exists(pythonPath), "generated Python file should exist");

    const std::string pythonText = readTextFile(pythonPath);
    ok &= check(pythonText.find("class SensorFrame") != std::string::npos, "python should contain SensorFrame class");
    ok &= check(pythonText.find("def serialize") != std::string::npos, "python should contain serialize");
    ok &= check(pythonText.find("TOPICS") != std::string::npos, "python should contain TOPICS constants");

    std::cout << "[stage13_codegen] output_dir=" << outputDir.string() << "\n";
    std::cout << "[stage13_codegen] result=" << (ok ? "ok" : "fail") << "\n";
    return ok ? 0 : 1;
}
