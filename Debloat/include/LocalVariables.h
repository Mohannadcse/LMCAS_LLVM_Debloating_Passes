/*
 * LocalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_

#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicInst.h"

#include "Utility.h"

using namespace llvm;
using namespace std;


class LocalVariables {
private:
	ofstream logger;
	void replaceLocalPrimitiveUsesAfterNeck(Module &module, map<string, uint64_t> &plocals,
			map<AllocaInst*, uint64_t> instrToIdx, std::vector<Instruction*> instList);
	void replaceLocalStructUsesAfterNeck(Module &module,
			map<pair<std::string, uint64_t>, uint64_t> &clocals,
			vector<Instruction*> instList);
public:
	void handlePrimitiveLocalVariables(Module &module, map<string, uint64_t> &plocals);
	void handleCustomizedLocalVariables(Module &module, map <pair<std::string, uint64_t>, uint64_t> &clocals);
	LocalVariables() {
		logger.open("logger.txt", ofstream::app);

	}
//	~LocalVariables(){
//		logger << strLogger.str();
//	}
};


#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_ */
