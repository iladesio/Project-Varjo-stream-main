set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(_build_output_dir ${CMAKE_BINARY_DIR}/bin)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${_build_output_dir})
endforeach(OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES)

set(_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(_src_common_dir ${CMAKE_CURRENT_SOURCE_DIR}/../Common)

find_package(cpprestsdk CONFIG REQUIRED)
# Find the OpenCV package
find_package(OpenCV REQUIRED)

# Include OpenCV headers
include_directories(${OpenCV_INCLUDE_DIRS})

set(_sources_app
    ${_src_dir}/main.cpp
    ${_src_dir}/AppLogic.hpp
    ${_src_dir}/AppLogic.cpp
    ${_src_dir}/AppState.hpp
    ${_src_dir}/AppView.hpp
    ${_src_dir}/AppView.cpp
    ${_src_dir}/MRScene.hpp
    ${_src_dir}/MRScene.cpp
)

set(_sources_common
    ${_src_common_dir}/CameraManager.hpp
    ${_src_common_dir}/CameraManager.cpp
    ${_src_common_dir}/D3D11MultiLayerView.hpp
    ${_src_common_dir}/D3D11MultiLayerView.cpp
    ${_src_common_dir}/D3D11Renderer.hpp
    ${_src_common_dir}/D3D11Renderer.cpp
    ${_src_common_dir}/D3D11Shaders.hpp
    ${_src_common_dir}/D3D11Shaders.cpp
    ${_src_common_dir}/DataStreamer.hpp
    ${_src_common_dir}/DataStreamer.cpp
    ${_src_common_dir}/ExampleShaders.hpp
    ${_src_common_dir}/GfxContext.hpp
    ${_src_common_dir}/GfxContext.cpp
    ${_src_common_dir}/Globals.hpp
    ${_src_common_dir}/Globals.cpp
    ${_src_common_dir}/MultiLayerView.hpp
    ${_src_common_dir}/MultiLayerView.cpp
    ${_src_common_dir}/Renderer.hpp
    ${_src_common_dir}/Renderer.cpp
    ${_src_common_dir}/Scene.hpp
    ${_src_common_dir}/Scene.cpp
    ${_src_common_dir}/SyncView.hpp
    ${_src_common_dir}/SyncView.cpp
    ${_src_common_dir}/UI.hpp
    ${_src_common_dir}/UI.cpp
    ${_src_common_dir}/Undistorter.hpp
    ${_src_common_dir}/Undistorter.cpp
)

source_group("Common" FILES ${_sources_common})

set(_target MRExample)
add_executable(${_target} ${_sources_app} ${_sources_common})

target_include_directories(${_target} PRIVATE ${_src_common_dir})

set_property(TARGET ${_target} PROPERTY FOLDER "Examples")
set_target_properties(${_target} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_compile_definitions(${_target} PUBLIC -D_UNICODE -DUNICODE -DNOMINMAX ${VarjoLibDefinitions})

target_link_libraries(${_target}
    PRIVATE ImGui::ImGui
    PRIVATE ImGui::ImGuiBackends
    PRIVATE FreeType::FreeType
    PRIVATE cpprestsdk::cpprest
    PRIVATE ${OpenCV_LIBS}
    PRIVATE GLM
    PRIVATE CxxOpts
    PRIVATE VarjoLib
    PRIVATE d3d11
    PRIVATE dxgi
    PRIVATE d3dcompiler
    PRIVATE windowscodecs
)
