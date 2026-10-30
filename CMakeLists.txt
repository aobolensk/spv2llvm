cmake_minimum_required(VERSION 3.20)
project(spv2llvm-mlir LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG HINTS "${LLVM_DIR}/../mlir")
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

include_directories(${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})

add_executable(spv2llvm-mlir spv2llvm-mlir.cpp)
llvm_update_compile_flags(spv2llvm-mlir)
target_link_libraries(spv2llvm-mlir PRIVATE
  MLIRSPIRVDialect
  MLIRSPIRVDeserialization
  MLIRSPIRVToLLVM
  MLIRReconcileUnrealizedCasts
  MLIRLLVMDialect
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
  MLIRTargetLLVMIRExport
  MLIRPass
  MLIRIR
  LLVMBitWriter
  LLVMSupport)
target_link_options(spv2llvm-mlir PRIVATE -static-libstdc++ -static-libgcc -no-pie)
