#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace heartstead::modding {

struct ModManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies;
    std::filesystem::path root;
};

} // namespace heartstead::modding
