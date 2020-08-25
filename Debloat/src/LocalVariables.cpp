/*
 * LocalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "LocalVariables.h"

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
	//	strLogger << "Find the index of the Neck\n";
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
							//strLogger << "Neck Found@: " << neckIdx << "---Size: " << instList.size() << "\n";
							return neckIdx;
						}
					}
				}
			}
		}
	}
	return neckIdx;
}

bool processGepInstr(llvm::GetElementPtrInst *gep,
		pair<string, uint64_t> structInfo) {
	auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
	auto opr0Instr = dyn_cast<AllocaInst>(gep->getOperand(0));
	bool ret = false;
	if (opr0Type && opr0Instr)
		if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
			if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
				if (pt->getStructName() == structInfo.first
						&& op2->getValue().getZExtValue() == structInfo.second)
					ret = true;
				else
					ret = false;
	return ret;
}

/*1- Check getelementptr instructions after the neck
 *2- check if the same struct name and same element index
 *3- If all are matching, then check all uses of the getelementptr and store them in a vector.
 *4- if all use instructions are load, then we can convert them into constants
 */
void LocalVariables::replaceLocalStructUsesAfterNeck(Module &module,
		map<pair<string, uint64_t>, uint64_t> &clocals,
		vector<Instruction*> instList) {

	string str;
	raw_string_ostream strLogger(str);

	for (auto srctGep : clocals) {
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
									> neckIndex(module, instList)
									&& processGepInstr(gep, srctGep.first)) {
								for (auto i : gep->users()) {
									if (isa<StoreInst>(i)) {
										isThereStoreInst = 1;
										break;
									} else if (auto ld = dyn_cast<LoadInst>(
											i)) {
										strLogger << "GEP: " << *gep << "\n";
										loadInstUseGep.push_back(ld);
									}
								}
							}
						}
					}
				}
		}

		//if all loadinstr, then I can perform constant conversion
		if (!isThereStoreInst) {
			strLogger << "\n*****\nReplacing Load Intructions After neck (struct)...\n";
			for (auto ld : loadInstUseGep) {
				strLogger << "\tGEP: " << *ld << "\n";
				if (auto intType =
						dyn_cast<IntegerType>(
								ld->getPointerOperand()->getType()->getPointerElementType())) {
					auto val = llvm::ConstantInt::get(intType, srctGep.second);
					//						ReplaceInstWithValue(ld->getParent()->getInstList(), &*ld, val);
					ld->replaceAllUsesWith(val);
					ld->eraseFromParent();
				}
			}
		}
	}
	logger << strLogger.str();
}

/*
 * I need to decide which operand that will be the allocate instr (I noticed I need to check only operand 0)
 * I need to do it now to handle only structs, when GetElementPtrInst is used to compute the address of struct
 * the number of arguments is 4. getelementptr t* %val, t1 idx1, t2 idx2
 */
void LocalVariables::handleCustomizedLocalVariables(Module &module,
		map<pair<std::string, uint64_t>, uint64_t> &clocals) {
	std::vector<Instruction*> instList;

	string str;
	raw_string_ostream strLogger(str);

	strLogger << "*****\nStart Converting struct locals to constant\n";
	for (auto elem : clocals) {
		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == "main")
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<GetElementPtrInst>(curI))
							if (returnIndex(instList, gep)
									< neckIndex(module, instList))
								//check if the gep is matching one of the elements in clocals
								if (gep->getNumOperands() == 3
										&& processGepInstr(gep, elem.first)) {
									//perform the constant conversion
									for (auto i : gep->users()) {
										strLogger << "GEP: " << *gep << "\n";
										strLogger << "\tuser of GEP: " << *i
												<< "\n";
										if (auto si = dyn_cast<StoreInst>(i)) {
											if (auto ci = dyn_cast<ConstantInt>(
													si->getOperand(0))) {
												auto val =
														ConstantInt::get(
																si->getOperand(
																		0)->getType(),
																		elem.second);
												strLogger
												<< "\tSI uses GEP replace.. \n\n";
												StoreInst *str = new StoreInst(
														val, si->getOperand(1));
												ReplaceInstWithInst(si, str);
											}
										} else if (auto ld = dyn_cast<LoadInst>(
												i)) {

										}
									}
								}
					}
				}
		}
	}
	logger << strLogger.str();
	//post-neck
	replaceLocalStructUsesAfterNeck(module, clocals, instList);
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

/*
 * Iterate local variables
 * Find the use of variables after the neck
 * If the use is access (only load instructions) only without modification,
 * then we can convert to constant
 */
