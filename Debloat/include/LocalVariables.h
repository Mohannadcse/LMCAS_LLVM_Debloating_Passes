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

	void handleLocalPrimitiveUsesAfterNeck(Module &, map<string, uint64_t> &,
			map<AllocaInst*, uint64_t>, vector<Instruction*>, map<AllocaInst*, string>, string);
	void handlePtrLocalStructUsesAfterNeck(Module &,
			map<tuple<string, uint64_t, int>, uint64_t> &, int &, string);
	void handleLocalStructUsesAfterNeck(Module &,
			map <pair<string, uint64_t>, uint64_t> &, int &, string);
	void replaceStructPostNeck(vector<pair<GetElementPtrInst*, postNeckGepInfo>> );
	void inspectInitalizationPreNeck(Module&, vector<Instruction*>,
			map<tuple<string, uint64_t, int>, uint64_t> &, int &, string);
	void handleStructInOtherMethods(Function*, map<tuple<string, uint64_t, int>, uint64_t> &, int &);

	bool processGepInstrStruct(llvm::GetElementPtrInst *gep,
			tuple<string, uint64_t, int> structInfo);
	bool processGepInstrPtrStruct(llvm::GetElementPtrInst *gep,
			tuple<string, uint64_t, int> structInfo);
	bool processGepInstrNestedStruct(llvm::GetElementPtrInst *mainGEP, llvm::GetElementPtrInst *elemGEP,
			tuple<string, string, uint64_t, uint64_t, int> structInfo, int flag);

	void constantConversionStrctVars(Module &, GetElementPtrInst*, string, uint64_t, raw_string_ostream &, int cntxtFlg);

public:
	void testing(Module&);
	void handleStringVars(Module&, map<uint64_t, pair<uint64_t, string>>, string);
	void handlePrimitiveLocalVariables(Module &, map<string, uint64_t> &, string);
	void handleStructLocalVars(Module &, map <tuple<string, uint64_t, int>, uint64_t>&, string);
	void handlePtrToStrctLocalVars(Module &, map <tuple<string, uint64_t, int>, uint64_t> &, string);
	void handlePtrToPrimitiveLocalVariables(Module &, map<uint64_t, pair<uint64_t, uint64_t>>&, string);
	void handleNestedStrct(Module &, map <tuple<string, string, uint64_t, uint64_t, int>, uint64_t>&, string);
	void handleNestedStrctPtr(Module &, map <tuple<string, string, uint64_t, uint64_t, int>, uint64_t>&, string);

	void initalizeInstList(Module &, string);
	LocalVariables() {
		logger.open("logger.txt", ofstream::app);
	}
};


#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_ */
