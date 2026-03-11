#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "LIdlGenerator.h"
#include "LIdlParser.h"

namespace LDdsFramework {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
bool isConsoleHandle(DWORD stdHandleId)
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
        const char * currentLocale = std::setlocale(LC_ALL, nullptr);
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

struct CommandLineOptions
{
    std::vector<std::string> inputFiles;
    std::string outputDirectory;
    std::string installDirectory;
    TargetLanguage targetLanguage;
    bool showHelp;
    bool showVersion;
    bool verbose;
    bool strictMode;
    std::vector<std::string> includePaths;
    std::string buildPlatform;

    CommandLineOptions() noexcept
        : outputDirectory()
        , installDirectory()
        , targetLanguage(TargetLanguage::Cpp)
        , showHelp(false)
        , showVersion(false)
        , verbose(false)
        , strictMode(false)
        , includePaths()
#ifdef _WIN32
        , buildPlatform("x64")
#else
        , buildPlatform()
#endif
    {
    }
};

void showVersion()
{
    std::cout << "lidl 1.0.0" << std::endl;
}

void showHelp(const char * programName)
{
    std::cout << "Usage: " << programName << " [options] <input-files...>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help                 Show help" << std::endl;
    std::cout << "  -v, --version              Show version" << std::endl;
    std::cout << "  -o, --output <dir>         Generation root, default: <repo>/bin/generate" << std::endl;
    std::cout << "      --install <dir>        Install root, default: parent of output root" << std::endl;
    std::cout << "  -l, --language <lang>      Target language, default: cpp" << std::endl;
    std::cout << "  -I, --include <path>       Extra include path" << std::endl;
    std::cout << "  -s, --strict               Enable strict parse mode" << std::endl;
    std::cout << "  -V, --verbose              Print extra logs" << std::endl;
#ifdef _WIN32
    std::cout << "      --platform <name>      Visual Studio platform, default: x64" << std::endl;
#endif
    std::cout << std::endl;
    std::cout << "Default layout:" << std::endl;
    std::cout << "  <output>/file2              generated project and sources" << std::endl;
    std::cout << "  <install>/include/file2     installed headers" << std::endl;
    std::cout << "  <install>/lib               file2.lib and file2d.lib" << std::endl;
    std::cout << "  <install>                   file2.dll and file2d.dll" << std::endl;
    std::cout << std::endl;
    std::cout << "For C++ generation, lidl recursively generates include dependencies and builds both Debug and Release." << std::endl;
}

TargetLanguage parseLanguage(const std::string & lang)
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

CommandLineOptions parseCommandLine(int argc, char * argv[])
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
        else if (arg == "--install" && i + 1 < argc)
        {
            options.installDirectory = argv[++i];
        }
        else if ((arg == "-l" || arg == "--language") && i + 1 < argc)
        {
            options.targetLanguage = parseLanguage(argv[++i]);
        }
        else if ((arg == "-I" || arg == "--include") && i + 1 < argc)
        {
            options.includePaths.push_back(argv[++i]);
        }
#ifdef _WIN32
        else if (arg == "--platform" && i + 1 < argc)
        {
            options.buildPlatform = argv[++i];
        }
#endif
        else if (!arg.empty() && arg[0] != '-')
        {
            options.inputFiles.push_back(arg);
        }
    }

    return options;
}

std::string quoteArg(const std::string & value)
{
    std::string escaped = "\"";
    for (const char ch : value)
    {
        if (ch == '"')
        {
            escaped += "\\\"";
        }
        else
        {
            escaped.push_back(ch);
        }
    }
    escaped += "\"";
    return escaped;
}

fs::path resolveExecutablePath()
{
#ifdef _WIN32
    std::vector<char> buffer(static_cast<size_t>(MAX_PATH), '\0');
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length >= buffer.size())
    {
        buffer.resize(buffer.size() * 2U, '\0');
        length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length > 0)
    {
        return fs::path(std::string(buffer.data(), length));
    }
#endif
    return fs::current_path() / "LIdl";
}

