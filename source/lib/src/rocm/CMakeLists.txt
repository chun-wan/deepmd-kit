# required cmake version
cmake_minimum_required(VERSION 3.5)
# project name
project(deepmd_op_rocm)
set(CMAKE_LINK_WHAT_YOU_USE TRUE)

# set c++ version c++14
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_HIP_STANDARD 14)
add_definitions("-DCUB_IGNORE_DEPRECATED_CPP_DIALECT")
add_definitions("-DCUB_IGNORE_DEPRECATED_CPP_DIALECT")

message(STATUS "HIP major version is " ${HIP_VERSION_MAJOR})

set (HIP_HIPCC_FLAGS ; -fno-gpu-rdc; --amdgpu-target=gfx906,gfx908,gfx90a; -fPIC; -O3; --std=c++14)

file (GLOB SOURCE_FILES "*.hip.cu" )

hip_add_library(deepmd_op_rocm SHARED ${SOURCE_FILES})
target_include_directories(deepmd_op_rocm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include/)

install(TARGETS deepmd_op_rocm DESTINATION lib/)
if (BUILD_CPP_IF)
  install(TARGETS deepmd_op_rocm DESTINATION lib/)
endif (BUILD_CPP_IF)
if (BUILD_PY_IF)
  install(TARGETS deepmd_op_rocm DESTINATION deepmd/op/)
endif (BUILD_PY_IF)
