/*
 * LocalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "LocalVariables.h"

int returnIndex(std::vector<Instruction*> list, Instruction *inst) {
	int i = -1;
	//outs() << "TryingToFind inst: " << *inst << " --ListSize= " << list.size() << "\n";
	auto it = find(list.begin(), list.end(), inst);
	if (it != list.cend()) {
		i = std::distance(list.begin(), it);
	}
	return i;
}

void LocalVariables::initalizeInstList(Module &module){
	//	vector<Instruction*> ret;

	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main") {
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
				for (auto curI = curB->begin(); curI != curB->end();
						curI++, i++) {
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<CallInst>(curI))
						if (cs->getCalledFunction())
							if (cs->getCalledFunction()->getName()
									== "klee_dump_memory")
								break;
				}
		}
	}

	//	return ret;
}

uint32_t neckIndex(Module &module, vector<Instruction*> &instList) {
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
					if (auto cs = dyn_cast<CallInst>(curI)) {
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

/*
 * this method checks if the gep is matching any of the struct variables that KLEE captured
 */
bool processGepInstr(llvm::GetElementPtrInst *gep,
		pair<string, uint64_t> structInfo) {
	auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
	auto opr0Instr = dyn_cast<AllocaInst>(gep->getOperand(0));
	auto opr0InstrLD = dyn_cast<llvm::LoadInst>(gep->getOperand(0)); //to handle pointer to struct
	bool ret = false;
	if (opr0Type && (opr0Instr || opr0InstrLD))
		if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
			if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
				if (pt->getStructName() == structInfo.first
						&& op2->getValue().getZExtValue() == structInfo.second)
					ret = true;
				else
					ret = false;
	return ret;
}

void LocalVariables::replaceStructPostNeck(
		vector<pair<GetElementPtrInst*, postNeckGepInfo>> gepInfo) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "\n*****\nReplacing Load Intructions After neck (struct)...\n";
	for (auto elem : gepInfo) {
		//make sure !isThereStoreInst
		if (!get<0>(elem.second)) {
			for (auto ld : get<1>(elem.second)) {
				strLogger << "\nGEP: " << *elem.first << "\n";
				if (auto intType =
						dyn_cast<IntegerType>(
								ld->getPointerOperand()->getType()->getPointerElementType())) {
					auto val = llvm::ConstantInt::get(intType,
							get<2>(elem.second));
					//ReplaceInstWithValue(ld->getParent()->getInstList(), &*ld, val);
					strLogger << "\tReplace: " << *ld << "\n";
					ld->replaceAllUsesWith(val);
					ld->eraseFromParent();
				}
			}
		}
	}
	logger << strLogger.str();
}

/*1- Check getelementptr instructions after the neck
 *2- check if the same struct name and same element index
 *3- If all are matching, then check all uses of the getelementptr and store them in a vector.
 *4- if all use instructions are load, then we can convert them into constants
 *clocals: 0- structName 1- element index 2- constant value
 */
void LocalVariables::handleLocalStructUsesAfterNeck(Module &module,
		map<pair<string, uint64_t>, uint64_t> &clocals,
		vector<Instruction*> instList) {
	string str;
	raw_string_ostream strLogger(str);

	vector<pair<GetElementPtrInst*, postNeckGepInfo>> gepInfo;

	for (auto srctGep : clocals) {
		vector<LoadInst*> loadInstUseGep;
		GetElementPtrInst *inst;
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
								inst = gep;
								for (auto i : gep->users()) {
									if (isa<StoreInst>(i)) {
										isThereStoreInst = 1;
										break;
									} else if (auto ld = dyn_cast<LoadInst>(
											i)) {
										loadInstUseGep.push_back(ld);
									}
								}
							}
						}
					}
				}
		}
		gepInfo.push_back(
				make_pair(inst,
						make_tuple(isThereStoreInst, loadInstUseGep,
								srctGep.second)));
	}
	replaceStructPostNeck(gepInfo);
	logger << strLogger.str();
}

/*
 * I need to decide which operand that will be the allocate instr (I noticed I need to check only operand 0)
 * I need to do it now to handle only structs, when GetElementPtrInst is used to compute the address of struct
 * the number of arguments is 4. getelementptr t* %val, t1 idx1, t2 idx2
 */
