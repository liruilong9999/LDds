/**
 * @file main.cpp
 * @brief lidl工具入口点
 *
 * lidl - Lightweight DDS IDL编译器
 * 将IDL文件编译为各种目标语言的数据结构定义
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "LDds/LIdlGenerator.h"
#include "LDds/LIdlParser.h"

namespace LDdsFramework {

/**
 * @brief 命令行选项结构
 */
struct CommandLineOptions
{
    std::vector<std::string> inputFiles;        ///< 输入IDL文件列表
    std::string outputDirectory;                ///< 输出目录
    TargetLanguage targetLanguage;              ///< 目标语言
    bool showHelp;                              ///< 显示帮助
    bool showVersion;                           ///< 显示版本
    bool verbose;                               ///< 详细输出
    bool strictMode;                            ///< 严格模式
    std::vector<std::string> includePaths;      ///< 包含路径

    /**
     * @brief 默认构造函数
     */
    CommandLineOptions() noexcept
        : outputDirectory("./generated")
        , targetLanguage(TargetLanguage::Cpp)
        , showHelp(false)
        , showVersion(false)
        , verbose(false)
        , strictMode(false)
    {}
};

/**
 * @brief 显示版本信息
 */
void showVersion()
{
    std::cout << "lidl - Lightweight DDS IDL Compiler" << std::endl;
    std::cout << "Version 1.0.0" << std::endl;
    std::cout << std::endl;
    std::cout << "Copyright (c) 2024 LDdsFramework" << std::endl;
}

/**
 * @brief 显示帮助信息
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
 * @brief 解析目标语言字符串
 */
TargetLanguage parseLanguage(const std::string& lang)
{
    if (lang == "cpp" || lang == "c++") {
        return TargetLanguage::Cpp;
    } else if (lang == "csharp" || lang == "c#") {
        return TargetLanguage::CSharp;
    } else if (lang == "java") {
        return TargetLanguage::Java;
    } else if (lang == "python" || lang == "py") {
        return TargetLanguage::Python;
    } else if (lang == "go" || lang == "golang") {
        return TargetLanguage::Go;
    } else if (lang == "rust" || lang == "rs") {
        return TargetLanguage::Rust;
    } else if (lang == "typescript" || lang == "ts") {
        return TargetLanguage::TypeScript;
    }
    return TargetLanguage::Cpp;  // 默认C++
}

/**
 * @brief 解析命令行参数
 */
CommandLineOptions parseCommandLine(int argc, char* argv[])
{
    CommandLineOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            options.showHelp = true;
        } else if (arg == "-v" || arg == "--version") {
            options.showVersion = true;
        } else if (arg == "-V" || arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "-s" || arg == "--strict") {
            options.strictMode = true;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            options.outputDirectory = argv[++i];
        } else if ((arg == "-l" || arg == "--language") && i + 1 < argc) {
            options.targetLanguage = parseLanguage(argv[++i]);
        } else if ((arg == "-I" || arg == "--include") && i + 1 < argc) {
            options.includePaths.push_back(argv[++i]);
        } else if (arg[0] != '-') {
            // 非选项参数视为输入文件
            options.inputFiles.push_back(arg);
        }
    }

    return options;
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[])
{
    // 解析命令行参数
    CommandLineOptions options = parseCommandLine(argc, argv);

    // 显示版本信息
    if (options.showVersion) {
        showVersion();
        return EXIT_SUCCESS;
    }

    // 显示帮助信息
    if (options.showHelp || options.inputFiles.empty()) {
        showHelp(argv[0]);
        return options.inputFiles.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    // 配置解析选项
    ParseOptions parseOptions;
    parseOptions.strictMode = options.strictMode;
    parseOptions.includePaths = options.includePaths;

    // 配置生成选项
    GeneratorOptions generatorOptions;
    generatorOptions.generateComments = true;
    generatorOptions.generateSerialization = true;

    // 处理每个输入文件
    bool allSuccess = true;

    for (const auto& inputFile : options.inputFiles) {
        if (options.verbose) {
            std::cout << "Processing: " << inputFile << std::endl;
        }

        // 解析IDL文件
        LIdlParser parser(parseOptions);
        ParseResult parseResult = parser.parse(inputFile);

        if (!parseResult.success) {
            std::cerr << "Error: Failed to parse " << inputFile << std::endl;
            // 输出解析错误
            for (const auto& error : parseResult.errors) {
                std::cerr << "  [" << static_cast<int>(error.level) << "] "
                         << error.filePath << ":" << error.line
                         << ":" << error.column << " - "
                         << error.message << std::endl;
            }
            allSuccess = false;
            continue;
        }

        // 生成代码
        LIdlGenerator generator(options.targetLanguage);
        generator.setOptions(generatorOptions);

        std::string outputPath = options.outputDirectory + "/" + inputFile + ".gen";
        GenerationResult genResult = generator.generate(parseResult, outputPath);

        if (!genResult.success) {
            std::cerr << "Error: Failed to generate code for " << inputFile << std::endl;
            allSuccess = false;
            continue;
        }

        if (options.verbose) {
            std::cout << "Generated: " << genResult.outputPath
                     << " (" << genResult.linesGenerated << " lines)" << std::endl;
        }
    }

    return allSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace LDdsFramework

// 全局main函数入口
int main(int argc, char* argv[])
{
    return LDdsFramework::main(argc, argv);
}
