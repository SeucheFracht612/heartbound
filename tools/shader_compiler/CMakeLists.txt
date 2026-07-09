add_executable(heartstead_shader_compiler main.cpp)
target_link_libraries(heartstead_shader_compiler PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_shader_compiler
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_shader_compiler)
