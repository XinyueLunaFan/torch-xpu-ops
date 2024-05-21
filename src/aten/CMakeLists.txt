# ATen XPU sources

file(GLOB xpu_cpp "*.cpp")
file(GLOB xpu_h "*.h")
file(GLOB xpu_sycl "sycl/*.cpp")

list(APPEND ATen_XPU_CPP_SRCS ${xpu_cpp})
list(APPEND ATen_XPU_SYCL_SRCS ${xpu_sycl})

set(ATen_XPU_CPP_SRCS ${ATen_XPU_CPP_SRCS} PARENT_SCOPE)
set(ATen_XPU_SYCL_SRCS ${ATen_XPU_SYCL_SRCS} PARENT_SCOPE)

set(INCLUDE_DIR_FOR_TORCH "${CMAKE_BINARY_DIR}/aten/src/ATen/xpu" )
file(MAKE_DIRECTORY ${INCLUDE_DIR_FOR_TORCH})
foreach(HEADER  ${xpu_h})
  file(COPY ${HEADER} DESTINATION ${INCLUDE_DIR_FOR_TORCH})
endforeach()
