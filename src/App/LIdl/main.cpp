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
#include <clocale>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "LIdlGenerator.h"
#include "LIdlParser.h"

namespace LDdsFramework {

#ifdef _WIN32
bool isConsoleHandle(const DWORD stdHandleId)
{
    const HANDLE handle = GetStdHandle(stdHandleId);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD mode = 0;
    return GetConsoleMode(handle, &mode) != 0;
}

class ConsoleEncodingGuard
{
public:
    ConsoleEncodingGuard()
        : oldOutputCodePage_(0U)
        , oldInputCodePage_(0U)
        , outputCodePageChanged_(false)
        , inputCodePageChanged_(false)
    {
        const char* const currentLocale = std::setlocale(LC_ALL, nullptr);
        if (currentLocale != nullptr)
        {
            oldLocale_ = currentLocale;
        }

        if (isConsoleHandle(STD_OUTPUT_HANDLE) || isConsoleHandle(STD_ERROR_HANDLE))
        {
            oldOutputCodePage_ = GetConsoleOutputCP();
            if (oldOutputCodePage_ != 0U && oldOutputCodePage_ != CP_UTF8)
            {
                outputCodePageChanged_ = (SetConsoleOutputCP(CP_UTF8) != 0);
            }
        }

        if (isConsoleHandle(STD_INPUT_HANDLE))
        {
            oldInputCodePage_ = GetConsoleCP();
            if (oldInputCodePage_ != 0U && oldInputCodePage_ != CP_UTF8)
            {
                inputCodePageChanged_ = (SetConsoleCP(CP_UTF8) != 0);
            }
        }

        if (std::setlocale(LC_ALL, ".UTF-8") == nullptr)
        {
            // Fallback to user-preferred locale if UTF-8 locale is unavailable.
            std::setlocale(LC_ALL, "");
        }
    }

    ~ConsoleEncodingGuard()
    {
        if (outputCodePageChanged_ && oldOutputCodePage_ != 0U)
        {
            SetConsoleOutputCP(oldOutputCodePage_);
        }
        if (inputCodePageChanged_ && oldInputCodePage_ != 0U)
        {
            SetConsoleCP(oldInputCodePage_);
        }
        if (!oldLocale_.empty())
        {
            std::setlocale(LC_ALL, oldLocale_.c_str());
        }
    }

private:
    UINT oldOutputCodePage_;
    UINT oldInputCodePage_;
    bool outputCodePageChanged_;
    bool inputCodePageChanged_;
    std::string oldLocale_;
};
#endif

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
    std::cout << "lidl - 轻量级 DDS IDL 编译器" << std::endl;
    std::cout << "版本 1.0.0" << std::endl;
    std::cout << std::endl;
    std::cout << "版权 (c) 2024 LDdsFramework" << std::endl;
}

/**
 * @brief 显示帮助信息。
 */
void showHelp(const char* programName)
{
    std::cout << "用法: " << programName << " [options] <input-files...>" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -h, --help                 显示帮助信息" << std::endl;
    std::cout << "  -v, --version              显示版本信息" << std::endl;
    std::cout << "  -o, --output <dir>         设置输出目录 (默认: ./generated)" << std::endl;
    std::cout << "  -l, --language <lang>      设置目标语言 (默认: cpp)" << std::endl;
    std::cout << "  -I, --include <path>       添加 include 路径" << std::endl;
    std::cout << "  -s, --strict               启用严格模式" << std::endl;
    std::cout << "  -V, --verbose              启用详细输出" << std::endl;
    std::cout << std::endl;
    std::cout << "支持语言:" << std::endl;
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
#ifdef _WIN32
    const ConsoleEncodingGuard consoleEncodingGuard;
#endif

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
            std::cout << "正在处理: " << inputFile << std::endl;
        }

        LIdlParser parser(parseOptions);
        ParseResult parseResult = parser.parse(inputFile);

        if (!parseResult.success)
        {
            std::cerr << "错误: 解析失败 " << inputFile << std::endl;
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
            std::cerr << "错误: 代码生成失败 " << inputFile << std::endl;
            allSuccess = false;
            continue;
        }

        if (options.verbose)
        {
            std::cout << "生成完成: " << genResult.outputPath
                      << " (共 " << genResult.linesGenerated << " 行)" << std::endl;
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
