#pragma once

#include "engine/scripting/script_runtime.hpp"

namespace heartstead::scripting::luau {

[[nodiscard]] ScriptBackendInfo backend_info() noexcept;

[[nodiscard]] core::Result<std::unique_ptr<IScriptRuntime>> create_runtime(ScriptRuntimeDesc desc);

} // namespace heartstead::scripting::luau
