#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/User.h"

#include "llvm/Support/raw_os_ostream.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm-c/ExecutionEngine.h"

#include "CleaningUpStuff.h"
#include "GlobalVariables.h"
#include "LocalVariables.h"
#include "Predicates.h"

#include <stdlib.h>

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "debloat"

cl::opt<string> GlobalsFile("globals",
		cl::desc("file containing global to constant mapping"));
cl::opt<string> PrimitiveLocalsFile("plocals",
		cl::desc("file containing primitive local to constant mapping"));
cl::opt<string> PtrToPrimitiveLocalsFile("ptrToPrimLocals",
		cl::desc("file containing pointer to primitive local to constant mapping"));
cl::opt<string> PtrStrctLocalsFile("ptrStructlocals",
		cl::desc("file containing customized local to constant mapping"));
cl::opt<string> StringVars("stringVars",
		cl::desc("file containing mapping of string variables"));
cl::opt<string> CustomizedLocalsFile("clocals",
		cl::desc("file containing customized local to constant mapping"));
cl::opt<string> BBFile("bbfile",
		cl::desc("file containing visited basic blocks"));

cl::opt<bool> SimplifyPredicates("simplifyPredicate", llvm::cl::desc(""),
		llvm::cl::init(false));

cl::opt<bool> CleaningUp("cleanUp",
		llvm::cl::desc("remove unused variables and functions"),
		llvm::cl::init(false));

namespace {

set<pair<string, uint64_t>> populateBasicBlocks() {
	set<pair<string, uint64_t>> res;
	ifstream ifs(BBFile.c_str());
	string fn;
	uint64_t bbnum;
	while (ifs >> fn >> bbnum) {
		res.emplace(fn, bbnum);
	}
	return res;
}

map<string, uint64_t> populateGobals() {
	map<string, uint64_t> res;
	ifstream ifs(GlobalsFile.c_str());
	string name;
	uint64_t value;
	std::string line;
	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			name = tmpVect[0];
			value = strtoul(tmpVect[1].c_str(), nullptr, 10);
			res.emplace(name, value);
		}
	}
	return res;
}

map<uint64_t, pair<uint64_t, uint64_t>> populatePtrPrimitiveLocals() {
	map<uint64_t, pair<uint64_t, uint64_t>> res;
	ifstream ifs(PtrToPrimitiveLocalsFile.c_str());
	uint64_t ptrIdx;
	uint64_t actualIdx;
	uint64_t value;
	std::string line;
	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			ptrIdx = strtoul(tmpVect[0].c_str(), nullptr, 10);
			actualIdx = strtoul(tmpVect[1].c_str(), nullptr, 10);
			value = strtoul(tmpVect[2].c_str(), nullptr, 10);
			res.emplace(ptrIdx, make_pair(actualIdx, value));
		}
	}
	return res;
}

map<uint64_t, pair<uint64_t, string>> populateStringVars() {
	map<uint64_t, pair<uint64_t, string>> res;
	ifstream ifs(StringVars.c_str());
	uint64_t ptrIdx;
	uint64_t actualIdx;
	string value;
	std::string line;
	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			ptrIdx = strtoul(tmpVect[0].c_str(), nullptr, 10);
			actualIdx = strtoul(tmpVect[1].c_str(), nullptr, 10);
			value = tmpVect[2];//strtoul(tmpVect[2].c_str(), nullptr, 10);
			res.emplace(ptrIdx, make_pair(actualIdx, value));
		}
	}
	return res;
}

map<string, uint64_t> populatePrimitiveLocals() {
	map<string, uint64_t> res;
	ifstream ifs(PrimitiveLocalsFile.c_str());
	std::string name;
	uint64_t value;
	std::string line;
	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			name = tmpVect[0];
			value = strtoul(tmpVect[1].c_str(), nullptr, 10);
			res.emplace(name, value);
		}
	}
	return res;
}

map <std::tuple<std::string, uint64_t, int>, uint64_t> populatePtrStrctLocals() {
	map <tuple<std::string, uint64_t, int>, uint64_t> ret;

	ifstream ifs(PtrStrctLocalsFile.c_str());
	uint64_t value, structElem;
	std::string line, structName;
	int allocIdx;

	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			structName = tmpVect[0];
			structElem = strtoul(tmpVect[1].c_str(), nullptr, 10);
			value = strtoul(tmpVect[2].c_str(), nullptr, 10);
			allocIdx = atoi(tmpVect[3].c_str());
			ret.emplace(make_tuple(structName, structElem, allocIdx), value);
		}
	}
	return ret;
}