void LocalVariables::replaceLocalPrimitiveUsesAfterNeck(Module &module, map<string, uint64_t> &plocals,
		map<AllocaInst*, uint64_t> instrToIdx,
		std::vector<Instruction*> instList){
	string str;
	raw_string_ostream strLogger(str);

	map<AllocaInst*, uint64_t> updatedInstrToIdx;
	for (auto var : instrToIdx){
		std::vector<LoadInst*> loadInstUseGep;
		bool isThereStoreInst = 0;
		for (auto i : var.first->users()) {
			if (returnIndex(instList, cast<Instruction>(i)) > neckIndex(module, instList)){
//				strLogger << "INS After neck:: " << *i << "\n";
				if (auto si = dyn_cast<StoreInst>(i)){
					isThereStoreInst = 1;
					break;
				}
			}
		}
		if (! isThereStoreInst)
			updatedInstrToIdx.emplace(var.first, var.second);
	}

	strLogger << "\n*****\nReplacing Load Intructions After neck (Primitive)...\n";
	for (auto var : updatedInstrToIdx){
		for (auto i : var.first->users()) {
			if (auto ld = dyn_cast<llvm::LoadInst>(i)) {
				auto constVal = plocals.find(std::to_string(var.second));
				if (constVal != plocals.end())
					if (auto intType = dyn_cast<IntegerType>(var.first->getType()->getElementType())) {
						auto val = llvm::ConstantInt::get(intType, constVal->second);
						strLogger << "\nLD replace.. \n";
						strLogger << "\tFOUND: " << *var.first << " :: "
								<< var.second << " :: " << *i << "\n";
						//ReplaceInstWithValue(ld->getParent()->getInstList(), ld, val);
						ld->replaceAllUsesWith(val);
						ld->eraseFromParent();
					}
			}
		}
	}
	logger << strLogger.str();
}

void LocalVariables::handlePrimitiveLocalVariables(Module &module,
		map<string, uint64_t> &plocals) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleLocalVariables\n";
	//	set<BasicBlock> visitedBbs = populateBasicBlocks();
	map<AllocaInst*, uint64_t> instrToIdx;
	std::vector<Instruction*> instList;

	map<uint64_t, Instruction*> mapIdxInst = getAllInstr(module);

	map<llvm::AllocaInst*, std::string> instrToVarName;

	strLogger << "*****\nFind list of matching instructions that have index in locals.\n";
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		//the assumption is that majority of the analysis on the local variables
		//should be conducted inside the main, this where the neck is
		if (fn == "main") {
			//Get mapping between allocation instruction and its index in the locals file
			//this step is necessary to identify the corresponding value to each local variable
			//local variables are alloca instructions
			//I noticed the index of instructions isn't a reliable approach, because in the LLVM pass we consider the entire
			//bitcode, but KLEE interpreter doesn't maintain the order of the instructions
			//one hack I found, in KLEE there is no 2 consecutive br instructions
			//Therefore, when I reach a branch instr:
			//1- check if there is a br instr before it
			//2- if so, reduce i by one
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); ++curI) {
					//to keep the index i consistent and matching the one in KLEE
					//strLogger <<"i: " << i << " --- " << *curI<< "\n";
					if (auto br = dyn_cast<BranchInst>(&*curI)) {
						//check if the previous instr is br
						auto inst = mapIdxInst.find(i - 1);
						if (inst != mapIdxInst.end()
								&& isa<BranchInst>(inst->second)) {
							//strLogger << "****Found TWO Branches\n";
							--i;
						}
					}

					auto id = plocals.find(std::to_string(i));
					if (auto al = dyn_cast<llvm::AllocaInst>(curI)) {
						if (id != plocals.end()){
							if (! isa<StructType>(al->getType()->getElementType()))
								instrToIdx.emplace(cast<llvm::AllocaInst>(curI), i);
							//  idxToAlloc.emplace(i, cast<llvm::AllocaInst>(curI));
					 	}
						//obtain the names of local variables, if applicable
						if (!al->hasName()) {
							strLogger << *al << "\n";
							for (auto I = curB->begin(); I != curB->end();
									I++) {
								if (DbgDeclareInst *dbg = dyn_cast<
										DbgDeclareInst>(&(*I))) {
									if (const AllocaInst *dbgAI = dyn_cast<
											AllocaInst>(dbg->getAddress()))
										if (dbgAI == al) {
											if (DILocalVariable *varMD =
													dbg->getVariable()) {
												strLogger << "VarName: "
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
							strLogger << "NAME:: " << al->getName() << "\n";
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
			strLogger << "*****\nStart Converting Primitive locals to constant\n";
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
									auto constVal = plocals.find(
											std::to_string(inst->second));
									if (constVal != plocals.end()) {
										if (auto intType =
												dyn_cast<IntegerType>(
														opr->getType()->getElementType())) {
											auto val = llvm::ConstantInt::get(
													intType, constVal->second);
											strLogger << "\nLD replace.. \n";
											strLogger << "\tFOUND: "
													<< *inst->first << " :: "
													<< inst->second << " :: "
													<< *curI << "\n";
											ReplaceInstWithValue(
													curI->getParent()->getInstList(),
													curI, val);
										}
									}
								}
							}
					} else if (auto si = dyn_cast<llvm::StoreInst>(curI)) {
						if (returnIndex(instList, si)
								< neckIndex(module, instList)) {
							//strLogger << "SI:  " << *si << "\n";
							//strLogger << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

							if (isa<ConstantInt>(si->getOperand(0)))
								if (auto opr = dyn_cast<llvm::AllocaInst>(
										si->getOperand(1))) {

									auto inst = instrToIdx.find(opr);
									if (inst != instrToIdx.end()) {
										auto constVal = plocals.find(
												std::to_string(inst->second));
										if (constVal != plocals.end()) {
											if (auto intType =
													dyn_cast<IntegerType>(
															opr->getType()->getElementType())) {
												auto val =
														llvm::ConstantInt::get(
																intType,
																constVal->second);
												strLogger
												<< "\nSI replace.. \n";
												strLogger << "\tFOUND: "
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
												strLogger << "\tAfter conv: "
														<< *curI << "\n";
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
	logger << strLogger.str();
	replaceLocalPrimitiveUsesAfterNeck(module, plocals, instrToIdx, instList);
}

