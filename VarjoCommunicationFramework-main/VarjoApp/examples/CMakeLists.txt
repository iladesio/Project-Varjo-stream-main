project(VarjoExamples)
cmake_minimum_required(VERSION 3.12)
set (CMAKE_CXX_STANDARD 17)

# "Detect" platform to set correct imported locations
if(DEFINED ENV{Platform})
  if("$ENV{Platform}" STREQUAL "x86")
    set(_arch "x86")
  elseif("$ENV{Platform}" STREQUAL "x64")
    set(_arch "x86_64")
  endif()
else()
  set(_arch "x86_64")
endif()

# If running on cmake > 3.19, set cmp0111 to old to avoid spamming
# warnings/error to console when generating a project.
if (POLICY CMP0111)
  cmake_policy(SET CMP0111 OLD)
endif()

# Add library: Varjo
add_library(VarjoLib SHARED IMPORTED GLOBAL)
set_target_properties(VarjoLib PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/bin/VarjoLib.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/lib/VarjoLib.lib"
)

# Add library: OpenVR
add_library(OpenVR SHARED IMPORTED GLOBAL)
set_target_properties(OpenVR PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/include"
    IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/bin/${_arch}/Debug/openvr_api.dll"
    IMPORTED_LOCATION_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/bin/${_arch}/Release/openvr_api.dll"
    IMPORTED_LOCATION_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/bin/${_arch}/RelWithDebInfo/openvr_api.dll"
    IMPORTED_IMPLIB_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/lib/${_arch}/Debug/openvr_api.lib"
    IMPORTED_IMPLIB_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/lib/${_arch}/Release/openvr_api.lib"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenVR/lib/${_arch}/RelWithDebInfo/openvr_api.lib"
)
add_library(OpenVR::OpenVR ALIAS OpenVR)

# Add library: GLEW
add_library(GLEW STATIC  IMPORTED GLOBAL)
set_target_properties(GLEW PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Glew/include"
    IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Glew/lib/${_arch}/Debug/libglew32d.lib"
    IMPORTED_LOCATION_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Glew/lib/${_arch}/Release/libglew32.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Glew/lib/${_arch}/RelWithDebInfo/libglew32.lib"
    INTERFACE_COMPILE_DEFINITIONS "GLEW_STATIC"
)
add_library(Glew::Glew ALIAS GLEW)

# Add library: ImGui
add_library(ImGui SHARED IMPORTED GLOBAL)
set_target_properties(ImGui PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/include"
    IMPORTED_IMPLIB_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/Debug/ImGui.lib"
    IMPORTED_IMPLIB_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/Release/ImGui.lib"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/RelWithDebInfo/ImGui.lib"
)
add_library(ImGui::ImGui ALIAS ImGui)

# Add library: ImGuiBackends
add_library(ImGuiBackends SHARED IMPORTED GLOBAL)
set_target_properties(ImGuiBackends PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/include"
    IMPORTED_IMPLIB_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/Debug/ImGuiBackends.lib"
    IMPORTED_IMPLIB_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/Release/ImGuiBackends.lib"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ImGui/lib/${_arch}/RelWithDebInfo/ImGuiBackends.lib"
)
add_library(ImGui::ImGuiBackends ALIAS ImGuiBackends)

# Add library: FreeType
add_library(FreeType SHARED IMPORTED GLOBAL)
set_target_properties(FreeType PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/FreeType/include"
    IMPORTED_IMPLIB_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/vendor/FreeType/lib/${_arch}/Debug/freetyped.lib"
    IMPORTED_IMPLIB_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/FreeType/lib/${_arch}/Release/freetype.lib"
    IMPORTED_IMPLIB_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/vendor/FreeType/lib/${_arch}/RelWithDebInfo/freetype.lib"
)
add_library(FreeType::FreeType ALIAS FreeType)

# Add library: GLM
add_library(GLM INTERFACE)
target_include_directories(GLM INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/GLM/include>
)
add_library(GLM::GLM ALIAS GLM)

# Add library: JSON
add_library(JSON INTERFACE)
target_include_directories(JSON INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include>
)
add_library(JSON::JSON ALIAS JSON)

# Add library: CxxOpts
add_library(CxxOpts INTERFACE)
target_include_directories(CxxOpts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/cxxopts/include>
)
add_library(CxxOpts::CxxOpts ALIAS CxxOpts)

# Add library: D3DX12
add_library(D3DX12 INTERFACE)
target_include_directories(D3DX12 INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/D3DX12/include>
)
add_library(D3DX12::D3DX12 ALIAS D3DX12)

# Add public SDK examples
add_subdirectory(Benchmark)
add_subdirectory(EyeCameraStreamExample)
add_subdirectory(GazeTrackingExample)
add_subdirectory(MRExample)
add_subdirectory(MarkerExample)
add_subdirectory(ChromaKeyExample)
add_subdirectory(MaskingTool)

# If we are building to another directory, copy dll files from bin
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message("Copying DLL files from ${CMAKE_SOURCE_DIR} to ${CMAKE_BINARY_DIR}")
    add_custom_target(copyLibs ALL)
    add_custom_command(
        TARGET copyLibs
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bin/VarjoLib.dll ${CMAKE_BINARY_DIR}/bin/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/vendor/OpenVR/bin/${_arch}/$<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:Release>$<$<CONFIG:RelWithDebInfo>:RelWithDebInfo>/openvr_api.dll ${CMAKE_BINARY_DIR}/bin/
    )

    # Add libs to public SDK examples
    add_dependencies(Benchmark copyLibs)
    add_dependencies(EyeCameraStreamExample copyLibs)
    add_dependencies(GazeTrackingExample copyLibs)
    add_dependencies(MRExample copyLibs)
    add_dependencies(MarkerExample copyLibs)
    add_dependencies(ChromaKeyExample copyLibs)
    add_dependencies(MaskingTool copyLibs)

endif()