void LocalVariables::handleCustomizedLocalVariables(Module &module,
		map<pair<string, uint64_t>, uint64_t> &clocals) {
	vector<Instruction*> instList;

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
												StoreInst *str = new StoreInst(
														val, si->getOperand(1));
												strLogger
												<< "\tSI uses GEP replace \n\t\tFROM: " << *si << "\tTO: " << *str <<"\n";
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
	handleLocalStructUsesAfterNeck(module, clocals, instList);
	inspectInitalizationPreNeck(module, instList, clocals);
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
//I noticed operand(0) isn't AllocaInst as in the main method.
//Not quite sure if this is the case with all methods
void LocalVariables::handleStructInOtherMethods(Function *fn,
		map<pair<string, uint64_t>, uint64_t> &clocals) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "\n*****\nHandle structs in Other methods before the neck...\n";
	for (auto curI = inst_begin(fn); curI != inst_end(fn); curI++) {
		if (auto gep = dyn_cast<GetElementPtrInst>(&*curI)) {
			auto opr0Type = dyn_cast<PointerType>(
					gep->getOperand(0)->getType());
			if (opr0Type)
				if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
					if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2))) {
						auto it = clocals.find(
								make_pair(pt->getStructName(),
										op2->getValue().getZExtValue()));
						if (it != clocals.end()) {
							for (auto i : gep->users()) {
								strLogger << "GEP: " << *gep << "\n";
								strLogger << "\tuser of GEP: " << *i << "\n";
								if (auto si = dyn_cast<StoreInst>(i)) {
									if (auto ci = dyn_cast<ConstantInt>(
											si->getOperand(0))) {
										auto val = ConstantInt::get(
												si->getOperand(0)->getType(),
												it->second);
										strLogger
										<< "\tSI uses GEP replace.. \n\n";
										StoreInst *str = new StoreInst(val,
												si->getOperand(1));
										ReplaceInstWithInst(si, str);
									}
								}
							}
						}
					}
		}
	}
	logger << strLogger.str();
}

/*
 * sometimes the struct variable is initalized in a different method than the main method
 * like the `rm`
 * I need to inspect these methods by checking:
 * 1- all method calls before the main method
 * 2- check the arguments of each method if it contains the struct variable,initalized
 */
void LocalVariables::inspectInitalizationPreNeck(Module &module,
		vector<Instruction*> instList,
		map<pair<string, uint64_t>, uint64_t> &clocals) {
	string str;
	raw_string_ostream strLogger(str);

	set<string> structTypes;
	for (auto f : clocals) {
		structTypes.insert(f.first.first);
	}
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		if (curF->getName() == "main")
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
				for (auto curI = curB->begin(); curI != curB->end(); curI++)
					if (auto ci = dyn_cast<CallInst>(curI))
						if (returnIndex(instList, cast<Instruction>(ci))
								< neckIndex(module, instList)) {
							Function *fn = ci->getCalledFunction();
							if (fn->getName() == "llvm.dbg.declare")
								continue;
							//							outs() << "Call: " << fn->getName() << "\n";
							for (auto arg = fn->arg_begin();
									arg != fn->arg_end(); ++arg)
								if (arg->getType()->isPointerTy())
									if (auto st =
											dyn_cast<StructType>(
													arg->getType()->getPointerElementType()))
										if (structTypes.find(
												st->getStructName())
												!= structTypes.end()) {
											handleStructInOtherMethods(fn,
													clocals);
										}

						}
	}
	logger << strLogger.str();
}

string getVarName(map<AllocaInst*, std::string> &instrToVarName,
		AllocaInst *ld) {
	string name = "";
	auto varName = instrToVarName.find(ld);
	if (varName != instrToVarName.end())
		name = varName->second;
	return name;
}

/*
 * Iterate local variables
 * Find the use of variables after the neck
 * If the use is access (only load instructions) only without modification,
 * then we can convert to constant
 */
