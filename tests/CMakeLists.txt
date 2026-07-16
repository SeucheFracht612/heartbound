add_executable(heartstead_engine_tests engine_tests.cpp)
target_link_libraries(heartstead_engine_tests PRIVATE Heartstead::Engine)
heartstead_configure_target(heartstead_engine_tests)

add_test(NAME heartstead_engine_tests COMMAND heartstead_engine_tests)

add_executable(heartstead_game_runtime_tests game_runtime_tests.cpp)
target_link_libraries(heartstead_game_runtime_tests PRIVATE Heartstead::GameRuntime)
target_compile_definitions(
  heartstead_game_runtime_tests PRIVATE HEARTSTEAD_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
heartstead_configure_target(heartstead_game_runtime_tests)

add_test(NAME heartstead_game_runtime_tests COMMAND heartstead_game_runtime_tests)

function(heartstead_add_foundation_test target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE Heartstead::Engine)
  heartstead_configure_target(${target_name})
  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

heartstead_add_foundation_test(heartstead_coordinate_v02_tests coordinate_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_world_integrity_v02_tests world_integrity_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_replication_v02_tests replication_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_server_foundations_v02_tests
                               server_foundations_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_rich_blocks_v02_tests rich_blocks_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_region_storage_v02_tests region_storage_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_save_recovery_v02_tests save_recovery_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_authoritative_server_v02_tests
                               authoritative_server_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_lazy_process_fire_v02_tests
                               lazy_process_fire_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_workpiece_v02_tests workpiece_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_assembly_v02_tests assembly_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_infrastructure_v02_tests infrastructure_v02_tests.cpp)
heartstead_add_foundation_test(heartstead_render_milestone_tests render_milestone_tests.cpp)
heartstead_add_foundation_test(heartstead_renderer_frontend_tests renderer_frontend_tests.cpp)
heartstead_add_foundation_test(heartstead_chunk_mesh_scheduler_tests
                               chunk_mesh_scheduler_tests.cpp)
heartstead_add_foundation_test(heartstead_gpu_buffer_arena_tests gpu_buffer_arena_tests.cpp)
heartstead_add_foundation_test(heartstead_renderer_benchmark_tests renderer_benchmark_tests.cpp)
heartstead_add_foundation_test(heartstead_render_runtime_asset_tests
                               render_runtime_asset_tests.cpp)
heartstead_add_foundation_test(heartstead_greedy_chunk_mesher_tests
                               greedy_chunk_mesher_tests.cpp)
heartstead_add_foundation_test(heartstead_render_scene_tests render_scene_tests.cpp)
heartstead_add_foundation_test(heartstead_mesh_manager_tests mesh_manager_tests.cpp)

function(heartstead_add_smoke_test test_name target_name)
  if(TARGET ${target_name})
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${target_name}> ${ARGN})
    set_tests_properties(${test_name} PROPERTIES LABELS smoke TIMEOUT 30)
  endif()
endfunction()

heartstead_add_smoke_test(smoke.platform heartstead_platform_smoke)
heartstead_add_smoke_test(smoke.renderer heartstead_renderer_smoke)
heartstead_add_smoke_test(
  smoke.render_benchmark
  heartstead_render_benchmark
  --scene
  flat
  --radius
  0
  --warmup
  1
  --frames
  3
  --output
  ${CMAKE_CURRENT_BINARY_DIR}/smoke_render_benchmark.json
)
heartstead_add_smoke_test(smoke.physics heartstead_physics_sandbox)
heartstead_add_smoke_test(smoke.network heartstead_network_sandbox)
heartstead_add_smoke_test(smoke.scripting heartstead_scripting_sandbox)
heartstead_add_smoke_test(smoke.jobs heartstead_jobs_sandbox)
heartstead_add_smoke_test(smoke.math heartstead_math_sandbox)
heartstead_add_smoke_test(smoke.mod_sandbox heartstead_mod_sandbox)
heartstead_add_smoke_test(smoke.workpiece_sandbox heartstead_workpiece_sandbox)
heartstead_add_smoke_test(smoke.chunk_sandbox heartstead_chunk_sandbox)
heartstead_add_smoke_test(smoke.room_sandbox heartstead_room_sandbox)
heartstead_add_smoke_test(smoke.world_state_sandbox heartstead_world_state_sandbox)

heartstead_add_smoke_test(smoke.asset_cooker heartstead_asset_cooker ${PROJECT_SOURCE_DIR}
                          ${CMAKE_CURRENT_BINARY_DIR}/smoke_asset_manifest.txt development)
heartstead_add_smoke_test(smoke.asset_cooker_inspect heartstead_asset_cooker ${PROJECT_SOURCE_DIR}
                          ${CMAKE_CURRENT_BINARY_DIR}/smoke_asset_manifest_inspect.txt development
                          --inspect)
heartstead_add_smoke_test(smoke.mod_validator heartstead_mod_validator ${PROJECT_SOURCE_DIR})
heartstead_add_smoke_test(smoke.mod_validator_inspect heartstead_mod_validator ${PROJECT_SOURCE_DIR}
                          --inspect)
heartstead_add_smoke_test(smoke.prototype_inspector heartstead_prototype_inspector
                          ${PROJECT_SOURCE_DIR} base:items/raw_clay)
heartstead_add_smoke_test(smoke.replay_inspector heartstead_replay_inspector
                          ${PROJECT_SOURCE_DIR}/tests/fixtures/sample_command_replay.txt)
heartstead_add_smoke_test(smoke.save_inspector heartstead_save_inspector
                          ${PROJECT_SOURCE_DIR}/tests/fixtures/minimal_save_snapshot.txt
                          ${PROJECT_SOURCE_DIR})
heartstead_add_smoke_test(smoke.save_slot_inspector heartstead_save_inspector --slots
                          ${PROJECT_SOURCE_DIR}/tests/fixtures/save_slots)
heartstead_add_smoke_test(smoke.shader_compiler heartstead_shader_compiler ${PROJECT_SOURCE_DIR}
                          ${CMAKE_CURRENT_BINARY_DIR}/smoke_shader_manifest.txt development)
heartstead_add_smoke_test(smoke.shader_compiler_inspect heartstead_shader_compiler
                          ${PROJECT_SOURCE_DIR}
                          ${CMAKE_CURRENT_BINARY_DIR}/smoke_shader_manifest_inspect.txt
                          development --inspect)
heartstead_add_smoke_test(smoke.block_model_viewer heartstead_block_model_viewer
                          ${PROJECT_SOURCE_DIR})
heartstead_add_smoke_test(smoke.chunk_inspector heartstead_chunk_inspector --help)
heartstead_add_smoke_test(smoke.workpiece_inspector heartstead_workpiece_inspector
                          ${PROJECT_SOURCE_DIR}/tests/fixtures/sample_workpiece_grid.txt)
heartstead_add_smoke_test(smoke.world_inspector heartstead_world_inspector
                          ${PROJECT_SOURCE_DIR}/tests/fixtures/minimal_save_snapshot.txt
                          ${PROJECT_SOURCE_DIR})
heartstead_add_smoke_test(smoke.log_viewer heartstead_log_viewer --help)
heartstead_add_smoke_test(smoke.map_profile_inspector heartstead_map_profile_inspector --help)
