This repo contains 2 LLVM passes:
=================================

1- Debloat: provides several options to perform debloating 

2- Profiler: analysis pass that generates several statistics about number of methods, calls, instructions, basic blocks, etc..


Building steps:
===============

The following steps use `LLVM 12.0`. You can build your own LLVM pass or download a precompiled version

1- clone the repo

2- `mkdir build_debloat && cd build_debloat`

3- Run CMake with the path to the LLVM source:

	cmake ../LLVM_Debloating_Passes
	
4- `make`

Usage:
======
 
1- Debloating pass: 
	
	<path-to-llvm>/bin/opt -load \
	build_debloat/Debloat/libLLVMDebloat.so -debloat \
	-globals=gbls.txt\
    -plocals=primitiveLocals.txt \
	-clocals=customizedLocals.txt\
    -ptrStructlocals=ptrToStructLocals.txt \
    -ptrToPrimLocals=ptrToPrimitiveLocals.txt \
    -stringVars=stringVars.txt  \
 	-bbfile=bbs.txt -appName=${app} ${app}_orig.bc -verify -o ${app}_cc.bc
	

2- Profiling pass: 

	<path-to-llvm>/bin/opt -load \
	build_debloat/Profiler/libLLVMPprofiler.so \
 	-Pprofiler -size=${size_orig} -o /dev/null <.bc>
