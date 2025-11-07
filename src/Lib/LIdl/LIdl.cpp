#include "LIdl.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <unordered_map>

bool LIdl::generateFromFile(const std::string & lidlFile, const std::string & outDir)
{
    namespace fs = std::filesystem;

    try
    {
        if (!fs::exists(lidlFile))
        {
            std::cerr << "LIdl: input file does not exist: " << lidlFile << "\n";
            return false;
        }

        std::ifstream ifs(lidlFile);
        if (!ifs)
        {
            std::cerr << "LIdl: failed to open input file: " << lidlFile << "\n";
            return false;
        }

        std::ostringstream buf;
        buf << ifs.rdbuf();
        std::string content = buf.str();

        // Find structs of form: struct Name { ... };
        std::regex struct_re(R"(struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([^}]*)\}\s*;?)");
        std::sregex_iterator it(content.begin(), content.end(), struct_re);
        std::sregex_iterator end;

        if (!fs::exists(outDir))
            fs::create_directories(outDir);

        // Output file names based on input file name
        fs::path inPath(lidlFile);
        std::string base = inPath.stem().string();
        fs::path headerPath = fs::path(outDir) / (base + "_gen.hpp");
        fs::path sourcePath = fs::path(outDir) / (base + "_gen.cpp");

        std::ostringstream header;
        header << "#pragma once\n";
        header << "#include <cstdint>\n";
        header << "#include <string>\n";
        header << "#include <vector>\n";
        header << "#include <optional>\n";
        header << "\n";
        
        // simple type mapping from lidl basic types to C++ types
        std::unordered_map<std::string, std::string> typeMap = {
            {"int8", "int8_t"}, {"int16", "int16_t"}, {"int32", "int32_t"}, {"int64", "int64_t"},
            {"uint8", "uint8_t"}, {"uint16", "uint16_t"}, {"uint32", "uint32_t"}, {"uint64", "uint64_t"},
            {"float", "float"}, {"double", "double"}, {"bool", "bool"}, {"string", "std::string"}
        };

        bool anyStruct = false;
        for (; it != end; ++it)
        {
            anyStruct = true;
            std::string name = (*it)[1].str();
            std::string body = (*it)[2].str();

            header << "struct " << name << "\n{" << "\n";

            // parse fields: lines like 'type name;'
            std::regex field_re(R"(([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");
            std::sregex_iterator fit(body.begin(), body.end(), field_re);
            for (; fit != std::sregex_iterator(); ++fit)
            {
                std::string ftype = (*fit)[1].str();
                std::string fname = (*fit)[2].str();
                auto mit = typeMap.find(ftype);
                std::string cppType = (mit != typeMap.end()) ? mit->second : ftype;
                header << "    " << cppType << " " << fname << ";\n";
            }

            // default ctor
            header << "    " << name << "() = default;\n";
            header << "};\n\n";
        }

        if (!anyStruct)
        {
            // No structs found; generate a placeholder
            header << "// No struct definitions found in " << inPath.filename().string() << "\n";
            header << "namespace lidl_generated {}\n";
        }

        // write header
        std::ofstream ofsH(headerPath);
        if (!ofsH)
        {
            std::cerr << "LIdl: failed to open output header: " << headerPath << "\n";
            return false;
        }
        ofsH << header.str();

        // generate a trivial source file that includes the header
        std::ofstream ofsCpp(sourcePath);
        if (!ofsCpp)
        {
            std::cerr << "LIdl: failed to open output source: " << sourcePath << "\n";
            return false;
        }
        ofsCpp << "#include \"" << headerPath.filename().string() << "\"\n";

        std::cout << "LIdl: generated header " << headerPath.string() << " and source " << sourcePath.string() << "\n";
    return true;
}
    catch (const std::exception & ex)
    {
        std::cerr << "LIdl: exception: " << ex.what() << "\n";
        return false;
    }
}
