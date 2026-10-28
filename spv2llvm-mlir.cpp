// spv2llvm-mlir: SPIR-V binary -> spirv dialect -> llvm dialect -> LLVM IR
// bitcode, in one process, using upstream MLIR only.
//
// Pipeline (equivalent to the CLI chain):
//   mlir-translate --deserialize-spirv in.spv
//   | mlir-opt --convert-spirv-to-llvm=client-api=<API> --reconcile-unrealized-casts
//   | <take the nested module produced from spirv.module>
//   | mlir-translate --mlir-to-llvmir
// The nested module is translated directly, as mlir-runner
// --link-nested-modules does for the SPIR-V CPU runner flow.

#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SPIRVToLLVM/SPIRVToLLVMPass.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Target/SPIRV/Deserialization.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

static llvm::cl::opt<std::string> inputFile(llvm::cl::Positional,
                                            llvm::cl::desc("<in.spv>"),
                                            llvm::cl::Required);
static llvm::cl::opt<std::string> outputFile("o", llvm::cl::desc("Output .bc"),
                                             llvm::cl::Required);

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::cl::ParseCommandLineOptions(argc, argv, "SPIR-V to LLVM via MLIR\n");

  auto buf = llvm::MemoryBuffer::getFile(inputFile, /*IsText=*/false,
                                         /*RequiresNullTerminator=*/false);
  if (!buf) {
    llvm::errs() << "error: cannot read " << inputFile << ": "
                 << buf.getError().message() << "\n";
    return 1;
  }

  DialectRegistry registry;
  registry.insert<spirv::SPIRVDialect, LLVM::LLVMDialect>();
  registerBuiltinDialectTranslation(registry);
  registerLLVMDialectTranslation(registry);
  MLIRContext context(registry, MLIRContext::Threading::DISABLED);
  context.loadDialect<spirv::SPIRVDialect>();

  size_t size = (*buf)->getBufferSize();
  if (size % sizeof(uint32_t) != 0) {
    llvm::errs() << "error: SPIR-V binary module must contain integral number "
                    "of 32-bit words\n";
    return 1;
  }
  auto words = llvm::ArrayRef(
      reinterpret_cast<const uint32_t *>((*buf)->getBufferStart()),
      size / sizeof(uint32_t));
  OwningOpRef<spirv::ModuleOp> spvModule = spirv::deserialize(words, &context);
  if (!spvModule || failed(verify(*spvModule)))
    return 1;

  spirv::ClientAPI api = spirv::ClientAPI::Unknown;
  switch (spvModule->getMemoryModel()) {
  case spirv::MemoryModel::OpenCL:
    api = spirv::ClientAPI::OpenCL;
    break;
  case spirv::MemoryModel::GLSL450:
  case spirv::MemoryModel::Vulkan:
    api = spirv::ClientAPI::Vulkan;
    break;
  default:
    break;
  }

  // Wrap in builtin.module as mlir-opt does for a non-module top-level op.
  OwningOpRef<ModuleOp> top = ModuleOp::create(UnknownLoc::get(&context));
  top->getBody()->push_back(spvModule.release());

  PassManager pm(&context);
  ConvertSPIRVToLLVMPassOptions opts;
  opts.clientAPI = api;
  pm.addPass(createConvertSPIRVToLLVMPass(opts));
  pm.addPass(createReconcileUnrealizedCastsPass());
  if (failed(pm.run(*top)))
    return 1;

  auto nested = top->getOps<ModuleOp>();
  if (!llvm::hasSingleElement(nested)) {
    top->emitError("expected exactly one nested module after conversion");
    return 1;
  }
  ModuleOp kernel = *nested.begin();

  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule =
      translateModuleToLLVMIR(kernel, llvmContext);
  if (!llvmModule)
    return 1;

  std::error_code ec;
  llvm::raw_fd_ostream os(outputFile, ec, llvm::sys::fs::OF_None);
  if (ec) {
    llvm::errs() << "error: cannot open " << outputFile << ": " << ec.message()
                 << "\n";
    return 1;
  }
  llvm::WriteBitcodeToFile(*llvmModule, os);
  os.close();
  if (os.has_error()) {
    llvm::errs() << "error: write failed: " << os.error().message() << "\n";
    os.clear_error();
    return 1;
  }
  return 0;
}
