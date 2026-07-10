add_executable(heartstead_block_model_viewer main.cpp)
target_link_libraries(heartstead_block_model_viewer PRIVATE Heartstead::Engine)
target_compile_definitions(heartstead_block_model_viewer
                           PRIVATE HEARTSTEAD_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
heartstead_configure_target(heartstead_block_model_viewer)
