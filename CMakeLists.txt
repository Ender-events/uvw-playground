cmake_minimum_required(VERSION 3.14)

project(uvw-playground
  VERSION 1.0
  DESCRIPTION "Playground with uvw"
  LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch the header-only library "uvw" from GitHub
include(FetchContent)
FetchContent_Declare(uvw
  GIT_REPOSITORY https://github.com/Ender-events/uvw.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(uvw)

# Fetch the library "libuv" from GitHub
FetchContent_Declare(libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.44.2
)
FetchContent_MakeAvailable(libuv)

# Create the executable target
add_executable(uvw-playground main.cpp)

# Set C++ version for the target
target_compile_features(uvw-playground PUBLIC cxx_std_20)

# Link the "uvw" library to the target
target_link_libraries(uvw-playground PRIVATE uvw::uvw uv_a)