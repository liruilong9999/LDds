/**
 * @file LIdlGenerator.cpp
 * @brief LIdlGenerator class implementation
 */

#include "LDds/LIdlGenerator.h"

namespace LDdsFramework {

LIdlGenerator::LIdlGenerator()
    : m_target(TargetLanguage::Cpp)
    , m_options()
    , m_callback(nullptr)
{
}

LIdlGenerator::LIdlGenerator(TargetLanguage target)
    : m_target(target)
    , m_options()
    , m_callback(nullptr)
{
}

LIdlGenerator::~LIdlGenerator() noexcept = default;

LIdlGenerator::LIdlGenerator(LIdlGenerator&& other) noexcept
    : m_target(other.m_target)
    , m_options(std::move(other.m_options))
    , m_callback(std::move(other.m_callback))
{
}

LIdlGenerator& LIdlGenerator::operator=(LIdlGenerator&& other) noexcept
{
    if (this != &other) {
        m_target = other.m_target;
        m_options = std::move(other.m_options);
        m_callback = std::move(other.m_callback);
    }
    return *this;
}

void LIdlGenerator::setTargetLanguage(TargetLanguage target)
{
    m_target = target;
}

TargetLanguage LIdlGenerator::getTargetLanguage() const noexcept
{
    return m_target;
}

void LIdlGenerator::setOptions(const GeneratorOptions& options)
{
    m_options = options;
}

const GeneratorOptions& LIdlGenerator::getOptions() const noexcept
{
    return m_options;
}

void LIdlGenerator::setProgressCallback(const GenerationProgressCallback& callback)
{
    m_callback = callback;
}

GenerationResult LIdlGenerator::generate(
    const ParseResult& parseResult,
    const std::string& outputPath)
{
    (void)parseResult;
    (void)outputPath;
    GenerationResult result;
    result.success = false;
    return result;
}

GenerationResult LIdlGenerator::generateFromAst(
    const std::shared_ptr<AstNode>& astRoot,
    const std::string& outputPath)
{
    (void)astRoot;
    (void)outputPath;
    GenerationResult result;
    result.success = false;
    return result;
}

std::vector<GenerationResult> LIdlGenerator::generateBatch(
    const std::vector<ParseResult>& parseResults,
    const std::string& outputDirectory)
{
    (void)parseResults;
    (void)outputDirectory;
    return std::vector<GenerationResult>();
}

std::vector<std::string> LIdlGenerator::getFileExtensions() const
{
    switch (m_target) {
        case TargetLanguage::Cpp:
            return {".h", ".hpp", ".cpp"};
        case TargetLanguage::CSharp:
            return {".cs"};
        case TargetLanguage::Java:
            return {".java"};
        case TargetLanguage::Python:
            return {".py"};
        case TargetLanguage::Go:
            return {".go"};
        case TargetLanguage::Rust:
            return {".rs"};
        case TargetLanguage::TypeScript:
            return {".ts"};
        default:
            return {};
    }
}

bool LIdlGenerator::isLanguageSupported(TargetLanguage language) noexcept
{
    switch (language) {
        case TargetLanguage::Cpp:
        case TargetLanguage::CSharp:
        case TargetLanguage::Java:
        case TargetLanguage::Python:
        case TargetLanguage::Go:
        case TargetLanguage::Rust:
        case TargetLanguage::TypeScript:
            return true;
        default:
            return false;
    }
}

std::vector<TargetLanguage> LIdlGenerator::getSupportedLanguages()
{
    return {
        TargetLanguage::Cpp,
        TargetLanguage::CSharp,
        TargetLanguage::Java,
        TargetLanguage::Python,
        TargetLanguage::Go,
        TargetLanguage::Rust,
        TargetLanguage::TypeScript
    };
}

} // namespace LDdsFramework
