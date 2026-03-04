/**
 * @file main.cpp
 * @brief `lidl` 命令行工具入口。
 *
 * 功能：
 * - 解析 `.lidl` 文件并构建 AST
 * - 根据目标语言生成代码（当前重点为 C++）
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "LIdlGenerator.h"
#include "LIdlParser.h"

namespace LDdsFramework {

/**
 * @brief 命令行参数。
 */
struct CommandLineOptions
{
    std::vector<std::string> inputFiles;      ///< 输入 IDL 文件列表
    std::string              outputDirectory; ///< 输出目录
    TargetLanguage           targetLanguage;  ///< 目标语言
    bool                     showHelp;        ///< 是否显示帮助
    bool                     showVersion;     ///< 是否显示版本
    bool                     verbose;         ///< 是否启用详细日志
    bool                     strictMode;      ///< 是否启用严格模式
    std::vector<std::string> includePaths;    ///< include 搜索路径

    /**
     * @brief 默认构造。
     */
    CommandLineOptions() noexcept
        : outputDirectory("./generated")
        , targetLanguage(TargetLanguage::Cpp)
        , showHelp(false)
        , showVersion(false)
        , verbose(false)
        , strictMode(false)
    {
    }
};

/**
 * @brief 显示版本信息。
 */
void showVersion()
{
    std::cout << "lidl - Lightweight DDS IDL Compiler" << std::endl;
    std::cout << "Version 1.0.0" << std::endl;
    std::cout << std::endl;
    std::cout << "Copyright (c) 2024 LDdsFramework" << std::endl;
}

/**
 * @brief 显示帮助信息。
 */
void showHelp(const char* programName)
{
    std::cout << "Usage: " << programName << " [options] <input-files...>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl;
    std::cout << "  -v, --version              Show version information" << std::endl;
    std::cout << "  -o, --output <dir>         Set output directory (default: ./generated)" << std::endl;
    std::cout << "  -l, --language <lang>      Set target language (default: cpp)" << std::endl;
    std::cout << "  -I, --include <path>       Add include path" << std::endl;
    std::cout << "  -s, --strict               Enable strict mode" << std::endl;
    std::cout << "  -V, --verbose              Enable verbose output" << std::endl;
    std::cout << std::endl;
    std::cout << "Supported languages:" << std::endl;
    std::cout << "  cpp, csharp, java, python, go, rust, typescript" << std::endl;
}

/**
 * @brief 解析目标语言参数。
 */
TargetLanguage parseLanguage(const std::string& lang)
{
    if (lang == "cpp" || lang == "c++")
    {
        return TargetLanguage::Cpp;
    }
    if (lang == "csharp" || lang == "c#")
    {
        return TargetLanguage::CSharp;
    }
    if (lang == "java")
    {
        return TargetLanguage::Java;
    }
    if (lang == "python" || lang == "py")
    {
        return TargetLanguage::Python;
    }
    if (lang == "go" || lang == "golang")
    {
        return TargetLanguage::Go;
    }
    if (lang == "rust" || lang == "rs")
    {
        return TargetLanguage::Rust;
    }
    if (lang == "typescript" || lang == "ts")
    {
        return TargetLanguage::TypeScript;
    }
    return TargetLanguage::Cpp;
}

/**
 * @brief 解析命令行参数。
 */
CommandLineOptions parseCommandLine(int argc, char* argv[])
{
    CommandLineOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            options.showHelp = true;
        }
        else if (arg == "-v" || arg == "--version")
        {
            options.showVersion = true;
        }
        else if (arg == "-V" || arg == "--verbose")
        {
            options.verbose = true;
        }
        else if (arg == "-s" || arg == "--strict")
        {
            options.strictMode = true;
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc)
        {
            options.outputDirectory = argv[++i];
        }
        else if ((arg == "-l" || arg == "--language") && i + 1 < argc)
        {
            options.targetLanguage = parseLanguage(argv[++i]);
        }
        else if ((arg == "-I" || arg == "--include") && i + 1 < argc)
        {
            options.includePaths.push_back(argv[++i]);
        }
        else if (!arg.empty() && arg[0] != '-')
        {
            options.inputFiles.push_back(arg);
        }
    }

    return options;
}

/**
 * @brief `lidl` 主函数实现。
 */
int main(int argc, char* argv[])
{
    const CommandLineOptions options = parseCommandLine(argc, argv);

    if (options.showVersion)
    {
        showVersion();
        return EXIT_SUCCESS;
    }

    if (options.showHelp || options.inputFiles.empty())
    {
        showHelp(argv[0]);
        return options.inputFiles.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    ParseOptions parseOptions;
    parseOptions.strictMode = options.strictMode;
    parseOptions.includePaths = options.includePaths;

    GeneratorOptions generatorOptions;
    generatorOptions.generateComments = true;
    generatorOptions.generateSerialization = true;

    bool allSuccess = true;

    for (const auto& inputFile : options.inputFiles)
    {
        if (options.verbose)
        {
            std::cout << "Processing: " << inputFile << std::endl;
        }

        LIdlParser parser(parseOptions);
        ParseResult parseResult = parser.parse(inputFile);

        if (!parseResult.success)
        {
            std::cerr << "Error: Failed to parse " << inputFile << std::endl;
            for (const auto& error : parseResult.errors)
            {
                std::cerr << "  [" << static_cast<int>(error.level) << "] "
                          << error.filePath << ":" << error.line
                          << ":" << error.column << " - "
                          << error.message << std::endl;
            }
            allSuccess = false;
            continue;
        }

        LIdlGenerator generator(options.targetLanguage);
        generator.setOptions(generatorOptions);

        const std::string outputPath = options.outputDirectory + "/" + inputFile + ".gen";
        GenerationResult genResult = generator.generate(parseResult, outputPath);

        if (!genResult.success)
        {
            std::cerr << "Error: Failed to generate code for " << inputFile << std::endl;
            allSuccess = false;
            continue;
        }

        if (options.verbose)
        {
            std::cout << "Generated: " << genResult.outputPath
                      << " (" << genResult.linesGenerated << " lines)" << std::endl;
        }
    }

    return allSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace LDdsFramework

/**
 * @brief 进程入口。
 */
int main(int argc, char* argv[])
{
    return LDdsFramework::main(argc, argv);
}
