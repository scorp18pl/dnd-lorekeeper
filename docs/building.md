# Building Lorekeeper

## Prerequisites

| Tool | Minimum | Notes |
|---|---|---|
| CMake | 3.20 | |
| C++ compiler | C++20 | MSVC 19.x, GCC 12, Clang 15 |
| Python | 3.x | Required by glad2 to generate the OpenGL loader |
| jinja2 | any | `pip install jinja2` |
| Git | any | FetchContent clones dependencies |

All other dependencies (GLFW, GLM, glad2, Dear ImGui, nlohmann/json, stb) are
downloaded and built automatically by CMake at first configure.

## Build

```bash
pip install jinja2          # one-time; only needed for glad2 code generation

cmake -B build
cmake --build build --config Release
```

The executable is written to `build/Release/lorekeeper.exe` (MSVC) or
`build/lorekeeper` (GCC/Clang).

## Optional: surface texture

Drop an equirectangular map image as `assets/surface.jpg` (or `.png`) next to
the executable. Without one the globe renders as a solid lit sphere.