map <pair<std::string, uint64_t>, uint64_t> populateStructLocals() {
	map <pair<std::string, uint64_t>, uint64_t> ret;

	ifstream ifs(CustomizedLocalsFile.c_str());
	uint64_t value, structElem;
	std::string line, structName;

	for (int i = 0; std::getline(ifs, line); i++) {
		if (line.find(" ") != std::string::npos) {
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			structName = tmpVect[0];
			structElem = strtoul(tmpVect[1].c_str(), nullptr, 10);
			value = strtoul(tmpVect[2].c_str(), nullptr, 10);
			ret.emplace(make_pair(structName, structElem), value);
		}
	}
	return ret;
}

string findTheNeck(Module &module){
	string funcName;

	for (auto fn = module.getFunctionList().begin();
			fn != module.getFunctionList().end(); fn++)
		for (auto bb = fn->getBasicBlockList().begin();
				bb != fn->getBasicBlockList().end(); bb++)
			for (auto I = bb->getInstList().begin();
					I != bb->getInstList().end(); I++)
				if (CallInst *callSite = dyn_cast<CallInst>(I))
					if (callSite->getCalledFunction()
							&& callSite->getCalledFunction()->getName()
							== "klee_dump_memory")
						funcName = fn->getName().str();
	return funcName;
}

/*
 * this method adds the missing basic blocks till reach the neck. as klee captures only the executed BBs
 */
void updateVisitedBasicBlocks(Module &module, set<pair<string, uint64_t>> &visitedBbs) {
	string name;
	int i;
	for (auto fn = module.getFunctionList().begin();
			fn != module.getFunctionList().end(); fn++) {
		name = fn->getName().str();
		i = 0;
		for (auto bb = fn->getBasicBlockList().begin();
				bb != fn->getBasicBlockList().end(); bb++) {
			for (auto I = bb->getInstList().begin();
					I != bb->getInstList().end(); I++) {
				if (CallInst *callSite = dyn_cast<CallInst>(I)) {
					if (callSite->getCalledFunction()
							&& callSite->getCalledFunction()->getName()
									== "klee_dump_memory") {
						goto updateBB;
					}
				}
			}
			i++;
		}
	}
	updateBB:
	for (int c = 0; c < i; c++) {
		visitedBbs.emplace(name, c);
	}
}


struct Debloat: public ModulePass {
	static char ID; // Pass identification, replacement for typeid
	Debloat() :
		ModulePass(ID) {
	}

	bool runOnModule(Module &module) override {
		set<pair<string, uint64_t>> visitedBbs = populateBasicBlocks();
		updateVisitedBasicBlocks(module, visitedBbs);
		map<string, uint64_t> globals = populateGobals();
		map<string, uint64_t> plocals = populatePrimitiveLocals();
		map<uint64_t, pair<uint64_t, uint64_t>> ptrPrimLocals = populatePtrPrimitiveLocals();
		map<uint64_t, pair<uint64_t, string>> strVars = populateStringVars();
		map <std::tuple<std::string, uint64_t, int>, uint64_t> ptrStrLocals = populatePtrStrctLocals();
		map <pair<std::string, uint64_t>, uint64_t> structLocals = populateStructLocals();

		string funcName = findTheNeck(module);
		outs() << "NECK is found: " << funcName <<"\n";

		LocalVariables lv;
		lv.initalizeInstList(module, funcName);

		if (globals.size() != 0) {
			outs() << "\nConvert global variables: " << globals.size() << "\n";
			GlobalVariables gv;
			gv.handleGlobalVariables(module, globals, visitedBbs, funcName);
		}

		if (plocals.size() != 0) {
			outs() << "\nConvert Primitive Locals: " << plocals.size() << "\n";
			lv.handlePrimitiveLocalVariables(module, plocals, funcName);
		}

		if (ptrPrimLocals.size() != 0){
			outs() << "\nConvert Pointer Primitive Locals: " << ptrPrimLocals.size() << "\n";
			lv.handlePtrToPrimitiveLocalVariables(module, ptrPrimLocals, funcName);
		}

		if (structLocals.size() != 0){
			outs() << "\nConvert Struct to Locals: " << structLocals.size() << "\n";
			lv.handleStructLocalVars(module, structLocals, funcName);
		}

		if (ptrStrLocals.size() != 0){
			outs() << "\nConvert pointer to struct Locals: " << ptrStrLocals.size() << "\n";
			lv.handlePtrToStrctLocalVars(module, ptrStrLocals, funcName);
		}

		if (strVars.size() != 0){
			outs() << "\nConvert string variables: " << strVars.size() << "\n";
			lv.handleStringVars(module, strVars, funcName);
		}

		SimplifyPredicates = true;
		if (SimplifyPredicates) {
			outs() << "Simplifying Predicates is enabled\n";
						Predicates p;
						p.handlePredicates(module);
		}

		if (CleaningUp) {
			CleaningUpStuff cp;
			cp.removeUnusedStuff(module);
		}

		//		lv.testing(module);

		return true;
	}
};
}

char Debloat::ID = 0;
static RegisterPass<Debloat> X("debloat", "Debloat Pass");