fs::path resolveRepoRoot()
{
    const fs::path executablePath = resolveExecutablePath();
    if (!executablePath.empty())
    {
        return executablePath.parent_path().parent_path();
    }
    return fs::current_path();
}

fs::path normalizePath(const fs::path & pathValue)
{
    std::error_code ec;
    const fs::path absolutePath = fs::absolute(pathValue, ec);
    if (!ec)
    {
        const fs::path canonicalPath = fs::weakly_canonical(absolutePath, ec);
        if (!ec)
        {
            return canonicalPath;
        }
        return absolutePath.lexically_normal();
    }
    return pathValue.lexically_normal();
}

void printParseErrors(const std::string & filePath, const ParseResult & parseResult)
{
    std::cerr << "Parse failed: " << filePath << std::endl;
    for (const auto & error : parseResult.errors)
    {
        std::cerr << "  [" << static_cast<int>(error.level) << "] "
                  << error.filePath << ":" << error.line
                  << ":" << error.column << " - "
                  << error.message << std::endl;
    }
}

int runCommandWithLogs(const std::string & title, const std::string & command)
{
    std::cout << title << std::endl;
    std::cout << "  " << command << std::endl;
    return std::system(command.c_str());
}

bool buildGeneratedCppModule(
    const fs::path & moduleOutputDir,
    const fs::path & repoRoot,
    const fs::path & installRoot,
    const CommandLineOptions & options,
    std::string & errorMessage)
{
    const fs::path buildDir = moduleOutputDir / "build";

    std::ostringstream configureCommand;
    configureCommand << "cmake -S " << quoteArg(moduleOutputDir.string())
                     << " -B " << quoteArg(buildDir.string())
                     << " -DLDDS_ROOT=" << quoteArg(repoRoot.string())
                     << " -DLDDS_INSTALL_ROOT=" << quoteArg(installRoot.string());
#ifdef _WIN32
    if (!options.buildPlatform.empty())
    {
        configureCommand << " -A " << options.buildPlatform;
    }
#endif

    if (runCommandWithLogs("Configure generated project:", configureCommand.str()) != 0)
    {
        errorMessage = "cmake configure failed";
        return false;
    }

    const std::vector<std::string> configs = {"Debug", "Release"};
    for (const std::string & config : configs)
    {
        std::ostringstream buildCommand;
        buildCommand << "cmake --build " << quoteArg(buildDir.string()) << " --config " << config;
        if (runCommandWithLogs("Build generated project (" + config + "):", buildCommand.str()) != 0)
        {
            errorMessage = "cmake build failed for " + config;
            return false;
        }
    }

    return true;
}

bool ensureCoreBuilt(
    const fs::path & repoRoot,
    const CommandLineOptions & options,
    const std::string & config,
    std::string & errorMessage)
{
    static std::unordered_set<std::string> builtConfigs;
    if (builtConfigs.find(config) != builtConfigs.end())
    {
        return true;
    }

    const fs::path repoBuildDir = repoRoot / "build";

    std::ostringstream configureCommand;
    configureCommand << "cmake -S " << quoteArg(repoRoot.string())
                     << " -B " << quoteArg(repoBuildDir.string());
#ifdef _WIN32
    if (!options.buildPlatform.empty())
    {
        configureCommand << " -A " << options.buildPlatform;
    }
#endif

    if (runCommandWithLogs("Configure LDdsCore workspace:", configureCommand.str()) != 0)
    {
        errorMessage = "workspace configure failed";
        return false;
    }

    std::ostringstream buildCommand;
    buildCommand << "cmake --build " << quoteArg(repoBuildDir.string())
                 << " --config " << config
                 << " --target LDdsCore";
    if (runCommandWithLogs("Build LDdsCore (" + config + "):", buildCommand.str()) != 0)
    {
        errorMessage = "LDdsCore build failed for " + config;
        return false;
    }

    builtConfigs.insert(config);
    return true;
}

