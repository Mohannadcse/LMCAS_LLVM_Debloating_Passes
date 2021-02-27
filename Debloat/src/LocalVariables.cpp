/*
 * LocalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "LocalVariables.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Use.h"
#include "llvm/Analysis/AliasSetTracker.h"

//#include "llvm/IR/DerivedTypes.h"

int returnIndex(std::vector<Instruction*> list, Instruction *inst) {
	int i = -1;
//	outs() << "TryingToFind inst: " << *inst << " --ListSize= " << list.size() << "\n";
	auto it = find(list.begin(), list.end(), inst);
	if (it != list.cend()) {
		i = std::distance(list.begin(), it);
	}
	return i;
}

void LocalVariables::initalizeInstList(Module &module, string funcName){
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == funcName) {
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

uint32_t neckIndex(Module &module, vector<Instruction*> &instList, string funcName) {
	instList.clear();
	uint32_t neckIdx = 0;
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();
		if (fn == funcName) {
			uint64_t i = 0;
			//identify the index of the neck
			for (auto curB = curF->begin(); curB != curF->end(); curB++) {
				for (auto curI = curB->begin(); curI != curB->end();
						curI++, i++) {
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<CallInst>(curI)) {
						//if the call is indirect call then getCalledFunction returns null
						if (cs->getCalledFunction())
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
 * this method checks if the gep is matching any of the pointer to struct variables that KLEE captured
 */
bool processGepInstrPtrStruct(llvm::GetElementPtrInst *gep,
		tuple<string, uint64_t, int> structInfo) {
	bool ret = false;
	if (gep->getNumOperands() == 3){
		auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
		auto opr0Instr = dyn_cast<AllocaInst>(gep->getOperand(0));
		auto opr0InstrLD = dyn_cast<llvm::LoadInst>(gep->getOperand(0)); //to handle pointer to struct
		if (opr0Type && (opr0Instr || opr0InstrLD))
			if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
				if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
					if (pt->getStructName() == get<0>(structInfo)
							&& op2->getValue().getZExtValue() == get<1>(structInfo))
						ret = true;
					else
						ret = false;
	}
	return ret;
}

