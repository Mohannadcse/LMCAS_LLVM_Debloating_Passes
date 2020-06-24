This repo contains 2 LLVM passes:
=================================

1- Debloat: provides several obtions to perform debloating 

2- Profiler: analysis pass that generates several statistics about number of methods, calls, instructions, basic blocks, etc..


Building steps:
===============

The following steps use `LLVM 6.0`. You can build your own LLVM pass or download a precompiled version

- clone the repo

- `mkdir build_debloat && cd build_debloat`

- cmake -DLLVM_DIR=<path-to-llvm>/lib/cmake/llvm \
		../LLVM_Debloating_Passes/
- `make`

Usage:
======
 
1- Debloating pass: `~/Downloads/LLVM_6.0/bin/opt -load \

	build_debloat/Debloat/libLLVMDebloat.so \

 	-debloat -globals=$gbls -locals=$locals -bbfile=bbs.txt

	 <.bc> -verify -o <.bc>`

2- Profiling pass: `<path-to-llvm>/bin/opt -load \

	build_debloat/Profiler/libLLVMPprofiler.so \

 -Pprofiler -size=${size_orig} -o /dev/null <.bc>`
