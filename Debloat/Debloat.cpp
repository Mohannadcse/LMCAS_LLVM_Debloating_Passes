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
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/User.h"
//#include "llvm/IR/DerivedTypes.h"
//#include "llvm/IR/Type.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm-c/ExecutionEngine.h"

#include <map>
#include <set>
#include <string>
#include <fstream>
#include <cassert>
#include <vector>

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "debloat"

cl::opt<string> GlobalsFile("globals",
		cl::desc("file containing global to constant mapping"));
cl::opt<string> LocalsFile("locals",
		cl::desc("file containing local to constant mapping"));
cl::opt<string> BBFile("bbfile",
		cl::desc("file containing visited basic blocks"));

cl::opt<bool> SimplifyPredicates("simplifyPredicate", llvm::cl::desc(""),
		llvm::cl::init(false));

cl::opt<bool> CleaningUp("cleanUp",
		llvm::cl::desc("remove unused variables and functions"),
		llvm::cl::init(false));

namespace {
typedef string Global;
typedef pair<string, uint64_t> BasicBlock;
typedef string Local;

vector<std::string> splitString(std::string &str, char delim) {
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

map<Local, uint64_t> populateLocals() {
	map<Local, uint64_t> res;
	ifstream ifs(LocalsFile.c_str());
	Local name;
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

int returnIndex(std::vector<Instruction*> list, Instruction *inst) {
	uint32_t i = -1;
	auto it = find(list.begin(), list.end(), inst);
	if (it != list.cend()) {
		i = std::distance(list.begin(), it);
	}
	return i;
}

uint32_t neckIndex(Module &module, std::vector<Instruction*> &instList) {
	instList.clear();
	uint32_t neckIdx = 0;
//	outs() << "Find the index of the Neck\n";
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main") {
			uint64_t i = 0;
			//identify the index of the neck
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end();
						curI++, i++) {
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<llvm::CallInst>(curI)) {
						if (cs->getCalledFunction()->getName()
								== "klee_dump_memory") {
							neckIdx = i;
							//outs() << "Neck Found@: " << neckIdx << "---Size: " << instList.size() << "\n";
							return neckIdx;
						}
					}
				}
			}
		}
	}
	return neckIdx;
}

/*
 * this method adds the missing basic blocks till reach the neck. as klee captures only the executed BBs
 */
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
}

void handleGlobalVariables(Module &module, map<Global, uint64_t> &globals) {
	outs() << "Run handleGlobalVariables\n";
	set<BasicBlock> visitedBbs = populateBasicBlocks();
//	updateVisitedBasicBlocks(module, visitedBbs);

// identify globals in this module and delete the rest
	for (auto it = globals.cbegin(); it != globals.cend();) {
		if (module.getGlobalVariable(it->first, true))
			++it;
		else {
			it = globals.erase(it);
		}
	}

	llvm::outs() << "Remaind Variables After 1st iteration\n";
	for (auto &&kv : globals) {
		errs() << kv.first << " " << kv.second << "\n";
		auto it = globals.find("optind");
		if (it != globals.end()) {
			outs() << "Remove optind" << "\n";
			globals.erase(it);
		}
	}

	map<Global, uint64_t> newGlobals;
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		string fn = curF->getName();
		if (fn == "main") {
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); curI++) {
					if (auto si = dyn_cast<StoreInst>(&(*curI))) {
						if (GlobalVariable *gvar = dyn_cast<GlobalVariable>(
								si->getPointerOperand())) {
							auto it = globals.find(gvar->getName());
							if (it != globals.end()) {
								newGlobals.insert(*it);
							}
						}
					} else if (auto li = dyn_cast<LoadInst>(&(*curI))) {
						if (GlobalVariable *gvar = dyn_cast<GlobalVariable>(
								li->getPointerOperand())) {
							auto it = globals.find(gvar->getName());
							if (it != globals.end()) {
								newGlobals.insert(*it);
							}
						}
					}
				}
			}
		}
	}
	llvm::outs() << "Remaind Variables After 2nd iteration\n";
	for (auto &&kv : newGlobals) {
		errs() << kv.first << " " << kv.second << "\n";
	}

