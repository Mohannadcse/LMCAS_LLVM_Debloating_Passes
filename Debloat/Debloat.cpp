//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

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

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm-c/ExecutionEngine.h"

#include <map>
#include <set>
#include <string>
#include <fstream>
#include <cassert>

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "debloat"

cl::opt<string> GlobalsFile("globals", cl::desc("file containing global to constant mapping"));
cl::opt<string> LocalsFile("locals", cl::desc("file containing local to constant mapping"));
cl::opt<string> BBFile("bbfile", cl::desc("file containing visited basic blocks"));

cl::opt<bool> SimplifyPredicates("simplifyPredicate",
		llvm::cl::desc(""), llvm::cl::init(false));

cl::opt<bool> CleaningUp("cleanUp",
		llvm::cl::desc("remove unused variables and functions"), llvm::cl::init(false));

namespace {
typedef string Global;
typedef pair<string, uint64_t> BasicBlock;
typedef string Local;


vector<std::string> splitString(std::string& str, char delim){
	vector<std::string> strToVec;

	std::size_t current, previous = 0;
	current = str.find(delim);
	while (current != std::string::npos) {
		strToVec.push_back(str.substr(previous, current - previous));
		previous = current + 1;
		current = str.find(delim, previous);
	}
	strToVec.push_back(str.substr(previous, current - previous));
	return strToVec;
}


map<Global, uint64_t> populateGobals() {
	map<Global, uint64_t> res;
	ifstream ifs(GlobalsFile.c_str());
	Global name;
	uint64_t value;
	std::string line;
	for (int  i = 0; std::getline(ifs, line); i++){
		if(line.find(" ") != std::string::npos){
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			name = tmpVect[0];
			value = strtoul(tmpVect[1].c_str(),nullptr,10);
			res.emplace(name, value);
		}
	}
	return res;
}

map<Global, uint64_t> populateLocals() {
	map<Global, uint64_t> res;
	ifstream ifs(LocalsFile.c_str());
	Global name;
	uint64_t value;
	std::string line;
	for (int  i = 0; std::getline(ifs, line); i++){
		if(line.find(" ") != std::string::npos){
			splitString(line, ' ');
			auto tmpVect = splitString(line, ' ');
			name = tmpVect[0];
			value = strtoul(tmpVect[1].c_str(),nullptr,10);
			res.emplace(name, value);
		}
	}
	return res;
}

set<BasicBlock> populateBasicBlocks() {
	set<BasicBlock> res;
	ifstream ifs(BBFile.c_str());
	string fn;
	uint64_t bbnum;
	while (ifs >> fn >> bbnum) {
		res.emplace(fn, bbnum);
	}
	return res;
}


int returnIndex(std::vector<Instruction*>list, Instruction* inst){
	uint32_t i = -1;
	auto it = find(list.begin(), list.end(), inst);
	if (it != list.cend()){
		i = std::distance(list.begin(), it);
	}
	return i;
}

/*
 * this method adds the missing basic blocks till reach the neck. as klee captures only the executed BBs
 */
void updateVisitedBasicBlocks(Module &module, set<BasicBlock>& visitedBbs){
	string name;
	int i;
	for (auto fn = module.getFunctionList().begin(); fn != module.getFunctionList().end(); fn++){
		name = fn->getName().str();
		i =0;
		for (auto bb = fn->getBasicBlockList().begin(); bb != fn->getBasicBlockList().end(); bb++){
			for (auto I = bb->getInstList().begin(); I != bb->getInstList().end(); I++){
				if (CallInst* callSite = dyn_cast<CallInst>(I)){
					if (callSite->getCalledFunction() && callSite->getCalledFunction()->getName() == "klee_dump_memory"){
						goto updateBB;
					}
				}
			}
			i++;
		}
	}
	updateBB:
	for(int c = 0; c < i; c++){
		visitedBbs.emplace(name, c);
	}
}


void handleGlobalVariables(Module &module, map<Global, uint64_t>& globals){
	outs() << "Run handleGlobalVariables\n";
	set<BasicBlock> visitedBbs = populateBasicBlocks();
	updateVisitedBasicBlocks(module, visitedBbs);

	// identify globals in this module and delete the rest
	for (auto it = globals.cbegin(); it != globals.cend();) {
		if (module.getGlobalVariable(it->first, true))
			++it;
		else {
			it = globals.erase(it);
		}
	}

	// identify globals that cannot be made constant
	for (auto curF = module.getFunctionList().begin(),
			endF = module.getFunctionList().end();
			curF != endF; ++curF) {
		string fn = curF->getName();
		uint32_t bbnum = 0;
		for (auto curB = curF->begin(), endB = curF->end(); curB != endB; ++curB, ++bbnum) {
			if (visitedBbs.find(BasicBlock(fn, bbnum)) != visitedBbs.end()){
				continue;
			}
			auto curI = curB->begin(), endI = curB->end();
			while (curI != endI) {
				auto si = dyn_cast<StoreInst>(&(*curI));
				++curI;
				if (!si)
					continue;
				if (GlobalVariable* gvar = dyn_cast<GlobalVariable>(si->getPointerOperand())) {
					auto it = globals.find(gvar->getName());
					if (it != globals.end()){
						globals.erase(it);
					}
				}
			}
		}
	}
	llvm::outs()<< "Remaind Variables After 2nd iteration\n";
	for (auto&& kv : globals) {
		errs() << kv.first << " " << kv.second << "\n";
	}

	// make remaining globals constant
	for (auto curF = module.getFunctionList().begin(),
			endF = module.getFunctionList().end();
			curF != endF; ++curF) {
		string fn = curF->getName();

		uint32_t bbnum = 0;
		for (auto curB = curF->begin(), endB = curF->end(); curB != endB; ++curB, ++bbnum) {
			if (visitedBbs.find(BasicBlock(fn, bbnum)) != visitedBbs.end())
				continue;
			auto curI = curB->begin(), endI = curB->end();
			while (curI != endI) {
				if (auto li = dyn_cast<LoadInst>(&(*curI))) {
					if (GlobalVariable* gvar = dyn_cast<GlobalVariable>(li->getPointerOperand())) {
						auto it = globals.find(gvar->getName());
						if (it != globals.end()) {
							GlobalVariable* gvar = module.getGlobalVariable(it->first, true);
							assert(gvar);
							if (auto intType = dyn_cast<IntegerType>(gvar->getType()->getElementType())) {
								auto val = llvm::ConstantInt::get(intType, it->second);
								ReplaceInstWithValue(curB->getInstList(), curI, val);
							}
						}
					}
				}
				++curI;
			}
		}
	}
}

uint32_t neckIndex(Module &module, std::vector<Instruction*>&instList){
	uint32_t neckIdx = 0;
	outs() << "Find the index of the Neck\n";
	for (auto curF = module.getFunctionList().begin(),endF = module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main"){
			uint64_t i = 0;
			//identify the index of the neck
			for (auto curB = curF->begin(); curB != curF->end(); curB++){
				for (auto curI = curB->begin(); curI != curB->end(); curI++, i++){
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<llvm::CallInst>(curI)){
						if(cs->getCalledFunction()->getName() == "klee_dump_memory"){
							neckIdx = i;
							outs() << "Neck Found\n";
							return neckIdx;
						}
					}
				}
			}
		}
	}
	return neckIdx;
}