bool processModuleRecursive(
    const fs::path & inputFile,
    const fs::path & repoRoot,
    const CommandLineOptions & options,
    const ParseOptions & parseOptions,
    const GeneratorOptions & generatorOptions,
    std::unordered_set<std::string> & processedModules)
{
    const fs::path normalizedInput = normalizePath(inputFile);
    const std::string normalizedKey = normalizedInput.string();
    if (!processedModules.insert(normalizedKey).second)
    {
        return true;
    }

    std::cout << "Parse file: " << normalizedKey << std::endl;
    LIdlParser parser(parseOptions);
    ParseResult parseResult = parser.parse(normalizedKey);
    if (!parseResult.success)
    {
        printParseErrors(normalizedKey, parseResult);
        return false;
    }

    const std::shared_ptr<LIdlFile> idlFile = std::dynamic_pointer_cast<LIdlFile>(parseResult.astRoot);
    if (!idlFile)
    {
        std::cerr << "Parse result is not LIdlFile: " << normalizedKey << std::endl;
        return false;
    }

    for (const auto & includeFile : idlFile->includeFiles)
    {
        if (!processModuleRecursive(includeFile, repoRoot, options, parseOptions, generatorOptions, processedModules))
        {
            return false;
        }
    }

    LIdlGenerator generator(options.targetLanguage);
    generator.setOptions(generatorOptions);
    generator.setProgressCallback(
        [](const std::string & currentFile, uint32_t currentItem, uint32_t totalItems, const std::string & message) {
            std::cout << "  [" << currentItem << "/" << totalItems << "] "
                      << message << ": " << currentFile << std::endl;
        });

    const fs::path outputPath = fs::path(options.outputDirectory) / normalizedInput.stem();
    std::cout << "Generate code: " << normalizedKey << " -> " << outputPath.string() << std::endl;
    GenerationResult generationResult = generator.generate(parseResult, outputPath.string());
    if (!generationResult.success)
    {
        std::cerr << "Code generation failed: " << normalizedKey << std::endl;
        for (const auto & message : generationResult.messages)
        {
            std::cerr << "  - " << message << std::endl;
        }
        return false;
    }

    if (options.verbose)
    {
        std::cout << "Generated: " << generationResult.outputPath
                  << " (" << generationResult.linesGenerated
                  << " lines, " << generationResult.generationTimeMs << " ms)" << std::endl;
    }

    if (options.targetLanguage == TargetLanguage::Cpp)
    {
        for (const std::string & config : {"Debug", "Release"})
        {
            std::string coreBuildError;
            if (!ensureCoreBuilt(repoRoot, options, config, coreBuildError))
            {
                std::cerr << "Build failed: LDdsCore - " << coreBuildError << std::endl;
                return false;
            }
        }

        std::string buildError;
        const fs::path installRoot = normalizePath(options.installDirectory);
        if (!buildGeneratedCppModule(outputPath, repoRoot, installRoot, options, buildError))
        {
            std::cerr << "Build failed: " << outputPath.string() << " - " << buildError << std::endl;
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char * argv[])
{
#ifdef _WIN32
    const ConsoleEncodingGuard consoleEncodingGuard;
#endif

    CommandLineOptions options = parseCommandLine(argc, argv);

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

    const fs::path repoRoot = resolveRepoRoot();
    if (options.outputDirectory.empty())
    {
        options.outputDirectory = (repoRoot / "bin" / "generate").string();
    }
    if (options.installDirectory.empty())
    {
        options.installDirectory = normalizePath(fs::path(options.outputDirectory).parent_path()).string();
    }

    ParseOptions parseOptions;
    parseOptions.strictMode = options.strictMode;
    parseOptions.includePaths = options.includePaths;

    GeneratorOptions generatorOptions;
    generatorOptions.generateComments = true;
    generatorOptions.generateSerialization = true;
    generatorOptions.lddsRoot = repoRoot.string();
    generatorOptions.installRoot = normalizePath(options.installDirectory).string();

    std::unordered_set<std::string> processedModules;
    for (const auto & inputFile : options.inputFiles)
    {
        if (!processModuleRecursive(inputFile, repoRoot, options, parseOptions, generatorOptions, processedModules))
        {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

} // namespace LDdsFramework

int main(int argc, char * argv[])
{
    return LDdsFramework::main(argc, argv);
}
