#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "LIdlGenerator.h"
#include "LIdlParser.h"

using namespace LDdsFramework;

int main(int argc, char * argv[])
{
    const std::string outputDir =
        (argc > 1) ? argv[1] : "build/app_example_idl_pipeline";

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    const std::string idlText =
        "package Demo {\n"
        "  enum Color { Red = 1, Green, Blue = 7 }\n"
        "  struct Pose {\n"
        "    int32 id;\n"
        "    sequence<float, 8> samples;\n"
        "  }\n"
        "  union Value switch(int32) {\n"
        "    case 0: int32 number;\n"
        "    default: string text;\n"
        "  }\n"
        "}\n"
        "POSE_TOPIC = Demo::Pose;\n"
        "VALUE_TOPIC = Demo::Value;\n";

    LIdlParser parser;
    ParseResult parseResult = parser.parseString(idlText, "example_pipeline.lidl");
    if (!parseResult.success)
    {
        std::cerr << "[example_idl_pipeline] parse failed\n";
        for (const auto & error : parseResult.errors)
        {
            std::cerr << "  [error] " << error.filePath
                      << ":" << error.line
                      << ":" << error.column
                      << " " << error.message << "\n";
        }
        return EXIT_FAILURE;
    }

    LIdlGenerator cppGenerator(TargetLanguage::Cpp);
    GenerationResult cppResult = cppGenerator.generate(parseResult, outputDir);
    if (!cppResult.success)
    {
        std::cerr << "[example_idl_pipeline] cpp generate failed\n";
        return EXIT_FAILURE;
    }

    LIdlGenerator pythonGenerator(TargetLanguage::Python);
    GenerationResult pyResult = pythonGenerator.generate(parseResult, outputDir);
    if (!pyResult.success)
    {
        std::cerr << "[example_idl_pipeline] python generate failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "[example_idl_pipeline] result=ok"
              << " outputDir=" << outputDir << "\n";
    return EXIT_SUCCESS;
}
