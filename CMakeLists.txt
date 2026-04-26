cmake_minimum_required(VERSION 3.20)
project(Lorekeeper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# glad2 generates the OpenGL loader at build time using Python + jinja2.
find_package(Python COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import jinja2"
    RESULT_VARIABLE _jinja2_missing)
if(_jinja2_missing)
    message(FATAL_ERROR
        "Python package 'jinja2' is required by glad2 to generate the OpenGL loader.\n"
        "Install it with:  pip install jinja2\n"
        "Then re-run cmake.")
endif()

# ── GLFW ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── GLM ───────────────────────────────────────────────────────────────────────
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glm)

# ── glad2 (OpenGL 3.3 core) ───────────────────────────────────────────────────
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v2.0.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl REPRODUCIBLE LOADER API gl:core=3.3)

# ── Dear ImGui (docking branch) ───────────────────────────────────────────────
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw glad_gl)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD2)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# ── stb (single-header libraries) ─────────────────────────────────────────────
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(stb)

# ── tinyfiledialogs (native OS file/folder dialogs) ───────────────────────────
FetchContent_Declare(tfd
    GIT_REPOSITORY https://github.com/native-toolkit/libtinyfiledialogs.git
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(tfd)

add_library(tfd STATIC ${tfd_SOURCE_DIR}/tinyfiledialogs.c)
target_include_directories(tfd PUBLIC ${tfd_SOURCE_DIR})
if(WIN32)
    target_link_libraries(tfd PRIVATE comdlg32 ole32 shell32)
endif()

# ── Lorekeeper ────────────────────────────────────────────────────────────────
file(GLOB_RECURSE LK_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(lorekeeper ${LK_SOURCES})

target_include_directories(lorekeeper PRIVATE
    src
    ${stb_SOURCE_DIR})

target_link_libraries(lorekeeper PRIVATE
    glfw
    glad_gl
    imgui
    tfd
    glm::glm
    nlohmann_json::nlohmann_json)

target_compile_options(lorekeeper PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

# Copy shaders next to the executable after each build
add_custom_command(TARGET lorekeeper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:lorekeeper>/shaders
    COMMENT "Copying shaders")

# Copy assets directory next to the executable (contents gitignored)
add_custom_command(TARGET lorekeeper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:lorekeeper>/assets
    COMMENT "Copying assets")
