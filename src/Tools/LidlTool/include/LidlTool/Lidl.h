#pragma once

#include <string>

namespace LidlTool {
        
class Lidl {
public:
    // Parse an input .lidl file and generate output files to outDir.
    static bool generate(const std::string& inputFile, const std::string& outDir);
};

} // namespace LidlTool
