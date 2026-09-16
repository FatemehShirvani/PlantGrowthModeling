cmake_minimum_required(VERSION 3.14)

project(PlantGrowthModeling LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm)

add_library(glad STATIC dep/glad/src/glad.c)
target_include_directories(glad PUBLIC dep/glad/include)

add_executable(
  plant_growth
  src/main.cpp
  src/Error.cpp
  src/Mesh.cpp
  src/ShaderProgram.cpp
)

target_link_libraries(plant_growth PRIVATE glad glfw glm::glm ${CMAKE_DL_LIBS})
