if(NOT TARGET ZLIB::ZLIB)
  message(FATAL_ERROR "ZLIB::ZLIB was not defined before LLVMConfig.cmake")
endif()

if(NOT TARGET zstd::libzstd_static)
  message(FATAL_ERROR "zstd::libzstd_static was not defined before LLVMConfig.cmake")
endif()

if(NOT TARGET DIASDK::Diaguids)
  message(FATAL_ERROR "DIASDK::Diaguids was not defined before LLVMConfig.cmake")
endif()

set(LLVM_FOUND TRUE)
set(LLVM_PACKAGE_VERSION "test")
