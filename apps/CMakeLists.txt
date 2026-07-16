find_program(HEARTSTEAD_GLSLANG_VALIDATOR NAMES glslangValidator glslang)

function(heartstead_attach_terrain_shader_assets target)
  set(shader_source_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/render_smoke/assets/shaders")
  set(compiled_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_compiled_shaders")
  set(vertex_source "${shader_source_dir}/terrain.vert")
  set(fragment_source "${shader_source_dir}/terrain.frag")

  if(HEARTSTEAD_GLSLANG_VALIDATOR)
    set(vertex_spirv "${compiled_shader_dir}/terrain.vert.spv")
    set(fragment_spirv "${compiled_shader_dir}/terrain.frag.spv")
    add_custom_command(
      OUTPUT "${vertex_spirv}" "${fragment_spirv}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${compiled_shader_dir}"
      COMMAND "${HEARTSTEAD_GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
              "${vertex_source}" -o "${vertex_spirv}"
      COMMAND "${HEARTSTEAD_GLSLANG_VALIDATOR}" -V --target-env vulkan1.0
              "${fragment_source}" -o "${fragment_spirv}"
      DEPENDS "${vertex_source}" "${fragment_source}"
      COMMENT "Compiling terrain GLSL to SPIR-V"
      VERBATIM
    )
  else()
    set(vertex_spirv "${shader_source_dir}/terrain.vert.spv")
    set(fragment_spirv "${shader_source_dir}/terrain.frag.spv")
  endif()

  add_custom_target(
    ${target}_shader_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${vertex_source}"
            "$<TARGET_FILE_DIR:${target}>/assets/shaders/terrain.vert"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${fragment_source}"
            "$<TARGET_FILE_DIR:${target}>/assets/shaders/terrain.frag"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${vertex_spirv}"
            "$<TARGET_FILE_DIR:${target}>/assets/shaders/terrain.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${fragment_spirv}"
            "$<TARGET_FILE_DIR:${target}>/assets/shaders/terrain.frag.spv"
    DEPENDS "${vertex_spirv}" "${fragment_spirv}"
    COMMENT "Staging compiled terrain shader assets for ${target}"
    VERBATIM
  )
  add_dependencies(${target} ${target}_shader_assets)
endfunction()

add_subdirectory(render_smoke)
add_subdirectory(render_benchmark)
