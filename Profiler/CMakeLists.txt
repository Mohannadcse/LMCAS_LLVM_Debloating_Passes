set (CMAKE_CXX_STANDARD 14)
add_library(LLVMPprofiler MODULE
  Profiler.cpp
)


target_compile_features(LLVMPprofiler PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(LLVMPprofiler PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)

