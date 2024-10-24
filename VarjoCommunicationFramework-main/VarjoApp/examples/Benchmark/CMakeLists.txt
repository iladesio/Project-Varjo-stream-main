set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(BENCHMARK_VULKAN_RENDERER "Set to compile Benchmark with Vulkan renderer. Requires Vulkan SDK to be installed on build PC.")

set(_build_output_dir ${CMAKE_BINARY_DIR}/bin)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${_build_output_dir})
endforeach(OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES)

set(_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(_source_list
  ${_src_dir}/Config.hpp
  ${_src_dir}/D3D11Renderer.cpp
  ${_src_dir}/D3D11Renderer.hpp
  ${_src_dir}/D3D12Renderer.cpp
  ${_src_dir}/D3D12Renderer.hpp
  ${_src_dir}/D3DShaders.cpp
  ${_src_dir}/D3DShaders.hpp
  ${_src_dir}/GLRenderer.cpp
  ${_src_dir}/GLRenderer.hpp
  ${_src_dir}/GazeTracking.cpp
  ${_src_dir}/GazeTracking.hpp
  ${_src_dir}/Geometry.cpp
  ${_src_dir}/Geometry.hpp
  ${_src_dir}/GeometryGenerator.cpp
  ${_src_dir}/GeometryGenerator.hpp
  ${_src_dir}/IRenderer.cpp
  ${_src_dir}/IRenderer.hpp
  ${_src_dir}/OpenVRTracker.cpp
  ${_src_dir}/OpenVRTracker.hpp
  ${_src_dir}/Profiler.hpp
  ${_src_dir}/VRSHelper.cpp
  ${_src_dir}/VRSHelper.hpp
  ${_src_dir}/Window.hpp
  ${_src_dir}/Window.cpp
  ${_src_dir}/main.cpp
)

if(BENCHMARK_VULKAN_RENDERER)
    find_package(Vulkan MODULE REQUIRED)
    find_program(GLSLC glslc REQUIRED)

    set(_source_list
      ${_source_list}
      ${_src_dir}/VKRenderer.cpp
      ${_src_dir}/VKRenderer.hpp
      ${_src_dir}/VKShaders.hpp
    )

    # Compile GLSL shaders to SPIR-V
    set(_shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    include(${_shader_src_dir}/shaders.cmake)

    set(_shader_target_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    foreach(_shader IN LISTS _shader_list)
        # Build debug shaders
        set(_shader_input ${_shader_src_dir}/${_shader})
        set(_shader_output ${_shader_target_dir}/Debug/${_shader}.spv.inc)

        add_custom_command(
          OUTPUT ${_shader_output}
          MAIN_DEPENDENCY ${_shader_input}
          DEPENDS ${_shader_include_list}
          COMMENT "Compiling Vulkan shader ${_shader} -> ${_shader}.spv.inc (Debug mode)"
          COMMAND ${CMAKE_COMMAND} -E remove -f ${_shader_output}
          COMMAND ${GLSLC} --target-env=vulkan1.0 -mfmt=num -Werror -O0 -g -o ${_shader_output} ${_shader_input}
        )

        list(APPEND _shader_target_list ${_shader_output})

        # Build release shaders
        set(_shader_input ${_shader_src_dir}/${_shader})
        set(_shader_output ${_shader_target_dir}/Release/${_shader}.spv.inc)

        add_custom_command(
          OUTPUT ${_shader_output}
          MAIN_DEPENDENCY ${_shader_input}
          DEPENDS ${_shader_include_list}
          COMMENT "Compiling Vulkan shader ${_shader} -> ${_shader}.spv.inc (Release mode)"
          COMMAND ${CMAKE_COMMAND} -E remove -f ${_shader_output}
          COMMAND ${GLSLC} --target-env=vulkan1.0 -mfmt=num -Werror -O -o ${_shader_output} ${_shader_input}
        )

        list(APPEND _shader_target_list ${_shader_output})
    endforeach(_shader)

    add_custom_target(_build_shaders_benchmark DEPENDS ${_shader_target_list})
    set_property(TARGET _build_shaders_benchmark PROPERTY FOLDER "Examples")
endif(BENCHMARK_VULKAN_RENDERER)

set(_target Benchmark)
add_executable(${_target} ${_source_list})

set_property(TARGET ${_target} PROPERTY FOLDER "Examples")
set_target_properties(${_target} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_compile_definitions(${_target} PUBLIC -D_UNICODE -DUNICODE ${VarjoLibDefinitions})

target_link_libraries(${_target} VarjoLib)
target_link_libraries(${_target} GLEW)
target_link_libraries(${_target} GLM)
target_link_libraries(${_target} CxxOpts)
target_link_libraries(${_target} D3DX12)
target_link_libraries(${_target} OpenVR)
target_link_libraries(${_target} d3d11)
target_link_libraries(${_target} d3d12)
target_link_libraries(${_target} dxgi)
target_link_libraries(${_target} d3dcompiler)
target_link_libraries(${_target} opengl32)
target_link_libraries(${_target} dxguid)

if(BENCHMARK_VULKAN_RENDERER)
    add_dependencies(${_target} _build_shaders_benchmark)
    target_compile_definitions(${_target} PUBLIC -DUSE_VULKAN)
    target_include_directories(${_target} PRIVATE ${_shader_target_dir}/$<IF:$<CONFIG:Debug>,Debug,Release>)
    target_link_libraries(${_target} Vulkan::Vulkan)
endif(BENCHMARK_VULKAN_RENDERER)
