#pragma once

#include <string>

#include "LIdl_global.h"

class LIDL_EXPORT LIdl
{
public:
    // Generate C++ headers/sources from .lidl content, return true on success.
    static bool generateFromFile(const std::string & lidlFile, const std::string & outDir);
};