void handleLocalVariables(Module &module, map<Global, uint64_t>& locals){
	outs() << "Run handleLocalVariables\n";

	map<llvm::AllocaInst*, uint64_t> instrToIdx;
	std::vector<Instruction*>instList;
	uint32_t neckIdx = neckIndex(module, instList);

	outs() << "instList size:" << instList.size() <<"\n";

	outs() << "Find list of matching instructions that have index in locals.\n";
	for (auto curF = module.getFunctionList().begin(),endF = module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		//the assumption is that majority of the analysis on the local variables
		//should be conducted inside the main, this where the neck is
		if (fn == "main"){
			outs() << "Found main method\n";
			//			map<llvm::AllocaInst*, uint64_t> instrToIdx;
			//			std::vector<Instruction*>instList;

			//Get mapping between allocation instruction and its index in the locals file
			//this step is necessary to identify the corresponding value to each local variable
			//local variables are alloca instructions
			uint64_t i =0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++){
				for (auto curI = curB->begin(); curI != curB->end(); curI++){
					auto id = locals.find(std::to_string(i));
					if (auto al = dyn_cast<llvm::AllocaInst>(curI)){
						if(id != locals.end())
							instrToIdx.emplace(cast<llvm::AllocaInst>(curI), i);
					}
					i++;
				}
			}
		}
	}

	outs() << "Filter the locals map\n";
	for (auto curF = module.getFunctionList().begin(),endF = module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main"){
			for (auto curB = curF->begin(); curB != curF->end(); curB++){
				for (auto curI = curB->begin(); curI != curB->end(); curI++){
					//use-iterator can be used to iterate all use of the local variables. Then we can use
					//use-iterator and users iteration yield the same results
					if (auto allc = dyn_cast<llvm::AllocaInst>(curI)){
						//							llvm::outs() << "allcInst:: " <<*allc << "\n";
						auto it = instrToIdx.find(allc);
						//if the intruction in the list of local variables
						if (it != instrToIdx.end()){
							//iterate the users of the local variable
							for (llvm::Value::use_iterator ui = curI->use_begin(); ui != curI->use_end(); ui++){
								//									llvm::outs() << "use:: " << *ui->getUser()<<"\n";
								//find the index of the instruction to check if before or after neck,
								//if after erase the record from instrToIdx and locals
								if(auto si = dyn_cast<llvm::StoreInst>(ui->getUser())){
									uint32_t elem = returnIndex(instList, si);
									if (elem != -1 && elem > neckIdx){
										auto loc = locals.find(std::to_string(it->second));
										if(loc != locals.end()){
											locals.erase(loc);
										instrToIdx.erase(it);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}


	outs()<< "Found instructions and their indices\n";
	for (auto t : instrToIdx){
		auto it = locals.find(std::to_string(t.second));
		if (it != locals.end())
			llvm:outs() << "LIST: " << *t.first << " --- " << t.second<< "\n";
	}

	//convert to constant
	for (auto curF = module.getFunctionList().begin(),endF = module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main"){
			outs()<< "Start Converting to constant\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++){
				for (auto curI = curB->begin(); curI != curB->end(); curI++){
					if (auto ld = dyn_cast<llvm::LoadInst>(curI)){
						if(auto opr = dyn_cast<llvm::AllocaInst>(ld->getPointerOperand())){
							//here I need to check the mapping list of local variables and their values
							auto inst = instrToIdx.find(opr);
							if (inst != instrToIdx.end()){
								auto constVal = locals.find(std::to_string(inst->second));
								if (constVal != locals.end()){
									if(auto intType = dyn_cast<IntegerType>(opr->getType()->getElementType())){
										auto val = llvm::ConstantInt::get(intType, constVal->second);
										//llvm::outs() << "REPLACED DONE.. \n";
										//llvm::outs() << "FOUND: "<<  *inst->first << " :: " << inst->second << " :: " << constVal->first <<"\n";
										ReplaceInstWithValue(curB->getInstList(), curI, val);
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

//This method removes unused local/global variables and functions
void cleaningUp(Module &module){
	std::vector<Function*> funcToBeRemoved;
	std::vector<GlobalVariable*> gblVarsToBeRemoved;
	std::vector<Instruction*> localVarsToBeRemoved;
	std::vector<Instruction*> storeInstToBeRemoved;
	//identify uses of a function, if it's zero then remove the function
	for (auto curF = module.getFunctionList().begin(); curF != module.getFunctionList().end(); curF++){
		if(curF->getName() != "main"){
			//			outs() << "FuncName: " << curF->getName() << " NumUses= " << curF->getNumUses() << "\n";
			if (curF->getNumUses() == 0)
				funcToBeRemoved.push_back(&*curF);
		}
	}

	//loop over local vars in the functions that won't be removed
	for (auto curF = module.getFunctionList().begin(); curF != module.getFunctionList().end(); curF++){
		if(std::find(funcToBeRemoved.begin(), funcToBeRemoved.end(), &*curF) != funcToBeRemoved.end()){
			continue;
		} else {
			//				outs() << "FuncName: " << curF->getName() << "\n";
			//remove alloc inst that only used once in a store instr.
			//the alloc inst should be the 2nd operand of the store instr
			//I created 2 Instruction vectors: aloc instrs and stor instr. Because I need to remove store instrs before alloc instr
			//otherwise, I'll receive errors if the alloc was removed before its store insr
			for (auto I = inst_begin(*curF); I!= inst_end(*curF); I++){
				Instruction* i = &*I;
				if(auto ai = dyn_cast<AllocaInst>(i)){
					//						outs() << "Var: " << *ai << " , Used = " << ai->getNumUses() << "\n";
					for (auto u : ai->users()){
						//							outs() << "\tUses: " << *u <<"\n";
						if (ai->getNumUses() == 1 && isa<StoreInst>(u)){
							if (u->getOperand(1) == ai){
								localVarsToBeRemoved.push_back(i);
								storeInstToBeRemoved.push_back(dyn_cast<Instruction>(u));
							}
						}
					}
				}
			}
		}
	}


	for (auto curG = module.getGlobalList().begin(); curG != module.getGlobalList().end(); curG++){
		//		outs() << "gblName: " << curG->getName() << " NumUses= " << curG->getNumUses() << "\n";
		if (curG->getNumUses() == 0)
			gblVarsToBeRemoved.push_back(&*curG);
	}

	///the following 4 for-loops remove the identified unused variables and functions
	for (auto f : funcToBeRemoved){
		f->eraseFromParent();
	}

	for (auto g : gblVarsToBeRemoved){
		g->eraseFromParent();
	}

	/*
	outs() << "localVarsToBeRemoved: " << localVarsToBeRemoved.size() << "\n";
	outs() << "storeInstToBeRemoved: " << storeInstToBeRemoved.size() << "\n";
	 */

	for (auto str : storeInstToBeRemoved){
		str->eraseFromParent();
	}

	for (auto l : localVarsToBeRemoved){
		l->eraseFromParent();
	}

}

void handlePredicates(Module &module){
	for (auto curF = module.getFunctionList().begin(),
			endF = module.getFunctionList().end();
			curF != endF; ++curF) {
		for (auto curB = curF->begin(); curB != curF->end(); curB++){
			for (auto pi = curB->begin(); pi != curB->end(); pi++){
				if(auto icmp = dyn_cast<ICmpInst>(pi)){
					outs() << "icmp: " << *icmp << " : " << icmp->getNumOperands() << "\n";
					if (isa<ConstantInt>(icmp->getOperand(0)) && isa<Constant>(icmp->getOperand(1))){
						auto c1 = cast<ConstantInt>(icmp->getOperand(0));
						auto c2 = cast<ConstantInt>(icmp->getOperand(1));
						outs() << "\tc1: " << c1->getSExtValue() << " :: C2=" << c2->getSExtValue() << "\n";

						switch(icmp->getPredicate()) {
						case ICmpInst::ICMP_EQ: {
							//always false
							if (c1->getSExtValue() != c2->getSExtValue()){
								if(auto bi = dyn_cast<BranchInst>(icmp->getNextNode())){//the assumption branch instr is always after ICMP instr
									outs() << "\tBR: "<< *bi << " succ: " << bi->getNumSuccessors() << " :: oprds: " << bi->getNumOperands() << "\n";
									outs() << "\tsucc=s: " << *bi->getSuccessor(0) <<"\n";
									outs() << "\tsucc!=: " << *bi->getSuccessor(1) <<"\n";//icmp->getPredicate() != ICmpInst::ICMP_EQ
									auto tmpBI = BranchInst::Create(bi->getSuccessor(1), &*curB);
									outs() << "tmpBI: "<< *tmpBI << "\n";
									bi->eraseFromParent();
									bi->getSuccessor(0)->eraseFromParent();

									//									bi->isConditional()
									//									if (const CmpInst *CI = dyn_cast<CmpInst>(bi->getCondition()))
									//										outs() << "CI: " << *CI<< "\n";
								}
							} else {//always true

							}
							break;
						}

						case ICmpInst::ICMP_NE: {
							outs() << "\tnot equal" << "\n";
							if (isa<ConstantInt>(icmp->getOperand(0)) && isa<Constant>(icmp->getOperand(1))){

							}
							break;
						}
						default:
							outs() << "Error\n";
						}
					}
				}
			}
		}
	}
}


struct Debloat : public ModulePass {
	static char ID; // Pass identification, replacement for typeid
	Debloat() : ModulePass(ID) {}

	bool runOnModule(Module &module) override {
		map<Global, uint64_t> globals = populateGobals();
		map<Local, uint64_t> locals = populateLocals();

		if(globals.size() != 0){
			handleGlobalVariables(module, globals);
		}

		if(locals.size() != 0){
			outs() << "Sizeof Locals: " << locals.size() <<"\n";
			handleLocalVariables(module, locals);
		}

		if(SimplifyPredicates) {
			outs() << "Simplifying Predicates is enabled\n";
			//			handlePredicates(module);
		}

		if(CleaningUp){
			cleaningUp(module);
		}
		return true;
	}
};
}

char Debloat::ID = 0;
static RegisterPass<Debloat> X("debloat", "Debloat Pass");


/*iterates the operands of curI
	llvm::outs() << "INSTR:: " << *curI <<"\n";
	for(llvm::User::op_iterator i = curI->op_begin(); i != curI->op_end(); i++){
		llvm::outs() << "op_user: "<< *i->get() <<"\n";
	}
 */

/*iterates the instructions that use ui
 * for (llvm::Value::use_iterator ui = curI->use_begin(); ui != curI->use_end(); ui++){
 *
 * }
 * OR
 * for(auto i : alloc->users()){
	llvm::outs() << "USERS:: " << *i << "\n";
   }
 */

/*
 * count number of instructions in a function
 * unsigned int instCount = 0;
				for (const llvm::BasicBlock &bb : curF->getFunction()){
					instCount += std::distance(bb.begin(), bb.end());
				}
				llvm::outs() << "InstNum:: "<<instCount << "\n";
 */

/*
if(auto st = dyn_cast<llvm::StoreInst>(curI)){
	llvm::outs() << "st: " << *st << " :Opr= " << *st->getOperand(0)->getType() <<"\n";
	if(auto opr = dyn_cast<llvm::AllocaInst>(st->getOperand(1))){
		auto inst = indexToInstr.find(opr);
		if (inst != indexToInstr.end()){
			auto constVal = locals.find(std::to_string(inst->second));
			if (constVal != locals.end()){
				if(auto intType = st->getOperand(0)->getType()){
					auto val = llvm::ConstantInt::get(intType, constVal->second);
					llvm::outs() << "REPLACED DONE.. \n";
					llvm::outs() << "FOUND: "<<  *inst->first << " :: " << inst->second << " :: " << constVal->first <<"\n";
					ReplaceInstWithValue(curB->getInstList(), curI, val);
				}
			}
		}
	}
}
 */
