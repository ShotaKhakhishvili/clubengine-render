#pragma once

#include "SceneDesc.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CER
{

// Reads a scene file. Collects every problem (with its JSON path) instead of stopping at the first,
// so one failed render tells the author everything that needs fixing.
class SceneParser
{
public:
    bool Parse(const std::filesystem::path& file, FSceneDesc& outScene);

    const std::vector<std::string>& GetErrors() const { return errors; }

private:
    std::vector<std::string> errors;
};

}
