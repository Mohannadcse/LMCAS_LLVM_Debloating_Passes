/*
 * Utility.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <fstream>

using namespace llvm;
using namespace std;

namespace lmcas {

vector<std::string> splitString(std::string &str, char delim);

uint32_t neckIndex(Module &module, vector<Instruction *> &instList,
                   string funcName);

int returnIndex(std::vector<Instruction *> list, Instruction *inst);

vector<Instruction *> initalizePreNeckInstList(Module &module, string funcName);

} // namespace lmcas
#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_ */
