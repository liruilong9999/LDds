#ifndef LDDSFRAMEWORK_LIDLGENERATOR_H_
#define LDDSFRAMEWORK_LIDLGENERATOR_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LDds_Global.h"

namespace LDdsFramework {

struct AstNode;
struct ParseResult;

enum class TargetLanguage : uint32_t
{
    Cpp = 0,
    CSharp = 1,
    Java = 2,
    Python = 3,
    Go = 4,
    Rust = 5,
    TypeScript = 6,
    Custom = 255
};

struct GeneratorOptions
{
    bool generateComments;
    bool generateSerialization;
    bool generateDeserialization;
    bool generateConstructors;
    bool generateOperators;
    bool useNamespace;
    bool usePragmaOnce;
    std::string indentStyle;
    uint32_t indentSize;
    std::string namespaceName;
    std::string outputExtension;
    std::string lddsRoot;
    std::string installRoot;

    GeneratorOptions() noexcept
        : generateComments(true)
        , generateSerialization(true)
        , generateDeserialization(true)
        , generateConstructors(true)
        , generateOperators(true)
        , useNamespace(true)
        , usePragmaOnce(true)
        , indentStyle(" ")
        , indentSize(4)
        , namespaceName("Generated")
        , outputExtension(".gen.h")
        , lddsRoot()
        , installRoot()
    {
    }
};

struct GenerationResult
{
    bool success;
    std::string outputPath;
    std::string generatedCode;
    std::vector<std::string> messages;
    double generationTimeMs;
    size_t linesGenerated;

    GenerationResult() noexcept
        : success(false)
        , generationTimeMs(0.0)
        , linesGenerated(0)
    {
    }

    bool hasMessages() const noexcept
    {
        return !messages.empty();
    }
};

using GenerationProgressCallback = std::function<void(
    const std::string & currentFile,
    uint32_t currentItem,
    uint32_t totalItems,
    const std::string & message
)>;

class LDDSCORE_EXPORT LIdlGenerator final
{
public:
    LIdlGenerator();
    explicit LIdlGenerator(TargetLanguage target);
    ~LIdlGenerator() noexcept;

    LIdlGenerator(const LIdlGenerator & other) = delete;
    LIdlGenerator & operator=(const LIdlGenerator & other) = delete;

    LIdlGenerator(LIdlGenerator && other) noexcept;
    LIdlGenerator & operator=(LIdlGenerator && other) noexcept;

    void setTargetLanguage(TargetLanguage target);
    TargetLanguage getTargetLanguage() const noexcept;

    void setOptions(const GeneratorOptions & options);
    const GeneratorOptions & getOptions() const noexcept;

    void setProgressCallback(const GenerationProgressCallback & callback);

    GenerationResult generate(const ParseResult & parseResult, const std::string & outputPath);

    GenerationResult generateFromAst(
        const std::shared_ptr<AstNode> & astRoot,
        const std::string & outputPath
    );

    std::vector<GenerationResult> generateBatch(
        const std::vector<ParseResult> & parseResults,
        const std::string & outputDirectory
    );

    std::vector<std::string> getFileExtensions() const;

    static bool isLanguageSupported(TargetLanguage language) noexcept;
    static std::vector<TargetLanguage> getSupportedLanguages();

private:
    TargetLanguage m_target;
    GeneratorOptions m_options;
    GenerationProgressCallback m_callback;
};

} // namespace LDdsFramework

#endif // LDDSFRAMEWORK_LIDLGENERATOR_H_