// make remaining globals constant
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();

		uint32_t bbnum = 0;
		for (auto curB = curF->begin(), endB = curF->end(); curB != endB;
				++curB, ++bbnum) {
			if (visitedBbs.find(BasicBlock(fn, bbnum)) != visitedBbs.end())
				continue;
			auto curI = curB->begin(), endI = curB->end();
			while (curI != endI) {
				if (auto li = dyn_cast<LoadInst>(&(*curI))) {
					if (GlobalVariable *gvar = dyn_cast<GlobalVariable>(
							li->getPointerOperand())) {
						auto it = newGlobals.find(gvar->getName());
						if (it != newGlobals.end()) {
							GlobalVariable *gvar = module.getGlobalVariable(
									it->first, true);
							assert(gvar);
							if (auto intType = dyn_cast<IntegerType>(
									gvar->getType()->getElementType())) {
								auto val = llvm::ConstantInt::get(intType,
										it->second);
								//								llvm::outs() << "\tFOUND: " << it->first
								//																				<< " :: " << it->second
								//																				<< " :: " << *curI << "\n";
								ReplaceInstWithValue(curB->getInstList(), curI,
										val);
							}
						}
					}
				}
				++curI;
			}
		}
	}
}

map<uint64_t, Instruction*> getAllInstr(Module &module) {
	map<uint64_t, Instruction*> insts;
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main") {
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end();
						curI++, i++) {
					Instruction *inst = &*curI;
					insts.emplace(i, inst);
				}
			}
		}
	}
	return insts;
}

/*1- Check getelementptr instructions after the neck
 *2- check if the same struct name and same element index
 *3- If all are matching, then check all uses of the getelementptr and store them
 *in a vector.
 *4- if all use instructions are load, then we can convert them into constants
 */
void replaceLocalStructUsesAfterNeck(Module &module,
		map<GetElementPtrInst*, uint64_t> structToIdx,
		std::vector<Instruction*> instList, map<Global, uint64_t> &locals) {
	for (auto srctGep : structToIdx) {

		auto srctGepType = dyn_cast<PointerType>(
				(srctGep.first)->getOperand(0)->getType());
		auto srctGepTypeStrct = dyn_cast<StructType>(
				srctGepType->getElementType());

		outs() << "***" << *srctGepTypeStrct << "\n";

		std::vector<LoadInst*> loadInstUseGep;

		bool isThereStoreInst = 0;

		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == "main")
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<llvm::GetElementPtrInst>(
								curI)) {
							if (returnIndex(instList, gep)
									> neckIndex(module, instList)) {
								auto opr0Type = dyn_cast<PointerType>(
										gep->getOperand(0)->getType());
								auto opr0Instr = dyn_cast<AllocaInst>(
										gep->getOperand(0));
								if (opr0Type && opr0Instr)
									if (auto pt = dyn_cast<StructType>(
											opr0Type->getElementType()))
										if (pt->getStructName()
												== srctGepTypeStrct->getStructName()
												&& gep->getNumOperands()
														== srctGep.first->getNumOperands()
												&& gep->getOperand(2)
														== srctGep.first->getOperand(
																2)) {
											outs() << "GEP: " << *gep
													<< "---Oprds: "
													<< gep->getNumOperands()
													<< "\n";
											outs() << "GEP_Opr3: "
													<< *gep->getOperand(2)
													<< "\n";
											for (auto i : gep->users()) {
												if (isa<StoreInst>(i)) {
													isThereStoreInst = 1;
													break;
												} else if (auto ld = dyn_cast<
														LoadInst>(i)) {
													loadInstUseGep.push_back(
															ld);
												}
											}
										}
							}
						}
					}
				}
		}

		//if all loadinstr, then I can perform constant conversion
		if (!isThereStoreInst) {
			for (auto ld : loadInstUseGep) {
				outs() << "***Replacing Load Intructions...\n";
				outs() << "getPointerOperand: " << *ld->getOperand(0)->getType()
						<< "\n";
				auto constVal = locals.find(std::to_string(srctGep.second));
				if (constVal != locals.end()) {
					if (auto intType = dyn_cast<IntegerType>(
							ld->getPointerOperand()->getType()->getPointerElementType())) {
						auto val = llvm::ConstantInt::get(intType,
								constVal->second);
						outs() << "\t****FOUND CONST INST\n";
						outs() << "Obtain BB:: " << ld->getParent()->getInstList().size() << "\n";
						outs() << "intType:  " << *intType << "\n";
						ReplaceInstWithValue(ld->getParent()->getInstList(), ld, val);
					}
				}
			}
		}
	}
}

