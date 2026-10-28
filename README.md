# spv2llvm-mlir

Converts a SPIR-V binary to LLVM bitcode using upstream MLIR (`spirv` -> `llvm` dialect -> LLVM IR).

## Build

Requires an LLVM build or install with MLIR enabled (`-DLLVM_ENABLE_PROJECTS=mlir`).

```
cmake -S . -B build -G Ninja -DLLVM_DIR=<llvm-build>/lib/cmake/llvm
ninja -C build
```

MLIR is looked up next to `LLVM_DIR`. Pass `-DMLIR_DIR=...` to override.

## Run

```
./build/spv2llvm-mlir in.spv -o out.bc
```
