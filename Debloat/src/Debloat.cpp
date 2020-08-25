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
//#include "llvm/IR/DerivedTypes.h"
//#include "llvm/IR/Type.h"

#include "llvm/Support/raw_os_ostream.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm-c/ExecutionEngine.h"

#include "CleaningUpStuff.h"
#include "GlobalVariables.h"
#include "LocalVariables.h"
#include "Predicates.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "debloat"

cl::opt<string> GlobalsFile("globals",
		cl::desc("file containing global to constant mapping"));
cl::opt<string> PrimitiveLocalsFile("plocals",
		cl::desc("file containing primitive local to constant mapping"));
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

map <pair<std::string, uint64_t>, uint64_t> populateCustomizedLocals() {
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

/*
 * this method adds the missing basic blocks till reach the neck. as klee captures only the executed BBs
 */
/*
void updateVisitedBasicBlocks(Module &module, set<BasicBlock> &visitedBbs) {
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
	updateBB: for (int c = 0; c < i; c++) {
		visitedBbs.emplace(name, c);
	}
}*/


struct Debloat: public ModulePass {
	static char ID; // Pass identification, replacement for typeid
	Debloat() :
			ModulePass(ID) {
	}

	bool runOnModule(Module &module) override {
		set<pair<string, uint64_t>> visitedBbs = populateBasicBlocks();
		map<string, uint64_t> globals = populateGobals();
		map<string, uint64_t> plocals = populatePrimitiveLocals();
		map <pair<std::string, uint64_t>, uint64_t> clocals = populateCustomizedLocals();


		if (globals.size() != 0) {
			GlobalVariables gv;
			gv.handleGlobalVariables(module, globals, visitedBbs);
		}

		if (plocals.size() != 0) {
			LocalVariables lv;
			outs() << "\nSizeof Primitive Locals: " << plocals.size() << "\n";
			lv.handlePrimitiveLocalVariables(module, plocals);
		}

		if (clocals.size() != 0){
			LocalVariables lv;
			outs() << "\nSizeof Customized Locals: " << clocals.size() << "\n";
			//lv.handleCustomizedLocalVariables(module, clocals);

		}

		if (SimplifyPredicates) {
			outs() << "Simplifying Predicates is enabled\n";
//			Predicates p;
//			p.handlePredicates(module);
		}

		if (CleaningUp) {
			CleaningUpStuff cp;
			cp.removeUnusedStuff(module);
		}
		return true;
	}
};
}

char Debloat::ID = 0;
static RegisterPass<Debloat> X("debloat", "Debloat Pass");