bool processGepInstrStruct(llvm::GetElementPtrInst *gep,
		tuple<string, uint64_t> structInfo) {
	bool ret = false;
	if (gep->getNumOperands() == 3){
		auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
		auto opr0InstrAlloc = dyn_cast<AllocaInst>(gep->getOperand(0));
		if (opr0Type && opr0InstrAlloc){
			if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
				if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2))){
					if (pt->getStructName() == get<0>(structInfo)
							&& op2->getValue().getZExtValue() == get<1>(structInfo))
						ret = true;
					else
						ret = false;
			}
		}
	}
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
void LocalVariables::handlePtrLocalStructUsesAfterNeck(Module &module,
		map<tuple<string, uint64_t, int>, uint64_t> &clocals,
		int &modifiedInst, string funcName) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "\n*****\nRunning handlePtrLocalStructUsesAfterNeck...\n";
	vector<pair<GetElementPtrInst*, postNeckGepInfo>> gepInfo;

	for (auto srctGep : clocals) {
		vector<LoadInst*> loadInstUseGep;
		GetElementPtrInst *inst;
		bool isThereStoreInst = 0;

		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == funcName)
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<llvm::GetElementPtrInst>(
								curI)) {
							if (returnIndex(instList, gep)
									> neckIndex(module, instList, funcName)
									&& processGepInstrPtrStruct(gep, srctGep.first)) {
								inst = gep;
								modifiedInst++;
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


void LocalVariables::handleLocalStructUsesAfterNeck(Module &module,
		map <pair<std::string, uint64_t>, uint64_t> &clocals,
		int &modifiedInst, string funcName) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "\n*****\nRunning handleLocalStructUsesAfterNeck...\n";
	vector<pair<GetElementPtrInst*, postNeckGepInfo>> gepInfo;

	for (auto srctGep : clocals) {
		vector<LoadInst*> loadInstUseGep;
		GetElementPtrInst *inst;
		bool isThereStoreInst = 0;

		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == funcName)
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<llvm::GetElementPtrInst>(
								curI)) {
							if (returnIndex(instList, gep)
									> neckIndex(module, instList, funcName)
									&& processGepInstrStruct(gep, srctGep.first)) {
								inst = gep;
								modifiedInst++;
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
 * This method does constant conversion for strcut
 */
void LocalVariables::handleStructLocalVars(Module &module, map <pair<std::string, uint64_t>, uint64_t> &structLocals,
		string funcName){
	int modifiedInst = 0;
	string str;
	raw_string_ostream strLogger(str);
	GetElementPtrInst* singleGEP;
	strLogger << "*****\nStart Converting struct locals to constant\n";

	for (auto elem : structLocals) {
		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == funcName){
				strLogger << "FUNC Name: "<< fn << "\n";
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<GetElementPtrInst>(curI))
							if (returnIndex(instList, gep)
									< neckIndex(module, instList, funcName)){
								//check if the gep is matching one of the elements in structLocals
								if (gep->getNumOperands() == 3
										&& processGepInstrStruct(gep, elem.first)) {
									strLogger << "FOUND GEP with 3 args" << "\n";
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
//	handleLocalStructUsesAfterNeck(module, structLocals, modifiedInst, funcName);
	//inspectInitalizationPreNeck(module, instList, ptrStructLocals, modifiedInst, funcName);
}

/*
 * I need to decide which operand that will be the allocate instr (I noticed I need to check only operand 0)
 * I need to do it now to handle only structs, when GetElementPtrInst is used to compute the address of struct
 * the number of operands is 3. getelementptr t* %val, t1 idx1, t2 idx2
 */
void LocalVariables::handlePtrToStrctLocalVars(Module &module,
		map<tuple<string, uint64_t, int>, uint64_t> &ptrStructLocals, string funcName) {
	int modifiedInst = 0;
	string str;
	raw_string_ostream strLogger(str);
	GetElementPtrInst* singleGEP;
	strLogger << "*****\nStart Converting pointer struct locals to constant\n";
	for (auto elem : ptrStructLocals) {
		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == funcName){
				strLogger << "FUNC Name: "<< fn << "\n";
				for (auto curB = curF->begin(); curB != curF->end(); curB++) {
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						if (auto gep = dyn_cast<GetElementPtrInst>(curI))
							if (returnIndex(instList, gep)
									< neckIndex(module, instList, funcName))
								//check if the gep is matching one of the elements in clocals
								if (gep->getNumOperands() == 3
										&& processGepInstrPtrStruct(gep, elem.first)) {
									strLogger << "FOUND GEP with 3 args" << "\n";
									//this var counts the number of gep that have been converted to constant
									modifiedInst++;
									//perform the constant conversion
									singleGEP = gep;
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
				}}
		}
	}
	logger << strLogger.str();
	handlePtrLocalStructUsesAfterNeck(module, ptrStructLocals, modifiedInst, funcName);
	inspectInitalizationPreNeck(module, instList, ptrStructLocals, modifiedInst, funcName);

	//if there is only one modification, this mean high probably in the deadcode.
	//so I need to define this variable after the method call for klee_dump
	/*if (modifiedInst == 1){
		outs() << "I need to find define After klee_dump" <<"\n";
		IRBuilder<> builder(module.getContext());
		//1- create load
		//2- create gep
		//3- load
		int c = 0;
		bool deadGEP = false;
		BasicBlock* bb = singleGEP->getParent();
		for (auto i = bb->getInstList().begin(); i != bb->getInstList().end();
				i++, c++){
			outs() << *i << "\n";
			if (c == bb->getInstList().size() -1 && isa<BranchInst>(i)){
//				outs() << *i << "\n";
				deadGEP = true;
				i->removeFromParent();
				outs() << "Revome the last inst\n";
				break;
			}
		}
		//insert after klee_dump
		for (auto curF = module.getFunctionList().begin();
				curF != module.getFunctionList().end(); curF++) {
			string fn = curF->getName();
			if (fn == "main")
				for (auto curB = curF->begin(); curB != curF->end(); curB++)
					for (auto curI = curB->begin(); curI != curB->end();
							curI++) {
						Instruction *inst = &*curI;
						if (auto cs = dyn_cast<CallInst>(curI))
							if (cs->getCalledFunction()->getName()
									== "klee_dump_memory" && deadGEP) {
								outs() << "FOUND KLEE_DUMP\n";
//								auto term = cs->getParent()->getTerminator();
								outs() << "BEFORE moving BB\n";
								//								term->insertAfter(cast<Instruction>(lastInst));
								Instruction* curInst = inst;
								int c = 0;
								for (auto i = bb->getInstList().begin(); i != bb->getInstList().end(); i++, c++){
//									outs() << "\tBEF: " << *curInst <<"\n";
									i->moveAfter(curInst+c);
//									curInst = curInst->getNextNode();
//									outs() << "\tAFT: " << *curInst <<"\n";
								}
								//								bb->moveAfter(inst->getParent());
								outs() << "AFTER moving BB\n";
								return;
							}
					}
		}
	}*/
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
		map<tuple<string, uint64_t, int>, uint64_t> &clocals, int &modifiedInst) {
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
						//change to make_tuple, and chk dump impelemnt to get the 3rd element (int)
						if (auto ld = dyn_cast<llvm::LoadInst>(gep->getOperand(0))){
							int idx = 0;
							auto ldOp = find (instList.begin(), instList.end(), cast<llvm::Instruction>(ld->getPointerOperand()));
							if (ldOp != instList.end()){
								idx = std::distance(instList.begin(), ldOp);
								//											llvm::outs() << "idx: " << idx << "\n";
								auto it = clocals.find(
										make_tuple(pt->getStructName(), op2->getValue().getZExtValue(), idx));
								if (it != clocals.end()) {
									for (auto i : gep->users()) {
										modifiedInst++;
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
							//										llvm::outs() << "ld opr:: " << *ld->getPointerOperand() <<"\n";
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
		map<tuple<string, uint64_t, int>, uint64_t> &clocals, int &modifiedInst, string funcName) {
	string str;
	raw_string_ostream strLogger(str);
	set<string> structTypes;
	strLogger << "\n*****\nRunning inspectInitalizationPreNeck...\n";
	for (auto f : clocals) {
		structTypes.insert(get<0>(f.first));//f.first.first
	}
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		if (curF->getName() == funcName)
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
				for (auto curI = curB->begin(); curI != curB->end(); curI++)
					if (auto ci = dyn_cast<CallInst>(curI))
						if (returnIndex(instList, cast<Instruction>(ci))
								< neckIndex(module, instList, funcName)) {
							Function *fn = ci->getCalledFunction();
							if (fn->getName() == "llvm.dbg.declare")
								continue;
							//							outs() << "Call: " << fn->getName() << "\n";
							//check if arguments of the function call site contain a struct
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
													clocals, modifiedInst);
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
		map<AllocaInst*, string> instrToVarName, string funcName) {

	string str;
	raw_string_ostream strLogger(str);
	strLogger << "INSIDE handleLocalPrimitiveUsesAfterNeck\n";
	map<AllocaInst*, uint64_t> updatedInstrToIdx;
	for (auto var : instrToIdx) {
		std::vector<LoadInst*> loadInstUseGep;
		bool isThereStoreInst = 0;
		for (auto i : var.first->users()) {
			if (returnIndex(instList, cast<Instruction>(i))
					> neckIndex(module, instList, funcName)) {
//								strLogger << "INS After neck:: " << *i << "\n";
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
		strLogger << "var: " << *var.first << "\n";
		for (auto i : var.first->users()) {
			strLogger << "\tUSER: " << *i <<"\n";

			if (auto ld = dyn_cast<llvm::LoadInst>(i)) {
				if (returnIndex(instList, cast<Instruction>(i))
						> neckIndex(module, instList, funcName)
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
//							ld->eraseFromParent(); //TODO this stmt causes the problem. Woork around is putting the ld inst that should be removed in a vector and then iterate the elements of the vector to remove the instr
						}
				}
			}
		}
	}
	logger << strLogger.str();
}

void LocalVariables::handlePtrToPrimitiveLocalVariables(Module &module,
		map<uint64_t, pair<uint64_t, uint64_t>> &ptrToPrimtive, string funcName) {
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
					if (returnIndex(instList, &*curI) < neckIndex(module, instList, funcName)){
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
									< neckIndex(module, instList, funcName)) {
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


void LocalVariables::handleStringVars(Module &module, map<uint64_t, pair<uint64_t,
		string>> strList, string funcName){
	IRBuilder<> builder(module.getContext());
	//	ArrayType* ArrayTy_0 = ArrayType::get(IntegerType::get(module.getContext(), 8), 5);
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleStringVars\n";
	for (auto elem : strList){
		string constName = to_string(elem.first);
		strLogger << "ELEM: " << elem.first;
		auto it = instList[elem.first];
		strLogger << "\tFound elemInst:: " << *it << "\n";
		Constant* ary = llvm::ConstantDataArray::getString(module.getContext(), elem.second.second, true);
		GlobalVariable* gv = new GlobalVariable(module, ary->getType(), true,
				GlobalValue::LinkageTypes::PrivateLinkage,
				ary, "");
		gv->setInitializer(ary);
		gv->setName(constName);
		Value *gv_i_ref =
				builder.CreateConstGEP2_32(
						cast<PointerType>(gv->getType())->getElementType(),gv, 0, 0);

		if (auto al = dyn_cast<AllocaInst>(it)){
			strLogger << "Found Alloc inst correspong to elem\n";
			//iterate all uses of the instr
			for (auto usr : al->users()){
				if (returnIndex(instList, cast<Instruction>(usr)) > neckIndex(module, instList, funcName)){
					strLogger << "users:: " << *usr <<"\n";
					if (auto ld = dyn_cast<LoadInst>(usr)){
						strLogger << "\tFound LD \n";
						StoreInst *str = new StoreInst(gv_i_ref, ld->getPointerOperand());
						str->insertBefore(ld);
					} else if (auto st = dyn_cast<StoreInst>(usr)){
						strLogger << "\tFound ST \n";
						//i need to replace the old GEP with a new GEP
						if (auto opr = dyn_cast<GetElementPtrInst>(st->getOperand(0))){
							StoreInst *str = new StoreInst(gv_i_ref, st->getOperand(1));
							ReplaceInstWithInst(st, str);
						}
					}
				}
			}
		}
	}
	logger << strLogger.str();
}

void LocalVariables::handlePrimitiveLocalVariables(Module &module,
		map<string, uint64_t> &plocals, string funcName) {
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleLocalVariables\n";
	//	set<BasicBlock> visitedBbs = populateBasicBlocks();
	map<AllocaInst*, uint64_t> stackVarToIdx;

	map<uint64_t, Instruction*> mapIdxInst = getAllInstr(module);

	map<AllocaInst*, std::string> instrToVarName;

	//I need to update plocals because I noticed some vars are included but their declaration is after the neck.
	//So I need to remove these stack vars
	map<string, uint64_t> updatedPlocals;

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
									al->getType()->getElementType())){
								stackVarToIdx.emplace(cast<llvm::AllocaInst>(curI),
										i);
							}
						}
						//obtain the names of local variables, if applicable
						if (!al->hasName()) {
							for (auto I = curB->begin(); I != curB->end();
									I++) {
								if (DbgDeclareInst *dbg = dyn_cast<
										DbgDeclareInst>(&(*I))) {
									if (const AllocaInst *dbgAI = dyn_cast<
											AllocaInst>(dbg->getAddress()))
										if (dbgAI == al) {
											auto ff = returnIndex(instList, dbg) < neckIndex(module, instList, funcName);
											if (id != plocals.end() && ff)
												updatedPlocals.emplace(id->first, id->second);

											if (DILocalVariable *varMD =
													dbg->getVariable()) {
												strLogger << *al << "\n";
												strLogger << "\tVarName: "
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
								< neckIndex(module, instList, funcName))//do the conversion till the neck
							if (auto opr = dyn_cast<llvm::AllocaInst>(
									ld->getPointerOperand())) {
								//I need to ignore some local variables like argc, because the number of arguments will be
								//different after running the specalized apps. So argc variable should be delayed
								//auto varName = instrToVarName.find(opr);
								//if (varName != instrToVarName.end())
								if (getVarName(instrToVarName, opr) == "argc")
									continue;

								//here I need to check the mapping list of local variables and their values
								auto inst = stackVarToIdx.find(opr);
								if (inst != stackVarToIdx.end()) {
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
								< neckIndex(module, instList, funcName)) {
//							strLogger << "SI:  " << *si << "\n";
//							strLogger << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

							if (isa<ConstantInt>(si->getOperand(0)))
								if (auto opr = dyn_cast<llvm::AllocaInst>(
										si->getOperand(1))) {
									auto inst = stackVarToIdx.find(opr);
									if (inst != stackVarToIdx.end()) {
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

	strLogger << "sizeUPDATED:: " <<updatedPlocals.size() << "\n";
	for (auto it : updatedPlocals)
		strLogger << it.first << it.second << "\n";

	strLogger << "size:: " <<plocals.size() << "\n";
	for (auto it : plocals)
			strLogger << it.first << it.second << "\n";

	logger << strLogger.str();
	handleLocalPrimitiveUsesAfterNeck(module, updatedPlocals, stackVarToIdx, instList,
			instrToVarName, funcName);
}

void LocalVariables::testing(Module &module){
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "Inside test procedure...\n";
	for (auto g = module.global_begin(); g != module.global_end(); g++){
		strLogger << "g: " << *g << "\n";
		for (Value::use_iterator u = g->use_begin(); u != g->use_end(); u++){
			strLogger << "use: " << *u->getUser() << "\n";
		}
	}

	for (auto f = module.getFunctionList().begin(); f != module.getFunctionList().end(); f++){
		strLogger << "Func: " << f->getName() << "\n";
		for (auto bb = f->begin(); bb != f->end(); bb++){
			for (auto i = bb->begin(); i != bb->end(); i++){
				if (auto st = dyn_cast<StoreInst>(i)){
					/*strLogger << "inst: " << *i << "\n";
					strLogger << "\topr0: " << *st->getOperand(0) <<"\n";
					strLogger << "\topr1: " << *st->getOperand(1) <<"\n";
					for (auto us : st->users()){
						strLogger << "usr: " << *us <<"\n";
					}
					for (auto u = st->use_begin(); u != st->use_end(); u++){
						strLogger << "use: " << *u <<"\n";
					}
					for (auto u = st->user_begin(); u != st->user_end(); u++){
						strLogger << "user: " << *u << "\n";
					}
					for (auto u = st->uses().begin(); u != st->uses().end(); u++){
						strLogger << "uses: " << *u << "\n";
					}*/
					outs() << "str: " << *st << "\n";
					outs() << "\tPTR oprnd: " << *st->getPointerOperand() << "\n";
				}
				/*if (auto ld = dyn_cast<LoadInst>(i)){
					strLogger << "LD: " << *ld << "\n";
					for (User *user : ld->users()){
						Instruction* I = dyn_cast<Instruction>(user);
						strLogger << "user: " << *I << "\n";
					}
					for (Use& user : ld->uses()){
						Instruction* I = dyn_cast<Instruction>(user);
						strLogger << "uses: " << *I << "\n";
					}
				}*/
			}
		}
	}

	logger << strLogger.str();
}

