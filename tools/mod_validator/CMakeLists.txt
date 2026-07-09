add_executable(heartstead_mod_validator main.cpp)
target_link_libraries(heartstead_mod_validator PRIVATE Heartstead::Engine)
target_compile_definitions(
  heartstead_mod_validator
  PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_mod_validator)
