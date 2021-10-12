set (CMAKE_CXX_STANDARD 14)
include_directories(include)

add_library(LLVMDebloat MODULE
  src/Debloat.cpp
  src/GlobalVariables.cpp
  src/LocalVariables.cpp
  src/Utility.cpp
  src/CleaningUpStuff.cpp
  src/Predicates.cpp
  src/PostNeckConstConversion.cpp
)

target_compile_features(LLVMDebloat PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(LLVMDebloat PROPERTIES
    COMPILE_FLAGS "-fno-rtti"
)
