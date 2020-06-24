set (CMAKE_CXX_STANDARD 11)
add_library(LLVMDebloat MODULE
  Debloat.cpp
)

target_compile_features(LLVMDebloat PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(LLVMDebloat PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)
