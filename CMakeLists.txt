cmake_minimum_required(VERSION 3.20)
project(TheEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/external)

# -----------------------------------------------------------------------------
# GLFW
# -----------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(${EXT}/glfw EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# Vulkan headers + volk (runtime loader: no Vulkan SDK / import lib required)
# -----------------------------------------------------------------------------
add_library(volk STATIC ${EXT}/volk/volk.c)
target_include_directories(volk PUBLIC ${EXT}/volk ${EXT}/Vulkan-Headers/include)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# -----------------------------------------------------------------------------
# Dear ImGui (docking branch) + ImGuizmo + ImAnim
# -----------------------------------------------------------------------------
add_library(imgui STATIC
    ${EXT}/imgui/imgui.cpp
    ${EXT}/imgui/imgui_draw.cpp
    ${EXT}/imgui/imgui_tables.cpp
    ${EXT}/imgui/imgui_widgets.cpp
    ${EXT}/imgui/imgui_demo.cpp
    ${EXT}/imgui/backends/imgui_impl_glfw.cpp
    ${EXT}/imgui/backends/imgui_impl_vulkan.cpp
    ${EXT}/ImGuizmo/src/ImGuizmo.cpp
    ${EXT}/ImAnim/im_anim.cpp
)
target_include_directories(imgui PUBLIC
    ${EXT}/imgui
    ${EXT}/imgui/backends
    ${EXT}/ImGuizmo/src
    ${EXT}/ImAnim
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC glfw volk)

# -----------------------------------------------------------------------------
# glslang (used as a build-time shader compiler)
# -----------------------------------------------------------------------------
set(ENABLE_OPT             OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS          OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL            OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
add_subdirectory(${EXT}/glslang EXCLUDE_FROM_ALL)

add_executable(shaderc_mini tools/shaderc_mini.cpp)
target_link_libraries(shaderc_mini PRIVATE glslang glslang-default-resource-limits)
target_include_directories(shaderc_mini PRIVATE ${EXT} ${EXT}/glslang)
set_property(TARGET shaderc_mini PROPERTY FOLDER tools)

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${SHADER_SRC_DIR}/*.vert ${SHADER_SRC_DIR}/*.frag)
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_SRC_DIR}/*.glsl)

set(SPIRV_OUTPUTS)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(NAME ${SHADER} NAME)
    set(OUT ${SHADER_OUT_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND $<TARGET_FILE:shaderc_mini> ${SHADER} ${OUT} ${SHADER_SRC_DIR}
        DEPENDS ${SHADER} ${SHADER_INCLUDES} shaderc_mini
        COMMENT "Compiling shader ${NAME}"
        VERBATIM)
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()
add_custom_target(shaders DEPENDS ${SPIRV_OUTPUTS})

# -----------------------------------------------------------------------------
# TheEngine
# -----------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(TheEngine ${ENGINE_SOURCES})
add_dependencies(TheEngine shaders)
target_include_directories(TheEngine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${EXT}/glm ${EXT}/stb ${EXT}/cgltf)
target_compile_definitions(TheEngine PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    GLFW_INCLUDE_NONE
    _CRT_SECURE_NO_WARNINGS
    ENGINE_SHADER_DIR="${SHADER_OUT_DIR}")
target_link_libraries(TheEngine PRIVATE imgui glfw volk)
if(WIN32)
    target_link_libraries(TheEngine PRIVATE ole32 shell32 uuid)
endif()
if(MSVC)
    target_compile_options(TheEngine PRIVATE /W3 /MP /utf-8)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT TheEngine)
endif()

# Copy compiled shaders next to the executable so it can be run standalone
add_custom_command(TARGET TheEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_OUT_DIR} $<TARGET_FILE_DIR:TheEngine>/shaders)

# Automated editor test: `ctest -C Release` (or run `TheEngine --selftest`).
enable_testing()
add_test(NAME editor_selftest COMMAND TheEngine --selftest WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# -----------------------------------------------------------------------------
# C# scripting: host .NET through nethost/hostfxr and build the engine API assembly
# -----------------------------------------------------------------------------
find_program(DOTNET_EXE dotnet)
set(NETHOST_DIR "")
if(DOTNET_EXE AND WIN32)
    get_filename_component(DOTNET_ROOT_DIR "${DOTNET_EXE}" DIRECTORY)
    file(GLOB NETHOST_CANDIDATES "${DOTNET_ROOT_DIR}/packs/Microsoft.NETCore.App.Host.win-x64/*/runtimes/win-x64/native")
    if(NETHOST_CANDIDATES)
        list(SORT NETHOST_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
        list(GET NETHOST_CANDIDATES 0 NETHOST_DIR)
    endif()
endif()

if(NETHOST_DIR AND EXISTS "${NETHOST_DIR}/nethost.lib")
    message(STATUS "C# scripting enabled (nethost: ${NETHOST_DIR})")
    target_include_directories(TheEngine PRIVATE "${NETHOST_DIR}")
    target_compile_definitions(TheEngine PRIVATE THEENGINE_HAS_DOTNET)
    # nethost.dll (dynamic CRT compatible; the static libnethost uses /MT).
    target_link_libraries(TheEngine PRIVATE "${NETHOST_DIR}/nethost.lib")
    add_custom_command(TARGET TheEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NETHOST_DIR}/nethost.dll" $<TARGET_FILE_DIR:TheEngine>)

    set(SCRIPTCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripting/ScriptCore)
    set(SCRIPTCORE_OUT ${CMAKE_BINARY_DIR}/Scripting)
    file(GLOB SCRIPTCORE_SOURCES CONFIGURE_DEPENDS ${SCRIPTCORE_DIR}/*.cs ${SCRIPTCORE_DIR}/*.csproj)
    add_custom_command(
        OUTPUT ${SCRIPTCORE_OUT}/TheEngine.ScriptCore.dll
        COMMAND ${DOTNET_EXE} build ${SCRIPTCORE_DIR}/TheEngine.ScriptCore.csproj -c Release -o ${SCRIPTCORE_OUT}
                --nologo -v q -clp:NoSummary "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/ScriptCoreObj/"
        DEPENDS ${SCRIPTCORE_SOURCES}
        COMMENT "Building TheEngine.ScriptCore (C#)"
        VERBATIM)
    add_custom_target(ScriptCore DEPENDS ${SCRIPTCORE_OUT}/TheEngine.ScriptCore.dll)
    add_dependencies(TheEngine ScriptCore)
    add_custom_command(TARGET TheEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SCRIPTCORE_OUT} $<TARGET_FILE_DIR:TheEngine>/Scripting)
else()
    message(WARNING "The .NET SDK (with nethost) was not found: C# scripting will be disabled.")
endif()