void handleLocalVariables(Module &module, map<Global, uint64_t> &locals) {
	outs() << "Run handleLocalVariables\n";
	set<BasicBlock> visitedBbs = populateBasicBlocks();
	map<llvm::AllocaInst*, uint64_t> instrToIdx;
	map<GetElementPtrInst*, uint64_t> structToIdx;
	std::vector<Instruction*> instList;

	map<uint64_t, Instruction*> mapIdxInst = getAllInstr(module);

	map<llvm::AllocaInst*, std::string> instrToVarName;

	outs() << "Find list of matching instructions that have index in locals.\n";
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		//the assumption is that majority of the analysis on the local variables
		//should be conducted inside the main, this where the neck is
		if (fn == "main") {
			//Get mapping between allocation instruction and its index in the locals file
			//this step is necessary to identify the corresponding value to each local variable
			//local variables are alloca instructions
			//I also need to consider struct variables, because I noticed they are used in the coreutils
			//Therefore, I need to consider getelementptr because this instr points the struct variable
			//I noticed the index of instructions isn't a reliable approach, because in the LLVM pass we consider the entire
			//bitcode, but KLEE interpreter doesn't maintain the order of the instructions
			//one hack I found, in KLEE there is no 2 consecutive br instructions
			//Therefore, I when I reach a branch instr:
			//1- check if there is a br instr before it
			//2- if so, reduce i by one
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); ++curI) {
					//to keep the index i consistent and matching the one in KLEE
					//outs() <<"i: " << i << " --- " << *curI<< "\n";
					if (auto br = dyn_cast<BranchInst>(&*curI)) {
						//check if the previous instr is br
						auto inst = mapIdxInst.find(i - 1);
						if (inst != mapIdxInst.end()
								&& isa<BranchInst>(inst->second)) {
							//outs() << "****Found TWO Branches\n";
							--i;
						}
					}

					auto id = locals.find(std::to_string(i));
					if (auto al = dyn_cast<llvm::AllocaInst>(curI)) {
						if (id != locals.end())
							instrToIdx.emplace(cast<llvm::AllocaInst>(curI), i);
						//obtain the names of local variables, if applicable
						if (!al->hasName()) {
							outs() << *al << "\n";
							for (auto I = curB->begin(); I != curB->end();
									I++) {
								if (DbgDeclareInst *dbg = dyn_cast<
										DbgDeclareInst>(&(*I))) {
									if (const AllocaInst *dbgAI = dyn_cast<
											AllocaInst>(dbg->getAddress()))
										if (dbgAI == al) {
											if (DILocalVariable *varMD =
													dbg->getVariable()) {
												outs() << "VarName: "
														<< varMD->getName().str()
														<< "\n";
												instrToVarName.emplace(
														cast<llvm::AllocaInst>(
																curI),
														varMD->getName().str());
											}
										}
								}
							}
						} else {
							outs() << "NAME:: " << al->getName() << "\n";
						}
						//Local variable can be struct, and GetElementPtrInst is used to point to struct elements
					} else if (auto gep = dyn_cast<GetElementPtrInst>(curI)) {
						if (id != locals.end()) {
							auto pt = dyn_cast<PointerType>(
									gep->getOperand(0)->getType());
							if (pt && isa<StructType>(pt->getElementType()))
								structToIdx.emplace(gep, i);
						}
					}
					i++;
				}
			}
		}
	}

	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main") {
			outs() << "Start Converting to constant\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); curI++) {
					//i need to add if store isnt and 1)its 2nd operand is alloc instr and 2)1st operand is constvalue, the
					//do the replacement
					if (auto ld = dyn_cast<llvm::LoadInst>(curI)) {
						if (returnIndex(instList, ld)
								< neckIndex(module, instList))//do the conversion till the neck
							if (auto opr = dyn_cast<llvm::AllocaInst>(
									ld->getPointerOperand())) {
								//I need to ignore some local variables like argc, because the number of arguments will be
								//different after running the specalized apps. So argc variable should be delayed
								auto varName = instrToVarName.find(opr);
								if (varName != instrToVarName.end())
									if (varName->second == "argc")
										continue;

								//here I need to check the mapping list of local variables and their values
								auto inst = instrToIdx.find(opr);
								if (inst != instrToIdx.end()) {
									auto constVal = locals.find(
											std::to_string(inst->second));
									if (constVal != locals.end()) {
										if (auto intType =
												dyn_cast<IntegerType>(
														opr->getType()->getElementType())) {
											auto val = llvm::ConstantInt::get(
													intType, constVal->second);
											llvm::outs() << "\nLD replace.. \n";
											llvm::outs() << "\tFOUND: "
													<< *inst->first << " :: "
													<< inst->second << " :: "
													<< *curI << "\n";
											outs() << "intType: "<< *intType << "\n";
											ReplaceInstWithValue(
													curI->getParent()->getInstList(), curI,
													val);
										}
									}
								}
							}
					} else if (auto si = dyn_cast<llvm::StoreInst>(curI)) {
						if (returnIndex(instList, si)
								< neckIndex(module, instList)) {
							//							outs() << "SI:  " << *si << "\n";
							//							outs() << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

							if (isa<ConstantInt>(si->getOperand(0)))
								if (auto opr = dyn_cast<llvm::AllocaInst>(
										si->getOperand(1))) {

									auto inst = instrToIdx.find(opr);
									if (inst != instrToIdx.end()) {
										auto constVal = locals.find(
												std::to_string(inst->second));
										if (constVal != locals.end()) {
											if (auto intType =
													dyn_cast<IntegerType>(
															opr->getType()->getElementType())) {
												auto val =
														llvm::ConstantInt::get(
																intType,
																constVal->second);
												llvm::outs()
														<< "\nSI replace.. \n";
												llvm::outs() << "\tFOUND: "
														<< *inst->first
														<< " :: "
														<< inst->second
														<< " :: " << *curI
														<< "\n";

												StoreInst *str = new StoreInst(
														val,
														curI->getOperand(1));
												ReplaceInstWithInst(
														curB->getInstList(),
														curI, str);
												outs() << "\tAfter conv: "
														<< *curI << "\n";
											}
										}
									}
								}
						}
					}

					/* I need to decide which operand that will be the allocate instr (I noticed I need to check only operand 0)
					 * I need to do it now to handle only structs, when GetElementPtrInst is used to compute the address of struct
					 * the number of arguments is 4. getelementptr t* %val, t1 idx1, t2 idx2
					 */
					else if (auto gep = dyn_cast<llvm::GetElementPtrInst>(
							curI)) {
						if (returnIndex(instList, gep)
								< neckIndex(module, instList)) {

							//the 1st operand should be pointing to a local variable (AllocInstr)
							auto opr0Type = dyn_cast<PointerType>(
									gep->getOperand(0)->getType());
							auto opr0Instr = dyn_cast<AllocaInst>(
									gep->getOperand(0));

							if (opr0Type && opr0Instr) {
								if (auto pt = dyn_cast<StructType>(
										opr0Type->getElementType())) {
									//find if the struct points to the captured struct  local variables (structToIdx)
									auto it = structToIdx.find(gep);
									if (it != structToIdx.end()) {
										/*outs() << "INSTR: " << *curI
										 << " --NumUsers: "
										 << curI->getNumUses()
										 << " --NumOperands: "
										 << curI->getNumOperands()
										 << "\n";*/
										outs()
												<< "\t\tFOUND struct points to a local variable.: "
												<< pt->getStructName() << "\n";

										auto constVal = locals.find(
												std::to_string(it->second));
										if (constVal != locals.end()) {
											//I need to check if this stuct element wasn't modified after the neck (only loads), then
											//I need to convert to constVal.
											//Usually, there is load or store after the getelementptr
											for (auto i : gep->users()) {
												outs() << "\tuser: " << *i
														<< "\n";
												//if load

												//if store
												if (auto si =
														dyn_cast<StoreInst>(
																i)) {
													if (auto ci = dyn_cast<
															ConstantInt>(
															si->getOperand(
																	0))) {
														auto val =
																llvm::ConstantInt::get(
																		si->getOperand(
																				0)->getType(),
																		constVal->second);

														llvm::outs()
																<< "\nSI replace.. \n";

														StoreInst *str =
																new StoreInst(
																		val,
																		si->getOperand(
																				1));
														ReplaceInstWithInst(si,
																str);
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
			}
		}
	}
	replaceLocalStructUsesAfterNeck(module, structToIdx, instList, locals);
}

//This method removes unused local/global variables and functions
void cleaningUp(Module &module) {
	std::vector<Function*> funcToBeRemoved;
	std::vector<GlobalVariable*> gblVarsToBeRemoved;
	std::vector<Instruction*> localVarsToBeRemoved;
	std::vector<Instruction*> storeInstToBeRemoved;
//identify uses of a function, if it's zero then remove the function
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		if (curF->getName() != "main") {
			//			outs() << "FuncName: " << curF->getName() << " NumUses= " << curF->getNumUses() << "\n";
			if (curF->getNumUses() == 0)
				funcToBeRemoved.push_back(&*curF);
		}
	}

//loop over local vars in the functions that won't be removed
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		if (std::find(funcToBeRemoved.begin(), funcToBeRemoved.end(), &*curF)
				!= funcToBeRemoved.end()) {
			continue;
		} else {
			//				outs() << "FuncName: " << curF->getName() << "\n";
			//remove alloc inst that only used once in a store instr.
			//the alloc inst should be the 2nd operand of the store instr
			//I created 2 Instruction vectors: aloc instrs and stor instr. Because I need to remove store instrs before alloc instr
			//otherwise, I'll receive errors if the alloc was removed before its store insr
			for (auto I = inst_begin(*curF); I != inst_end(*curF); I++) {
				Instruction *i = &*I;
				if (auto ai = dyn_cast<AllocaInst>(i)) {
					//						outs() << "Var: " << *ai << " , Used = " << ai->getNumUses() << "\n";
					for (auto u : ai->users()) {
						//							outs() << "\tUses: " << *u <<"\n";
						if (ai->getNumUses() == 1 && isa<StoreInst>(u)) {
							if (u->getOperand(1) == ai) {
								localVarsToBeRemoved.push_back(i);
								storeInstToBeRemoved.push_back(
										dyn_cast<Instruction>(u));
							}
						}
					}
				}
			}
		}
	}

	for (auto curG = module.getGlobalList().begin();
			curG != module.getGlobalList().end(); curG++) {
//		outs() << "gblName: " << curG->getName() << " NumUses= " << curG->getNumUses() << "\n";
		if (curG->getNumUses() == 0)
			gblVarsToBeRemoved.push_back(&*curG);
	}

///the following 4 for-loops remove the identified unused variables and functions
	for (auto f : funcToBeRemoved) {
		f->eraseFromParent();
	}

	for (auto g : gblVarsToBeRemoved) {
		g->eraseFromParent();
	}

	/*
	 outs() << "localVarsToBeRemoved: " << localVarsToBeRemoved.size() << "\n";
	 outs() << "storeInstToBeRemoved: " << storeInstToBeRemoved.size() << "\n";
	 */

	for (auto str : storeInstToBeRemoved) {
		str->eraseFromParent();
	}

	for (auto l : localVarsToBeRemoved) {
		l->eraseFromParent();
	}
}

void handlePredicates(Module &module) {
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		for (auto curB = curF->begin(); curB != curF->end(); curB++) {
			for (auto pi = curB->begin(); pi != curB->end(); pi++) {
				if (auto icmp = dyn_cast<ICmpInst>(pi)) {
					outs() << "icmp: " << *icmp << " : "
							<< icmp->getNumOperands() << "\n";
					if (isa<ConstantInt>(icmp->getOperand(0))
							&& isa<Constant>(icmp->getOperand(1))) {
						auto c1 = cast<ConstantInt>(icmp->getOperand(0));
						auto c2 = cast<ConstantInt>(icmp->getOperand(1));
						outs() << "\tc1: " << c1->getSExtValue() << " :: C2="
								<< c2->getSExtValue() << "\n";

						switch (icmp->getPredicate()) {
						case ICmpInst::ICMP_EQ: {
							//always false
							if (c1->getSExtValue() != c2->getSExtValue()) {
								if (auto bi = dyn_cast<BranchInst>(
										icmp->getNextNode())) {	//the assumption branch instr is always after ICMP instr
									outs() << "\tBR: " << *bi << " succ: "
											<< bi->getNumSuccessors()
											<< " :: oprds: "
											<< bi->getNumOperands() << "\n";
									outs() << "\tsucc=s: "
											<< *bi->getSuccessor(0) << "\n";
									outs() << "\tsucc!=: "
											<< *bi->getSuccessor(1) << "\n";//icmp->getPredicate() != ICmpInst::ICMP_EQ
									auto tmpBI = BranchInst::Create(
											bi->getSuccessor(1), &*curB);
									outs() << "tmpBI: " << *tmpBI << "\n";
									bi->eraseFromParent();
									bi->getSuccessor(0)->eraseFromParent();

									//									bi->isConditional()
									//									if (const CmpInst *CI = dyn_cast<CmpInst>(bi->getCondition()))
									//										outs() << "CI: " << *CI<< "\n";
								}
							} else {							//always true

							}
							break;
						}

						case ICmpInst::ICMP_NE: {
							outs() << "\tnot equal" << "\n";
							if (isa<ConstantInt>(icmp->getOperand(0))
									&& isa<Constant>(icmp->getOperand(1))) {

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

struct Debloat: public ModulePass {
	static char ID; // Pass identification, replacement for typeid
	Debloat() :
			ModulePass(ID) {
	}

	bool runOnModule(Module &module) override {
		map<Global, uint64_t> globals = populateGobals();
		map<Local, uint64_t> locals = populateLocals();

		if (globals.size() != 0) {
			handleGlobalVariables(module, globals);
		}

		if (locals.size() != 0) {
			outs() << "Sizeof Locals: " << locals.size() << "\n";
			handleLocalVariables(module, locals);
		}

		if (SimplifyPredicates) {
			outs() << "Simplifying Predicates is enabled\n";
			//			handlePredicates(module);
		}

		if (CleaningUp) {
			cleaningUp(module);
		}
		return true;
	}
};
}

char Debloat::ID = 0;
static RegisterPass<Debloat> X("debloat", "Debloat Pass");
