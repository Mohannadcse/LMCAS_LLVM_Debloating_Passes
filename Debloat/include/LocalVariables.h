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
#include "llvm/IR/InstIterator.h"

#include <set>

#include "Utility.h"

using namespace llvm;
using namespace std;


class LocalVariables {
private:
	typedef tuple <bool, vector<LoadInst*>, uint64_t> postNeckGepInfo;
	ofstream logger;
	vector<Instruction*> instList;

	void handleLocalPrimitiveUsesAfterNeck(Module &module, map<string, uint64_t> &plocals,
			map<AllocaInst*, uint64_t> instrToIdx, vector<Instruction*> instList,
			map<AllocaInst*, string> instrToVarName);
	void handleLocalStructUsesAfterNeck(Module &module,
			map<pair<std::string, uint64_t>, uint64_t> &clocals,
			vector<Instruction*> instList);
	void replaceStructPostNeck(vector<pair<GetElementPtrInst*, postNeckGepInfo>> gepInfo);
	void inspectInitalizationPreNeck(Module& module, vector<Instruction*> instList,
			map<pair<string, uint64_t>, uint64_t> &clocals);
	void handleStructInOtherMethods(Function* fn, map<pair<string, uint64_t>, uint64_t> &clocals);

public:
	void testing(Module&);
	void handlePrimitiveLocalVariables(Module &module, map<string, uint64_t> &plocals);
	void handleCustomizedLocalVariables(Module &module, map <pair<string, uint64_t>, uint64_t> &clocals);
	void handlePtrToPrimitiveLocalVariables(Module &module,
			map<uint64_t, pair<uint64_t, uint64_t>> &ptrToPrimtive);
	void initalizeInstList(Module &module);
	LocalVariables() {
		logger.open("logger.txt", ofstream::app);
	}
//	~LocalVariables(){
//		logger << strLogger.str();
//	}
};


#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_ */
