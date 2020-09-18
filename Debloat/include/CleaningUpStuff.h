/*
 * CleaningUp.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_
#define SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_

#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/CallGraph.h"

#include <set>

#include "Utility.h"

using namespace llvm;

class CleaningUpStuff {
public:
	void removeUnusedStuff(Module &module);
	CleaningUpStuff(){
		logger.open("logger.txt", std::ofstream::app);
	}

private:
	std::ofstream logger;
	void dfsutils(llvm::CallGraph &cg, Function* f, std::set<const llvm::Function*>&reachableFunctionsFromMain);
};



#endif /* SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_ */
