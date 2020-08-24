/*
 * GlobalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_GLOBALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_GLOBALVARIABLES_H_


#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/GlobalVariable.h"

#include <set>
#include <map>

#include "Utility.h"

using namespace llvm;
using namespace std;

//typedef string Global;
//typedef pair<string, uint64_t> BasicBlocks;

class GlobalVariables{
public:
	void handleGlobalVariables(Module &module, map<string, uint64_t> &globals, set<pair<string, uint64_t>> visitedBbs);
};




#endif /* SOURCE_DIRECTORY__DEBLOAT_GLOBALVARIABLES_H_ */