void LocalVariables::handleLocalPrimitiveUsesAfterNeck(Module &module,
		map<string, uint64_t> &plocals, map<AllocaInst*, uint64_t> instrToIdx,
		vector<Instruction*> instList,
		map<AllocaInst*, string> instrToVarName) {

	string str;
	raw_string_ostream strLogger(str);

	map<AllocaInst*, uint64_t> updatedInstrToIdx;
	for (auto var : instrToIdx) {
		std::vector<LoadInst*> loadInstUseGep;
		bool isThereStoreInst = 0;
		for (auto i : var.first->users()) {
			if (returnIndex(instList, cast<Instruction>(i))
					> neckIndex(module, instList)) {
				//				strLogger << "INS After neck:: " << *i << "\n";
				if (auto si = dyn_cast<StoreInst>(i)) {
					isThereStoreInst = 1;
					break;
				}
			}
		}
		if (!isThereStoreInst)
			updatedInstrToIdx.emplace(var.first, var.second);
	}

	strLogger
	<< "\n*****\nReplacing Load Intructions After neck (Primitive)...\n";
	for (auto var : updatedInstrToIdx) {
		for (auto i : var.first->users()) {
			if (auto ld = dyn_cast<llvm::LoadInst>(i)) {
				if (returnIndex(instList, cast<Instruction>(i))
						> neckIndex(module, instList)
						&& getVarName(instrToVarName,
								cast<AllocaInst>(ld->getPointerOperand()))
								!= "argc") {
					auto constVal = plocals.find(std::to_string(var.second));
					if (constVal != plocals.end())
						if (auto intType = dyn_cast<IntegerType>(
								var.first->getType()->getElementType())) {
							auto val = llvm::ConstantInt::get(intType,
									constVal->second);
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
	}
	logger << strLogger.str();
}

void LocalVariables::handlePtrToPrimitiveLocalVariables(Module &module,
		map<uint64_t, pair<uint64_t, uint64_t>> &ptrToPrimtive) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handlePtrToPrimitiveLocalVariables\n";
//	strLogger << "Size instList: " << instList.size() <<"\n";

	map<uint64_t, Instruction*> idxToInst = getAllInstr(module);
	map<AllocaInst*, pair<uint64_t, uint64_t>> instrToIdx;

	//here I need to find the alloc instr based on actualIdx not the ptrIdx
	for (auto ptr : ptrToPrimtive){
		//		auto it = idxToInst.find(ptr.first);
		auto it = idxToInst.find(ptr.second.first);
		if (it != idxToInst.end()){
			if (isa<AllocaInst>(it->second)){
				strLogger << "PTR:: " << *it->second << "\n";
				//				instrToIdx.emplace(cast<AllocaInst>(it->second), ptr.first);
				instrToIdx.emplace(cast<AllocaInst>(it->second), ptr.second);
			}
		}
	}

	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == "main") {
			strLogger
			<< "*****\nStart Converting Pointer to Primitive locals to constant\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); curI++) {
					if (returnIndex(instList, &*curI) < neckIndex(module, instList)){
						//handle load inst
						if (auto ld = dyn_cast<LoadInst>(curI)) {
							strLogger << "ptr oprd: " << *ld->getPointerOperand() << "---" << *ld->getPointerOperand()->getType() << "\n";
							//							auto al = dyn_cast<AllocaInst>(ld->getOperand(1));
							//							auto inst = instrToIdx.find(cast<AllocaInst>(si->getOperand(1)));
							//							auto val = llvm::ConstantInt::get(intType, inst->second.second);
							//							ReplaceInstWithValue(curB->getInstList(), curI,
							//																	val);
							//handle store inst
						} else if (auto si = dyn_cast<StoreInst>(curI)){
							if (returnIndex(instList, si)
									< neckIndex(module, instList)) {
								strLogger << "SI:  " << *si << "\n";
								//strLogger << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

								if (isa<llvm::PointerType>(
										si->getOperand(1)->getType()) && isa<AllocaInst>(si->getOperand(1))){
									//if(isa<AllocaInst>(si->getOperand(1))){
									auto al = dyn_cast<AllocaInst>(si->getOperand(1));
									auto inst = instrToIdx.find(cast<AllocaInst>(si->getOperand(1)));
									if (inst != instrToIdx.end()) {
										strLogger << "FOUND: " << *inst->first << " --" << *al->getType()->getElementType() << "\n";
										strLogger << "\t" << inst->second.first << " " << inst->second.second << "\n";
										if (auto intType = dyn_cast<IntegerType>(al->getOperand(0)->getType())) {
											auto val = llvm::ConstantInt::get(intType, inst->second.second);
											strLogger
											<< "\nSI replace.. \n";
											strLogger << "\tFOUND: "
													<< *inst->first
													<< " :: "
													<< inst->second.first
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
}

void LocalVariables::handlePrimitiveLocalVariables(Module &module,
		map<string, uint64_t> &plocals) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleLocalVariables\n";
	//	set<BasicBlock> visitedBbs = populateBasicBlocks();
	map<AllocaInst*, uint64_t> instrToIdx;


	map<uint64_t, Instruction*> mapIdxInst = getAllInstr(module);

	map<AllocaInst*, std::string> instrToVarName;

	strLogger
	<< "*****\nFind list of matching instructions that have index in locals.\n";
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
						if (id != plocals.end()) {
							if (!isa<StructType>(
									al->getType()->getElementType()))
								instrToIdx.emplace(cast<llvm::AllocaInst>(curI),
										i);
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
			strLogger
			<< "*****\nStart Converting Primitive locals to constant\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end(); curI++) {
					//i need to add if store isnt and 1)its 2nd operand is alloc instr and 2)1st operand is constvalue, the
					//do the replacement
					if (auto ld = dyn_cast<LoadInst>(curI)) {
//						strLogger << "lstSize: " << instList.size() << " LD idx: " << returnIndex(instList, ld) << "--" << *ld <<"\n";
						if (returnIndex(instList, ld)
								< neckIndex(module, instList))//do the conversion till the neck
							if (auto opr = dyn_cast<llvm::AllocaInst>(
									ld->getPointerOperand())) {
								//I need to ignore some local variables like argc, because the number of arguments will be
								//different after running the specalized apps. So argc variable should be delayed
								//auto varName = instrToVarName.find(opr);
								//if (varName != instrToVarName.end())
								if (getVarName(instrToVarName, opr) == "argc")
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
					} else if (auto si = dyn_cast<StoreInst>(curI)) {
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
	handleLocalPrimitiveUsesAfterNeck(module, plocals, instrToIdx, instList,
			instrToVarName);
}

void LocalVariables::testing(Module &module){
	for (auto g = module.global_begin(); g != module.global_end(); g++){
		outs() << "g: " << *g << "\n";
	}
}

