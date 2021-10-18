/*
 * GlobalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_


// #include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

#include <set>
#include <map>

#include "Utility.h"

using namespace llvm;
using namespace std;

//typedef string Global;
//typedef pair<string, uint64_t> BasicBlocks;

class GlobalVariables{
private:
	ofstream logger;

public:
	void handleGlobalVariables(Module &module, map<string, uint64_t> &globals,
			set<pair<string, uint64_t>> visitedBbs, string);
	GlobalVariables(){
		logger.open("logger.txt", ofstream::app);
	}
};




#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_ */
