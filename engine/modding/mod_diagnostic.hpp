#pragma once

#include <filesystem>
#include <string>

namespace heartstead::modding {

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

struct ModDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    std::filesystem::path source;
    std::string code;
    std::string message;
};

} // namespace heartstead::modding
