add_executable(heartstead_mod_sandbox main.cpp)
target_link_libraries(heartstead_mod_sandbox PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_mod_sandbox
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_mod_sandbox)
