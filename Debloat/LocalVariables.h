/*
 * LocalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_LOCALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_LOCALVARIABLES_H_

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
public:
	void handlePrimitiveLocalVariables(Module &module, map<string, uint64_t> &plocals);
	void handleCustomizedLocalVariables(Module &module, map <pair<std::string, uint64_t>, uint64_t> &clocals);
private:
	void replaceLocalPrimitiveUsesAfterNeck(Module &module, map<string, uint64_t> &plocals,
			map<AllocaInst*, uint64_t> instrToIdx, std::vector<Instruction*> instList);
	void replaceLocalStructUsesAfterNeck(Module &module,
			map<pair<std::string, uint64_t>, uint64_t> &clocals,
			vector<Instruction*> instList);
};


#endif /* SOURCE_DIRECTORY__DEBLOAT_LOCALVARIABLES_H_ */
