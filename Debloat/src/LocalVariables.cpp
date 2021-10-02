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
#include "llvm/IR/Attributes.h"
#include "llvm/Analysis/AliasSetTracker.h"

int returnIndex(std::vector<Instruction *> list, Instruction *inst)
{
	int i = -1;
	auto it = find(list.begin(), list.end(), inst);
	if (it != list.cend())
	{
		i = std::distance(list.begin(), it);
	}
	return i;
}

void LocalVariables::initalizeInstList(Module &module, string funcName)
{
	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		if (fn == funcName)
		{
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
				for (auto curI = curB->begin(); curI != curB->end();
					 curI++, i++)
				{
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<CallInst>(curI))
						if (cs->getCalledFunction())
							if (cs->getCalledFunction()->getName() == "klee_dump_memory")
								break;
				}
		}
	}
}

uint32_t neckIndex(Module &module, vector<Instruction *> &instList,
				   string funcName)
{
	instList.clear();
	uint32_t neckIdxInst = 0;
	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		if (fn == funcName)
		{
			uint64_t i = 0;
			//identify the index of the neck
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
			{
				for (auto curI = curB->begin(); curI != curB->end();
					 curI++, i++)
				{
					Instruction *inst = &*curI;
					instList.push_back(inst);
					if (auto cs = dyn_cast<CallInst>(curI))
					{
						//if the call is indirect call then getCalledFunction returns null
						if (cs->getCalledFunction())
							if (cs->getCalledFunction()->getName() == "klee_dump_memory")
							{
								neckIdxInst = i;
								//strLogger << "Neck Found@: " << neckIdxInst << "---Size: " << instList.size() << "\n";
								return neckIdxInst;
							}
					}
				}
			}
		}
	}
	return neckIdxInst;
}

/*
 * this method checks if the gep is matching any of the pointer to struct variables
 */
bool LocalVariables::processGepInstrPtrStruct(llvm::GetElementPtrInst *gep,
											  tuple<string, uint64_t, int> structInfo)
{
	bool ret = false;
	if (gep->getNumOperands() == 3)
	{
		auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
		auto opr0InstrLD = dyn_cast<LoadInst>(gep->getOperand(0)); //to handle pointer to struct
		if (opr0Type && opr0InstrLD)
		{
			if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
				if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
					if (pt->getStructName() == get<0>(structInfo) && op2->getValue().getZExtValue() == get<1>(structInfo) && returnIndex(instList, cast<Instruction>(opr0InstrLD->getPointerOperand())) == get<2>(structInfo))
						ret = true;
					else
						ret = false;
		}
	}
	return ret;
}

bool LocalVariables::processGepInstrStruct(llvm::GetElementPtrInst *gep,
										   tuple<string, uint64_t, int> structInfo)
{
	bool ret = false;
	if (gep->getNumOperands() == 3)
	{
		auto opr0Type = dyn_cast<PointerType>(gep->getOperand(0)->getType());
		auto opr0InstrAlloc = dyn_cast<AllocaInst>(gep->getOperand(0));
		if (opr0Type && opr0InstrAlloc)
		{
			if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
				if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
				{
					if (pt->getStructName() == get<0>(structInfo) && op2->getValue().getZExtValue() == get<1>(structInfo) && returnIndex(instList, cast<Instruction>(opr0InstrAlloc)) == get<2>(structInfo))
						ret = true;
					else
						ret = false;
				}
		}
	}
	return ret;
}

/*
 * cntxtFlg is used to recognize different contexts where this function is called. This flag is also used for handling
 * variables called by address when their address are passed as parameters, so we don't need to check the neck location
 * we achieve this by setting the value of this flag to zero
 */
bool LocalVariables::processGepInstrNestedStruct(
	llvm::GetElementPtrInst *mainGEP, llvm::GetElementPtrInst *elemGEP,
	tuple<string, string, uint64_t, uint64_t, int> structInfo,
	int cntxtFlg)
{
	bool ret = false;
	if (mainGEP->getNumOperands() == 3 && elemGEP->getNumOperands() == 3)
	{
		auto opr0TypeMain = dyn_cast<PointerType>(
			mainGEP->getOperand(0)->getType());
		auto opr0InstrAllocMain = dyn_cast<AllocaInst>(mainGEP->getOperand(0));
		auto opr0TypeElem = dyn_cast<PointerType>(
			elemGEP->getOperand(0)->getType());
		if (!cntxtFlg)
		{
			auto opr0InstrLoadMain = dyn_cast<LoadInst>(mainGEP->getOperand(0));
			opr0InstrAllocMain = dyn_cast<AllocaInst>(
				opr0InstrLoadMain->getOperand(0));
		}
		if (opr0TypeMain && opr0InstrAllocMain && opr0TypeElem)
		{
			auto ptMain = dyn_cast<StructType>(opr0TypeMain->getElementType());
			auto ptElem = dyn_cast<StructType>(opr0TypeElem->getElementType());
			if (ptMain && ptElem)
			{
				auto op2Main = dyn_cast<ConstantInt>(mainGEP->getOperand(2));
				auto op2Elem = dyn_cast<ConstantInt>(elemGEP->getOperand(2));
				if (op2Main && op2Elem)
				{
					bool idxInst = returnIndex(instList,
											   cast<Instruction>(opr0InstrAllocMain)) == get<4>(structInfo);
					if (!cntxtFlg)
						idxInst = 1;

					if (ptMain->getStructName() == get<0>(structInfo) && ptElem->getStructName() == get<1>(structInfo) && op2Main->getValue().getZExtValue() == get<2>(structInfo) && op2Elem->getValue().getZExtValue() == get<3>(structInfo) && idxInst)
						ret = true;
					else
						ret = false;
				}
			}
		}
	}
	return ret;
}

void LocalVariables::constantConversionStrctVars(Module &module,
												 GetElementPtrInst *elemGEP, string funcName, uint64_t value,
												 raw_string_ostream &strLogger, int cntxtFlg)
{
	int onlyReadGEP = 1;
	strLogger << "\tinside constantConversionStrctVars---cntxtFlg= " << cntxtFlg
			  << "\n";

	//check no writing after the neck if cntxtFlg = 1 or 2
	if (cntxtFlg == 1 || cntxtFlg == 2)
	{
		for (auto usr : elemGEP->users())
		{
			if (auto si = dyn_cast<StoreInst>(usr))
			{
				if (returnIndex(instList, si) > neckIndex(module, instList, funcName))
				{
					onlyReadGEP = 0;
					strLogger << "onlyReadGEP = " << onlyReadGEP << "\n";
				}
			}
		}
	}

	for (auto usr : elemGEP->users())
	{
		if (auto si = dyn_cast<StoreInst>(usr))
		{
			bool idxInst = 1;
			if (cntxtFlg == 1 || cntxtFlg == 2)
				idxInst = returnIndex(instList, si) < neckIndex(module, instList, funcName);
			//				we don't care about the idxInst in func before the neck

			if (idxInst)
				if (auto ci = dyn_cast<ConstantInt>(si->getOperand(0)))
				{
					strLogger << "ElemGEP: " << *elemGEP << "\n";
					strLogger << "\tuser of GEP: " << *usr << "\n";
					auto val = ConstantInt::get(si->getOperand(0)->getType(),
												value);
					StoreInst *str = new StoreInst(val, si->getOperand(1));
					strLogger << "\tSI uses GEP replace \n\t\tFROM: " << *si
							  << "\tTO: " << *str << "\n";
					ReplaceInstWithInst(si, str);
				}
		}
		else if (auto ld = dyn_cast<LoadInst>(usr))
		{
			strLogger << "FOUND ld instr: " << *ld << "\n";
			if (onlyReadGEP)
			{
				strLogger << "\nGEP: " << *elemGEP << "\n";
				strLogger << "\tuser of GEP: " << *usr << "\n";
				if (auto intType =
						dyn_cast<IntegerType>(
							ld->getPointerOperand()->getType()->getPointerElementType()))
				{
					auto val = llvm::ConstantInt::get(intType, value);
					strLogger << "\tReplace: " << *ld << "  with Val: " << value
							  << "\n";
					ld->replaceAllUsesWith(val);
					ld->eraseFromParent();
				}
			}
		}
	}
}

/*
 * The main logic to convert nestedstruct variables to constant:
 * 1- Search for GEP that matches the mainStrct, its index, and the index of corresponding AllocInstr
 * 2- Loop over the users of the found GEP
 * 3- If the user is GEP and matches the elemStrct
 * 4- then we can proceed with CC:
 * 4:1- find storeInst user of the GEP (elemStrct)
 * 4:2- perform CC based on the constant value
 *
 * The nested struct pattern (nested struct nested elem) handled here: GEP main -> GEP elem -> store/load instr
 */
void LocalVariables::handleNestedStrct(Module &module,
									   map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &vars,
									   string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	set<Instruction *> strctAlloc;

	strLogger << "*****\nStart CC nested struct (no PTR) locals\n";
	for (auto elem : vars)
	{
		strLogger << "ELEM:: " << get<0>(elem.first) << " "
				  << get<1>(elem.first) << " " << get<2>(elem.first) << " "
				  << get<3>(elem.first) << " " << *instList[get<4>(elem.first)]
				  << "\n";
		for (auto curF = module.getFunctionList().begin();
			 curF != module.getFunctionList().end(); curF++)
		{
			string fn = curF->getName();
			if (fn == funcName)
			{
				for (auto curI = inst_begin(*curF); curI != inst_end(*curF);
					 curI++)
				{
					if (auto *mainGEP = dyn_cast<GetElementPtrInst>(&*curI))
					{
						for (auto usr : mainGEP->users())
						{
							if (auto elemGEP = dyn_cast<GetElementPtrInst>(usr))
								if (processGepInstrNestedStruct(mainGEP,
																elemGEP, elem.first, 1))
									constantConversionStrctVars(module, elemGEP,
																funcName, elem.second, strLogger,
																1);
						}
					}
				}

				//handling nested structs passed by address before the neck
				//it finds function calls wherein struct variables are passed by address
				for (auto usr : instList[get<4>(elem.first)]->users())
					if (auto callInst = dyn_cast<CallInst>(usr))
					{
						strLogger << "\tFound user: " << *callInst << "\n";
						if (returnIndex(instList, callInst) < neckIndex(module, instList, funcName))
						{
							int c = 0;
							for (auto arg = callInst->arg_begin();
								 arg != callInst->arg_end(); arg++, c++)
							{
								if (instList[get<4>(elem.first)] == *arg)
								{
									strLogger << "\t\ttr: "
											  << callInst->paramHasAttr(c,
																		Attribute::ByVal)
											  << "\n";
									if (!callInst->paramHasAttr(c,
																Attribute::ByVal))
									{
										strLogger
											<< "\t\t\tfound call by address to: "
											<< *arg->get() << "\n";
										for (auto curI = inst_begin(
												 *callInst->getCalledFunction());
											 curI != inst_end(
														 *callInst->getCalledFunction());
											 curI++)
										{
											if (auto *mainGEP = dyn_cast<
													GetElementPtrInst>(
													&*curI))
											{
												for (auto usr : mainGEP->users())
												{
													if (auto elemGEP = dyn_cast<
															GetElementPtrInst>(
															usr))
													{
														if (processGepInstrNestedStruct(
																mainGEP,
																elemGEP,
																elem.first, 0))
															constantConversionStrctVars(
																module,
																elemGEP,
																funcName,
																elem.second,
																strLogger,
																0);
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

	logger << strLogger.str();
}

/*
 * The main logic to convert ptr to strct in a nestedstruct variables to constant:
 * 1- Search for GEP that matches the mainStrct, its index, and the index of corresponding AllocInstr
 * 2- Loop over the users of the found GEP
 * 3- If the user is LoadInstr
 * 4- Loop over users of the loadInstr
 * 5- If the user is GEP and matches the elemStrct
 * 6- then we can proceed with CC:
 * 6:1- find storeInst user of the GEP (elemStrct)
 * 6:2- perform CC based on the constant value
 */
void LocalVariables::handlePtrToNestedStrct(Module &module,
											map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &vars,
											string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger
		<< "*****\nStart CC nested struct PTR locals\n\n";

	//this logic for constant conversion for a struct variable that contains a pointer to struct element
	//because the pattern here is: GEP  main strct > load instr > GEP elem strct > load instr (after neck) or store inst (before neck)
	for (auto elem : vars)
	{
		strLogger << "ELEM:: " << get<0>(elem.first) << " "
				  << get<1>(elem.first) << " " << get<2>(elem.first) << " "
				  << get<3>(elem.first) << " " << *instList[get<4>(elem.first)]
				  << "\n";
		for (auto curF = module.getFunctionList().begin();
			 curF != module.getFunctionList().end(); curF++)
		{
			string fn = curF->getName();
			if (fn == funcName)
			{
				for (auto curI = inst_begin(*curF); curI != inst_end(*curF);
					 curI++)
				{
					if (auto *mainGEP = dyn_cast<GetElementPtrInst>(&*curI))
					{
						for (auto usr : mainGEP->users())
						{
							if (auto ld = dyn_cast<LoadInst>(usr))
							{
								strLogger << "\tmainGEP: " << *mainGEP << "\n";
								strLogger << "\t\tmainGEP user: " << *usr
										  << "\n";
								for (auto ldUser : ld->users())
								{
									strLogger << "\t\t\tld user: " << *ldUser
											  << "\n";
									if (auto *elemGEP = dyn_cast<
											GetElementPtrInst>(ldUser))
										if (processGepInstrNestedStruct(mainGEP,
																		elemGEP, elem.first, 2))
											constantConversionStrctVars(module,
																		elemGEP, funcName,
																		elem.second, strLogger, 2);
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

/*
 * this function performs constant conversion after the neck for pointer to nested struct after the neck
 * iff the element's value hasnt been changed after the neck by doing something similar to the function handlePtrToStructAfterNeck
 * The main logic of this functions:
 * 1- For each nested struct,
 * 1-1- find load inst that: its operand is matching the name of the main strct and uses the corresponding alloc instruction to the main strct
 * 2- find GEP1 that: uses the load instr, its operand is the name of the main strct and the index
 * 3- find GEP2 that: uses GEP1, its operand is the name of the element strct and the element index
 * 4- check the users of GEP2 is load instr and no store instr is a user of GEP2
 */
void LocalVariables::handlePtrToNestedStructAfterNeck(
	map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &vars,
	raw_string_ostream &strLogger)
{

}

/*
 * this function replaces pointer to strct but after the neck iff the struct hasnt been modified
 */
void LocalVariables::replacePtrToStructPostNeck(
	vector<pair<GetElementPtrInst *, postNeckGepInfo>> gepInfo,
	raw_string_ostream &strLogger)
{
	strLogger
		<< "\n*****\nReplacing Load Intructions After neck (ptr to struct)...\n";
	for (auto elem : gepInfo)
	{
		//make sure !isThereStoreInst
		if (!get<0>(elem.second))
		{
			for (auto ld : get<1>(elem.second))
			{
				strLogger << "\nGEP: " << *elem.first << "\n";
				if (auto intType =
						dyn_cast<IntegerType>(
							ld->getPointerOperand()->getType()->getPointerElementType()))
				{
					auto val = llvm::ConstantInt::get(intType,
													  get<2>(elem.second));
					strLogger << "\tReplace: " << *ld << "  with Val: "
							  << get<2>(elem.second) << "\n";
					ld->replaceAllUsesWith(val);
					strLogger << "\t\tRemoving converted Load instr: " << *ld
							  << "\n";
					ld->eraseFromParent();
				}
			}
		}
	}
}

/*1- Check getelementptr instructions after the neck
 *2- check if the same struct name and same element index
 *3- If all are matching, then check all uses of the getelementptr and store them in a vector.
 *4- if all use instructions are load, then we can convert them into constants
 *clocals: KEY: (0- structName 1- element index 2- allocIdxInst) --> VALUE: (constant value)
 */
void LocalVariables::handlePtrToStructAfterNeck(Module &module,
												map<tuple<string, uint64_t, int>, uint64_t> &clocals, string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "\n*****\nRunning handlePtrLocalStructUsesAfterNeck...\n";
	vector<pair<GetElementPtrInst *, postNeckGepInfo>> gepInfo;

	for (auto srctGep : clocals)
	{
		vector<LoadInst *> loadInstUseGep;
		GetElementPtrInst *inst;
		bool isThereStoreInst = 0;

		for (auto curF = module.getFunctionList().begin();
			 curF != module.getFunctionList().end(); curF++)
		{
			string fn = curF->getName();
			if (fn == funcName)
				for (auto curB = curF->begin(); curB != curF->end(); curB++)
				{
					for (auto curI = curB->begin(); curI != curB->end();
						 curI++)
					{
						if (auto gep = dyn_cast<llvm::GetElementPtrInst>(
								curI))
						{
							if (returnIndex(instList, gep) > neckIndex(module, instList, funcName) && processGepInstrPtrStruct(gep,
																															   srctGep.first))
							{
								inst = gep;
								for (auto i : gep->users())
								{
									if (isa<StoreInst>(i))
									{
										//this means the value is changed after the neck
										isThereStoreInst = 1;
										break;
									}
									else if (auto ld = dyn_cast<LoadInst>(
												 i))
									{
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
	replacePtrToStructPostNeck(gepInfo, strLogger);
	logger << strLogger.str();
}

/*
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
 */

/*
 * This method does constant conversion for strcut
 * the main logic is:
 * 1- find GEP instr with 3 operands
 * 2- find the users of the found GEP
 * 3- verify if this struct has corresponding const value in the list structLocals
 * 4- find if the user is store instr
 */
void LocalVariables::handleStructLocalVars(Module &module,
										   map<tuple<string, uint64_t, int>, uint64_t> &structLocals,
										   string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nStart CC struct locals\n";

	for (auto elem : structLocals)
	{
		for (auto curF = module.getFunctionList().begin();
			 curF != module.getFunctionList().end(); curF++)
		{
			string fn = curF->getName();
			if (fn == funcName)
			{
				strLogger << "FUNC Name: " << fn << "\n";
				for (auto curB = curF->begin(); curB != curF->end(); curB++)
				{
					for (auto curI = curB->begin(); curI != curB->end();
						 curI++)
					{
						if (auto gep = dyn_cast<GetElementPtrInst>(curI))
						{
							if (returnIndex(instList, gep) < neckIndex(module, instList, funcName))
							{
								//check if the gep is matching one of the elements in structLocals
								if (processGepInstrStruct(gep, elem.first))
								{
									strLogger << "FOUND GEP with 3 args"
											  << "\n";
									for (auto usr : gep->users())
									{
										strLogger << "GEP: " << *gep << "\n";
										strLogger << "\tuser of GEP: " << *usr
												  << "\n";

										if (auto si = dyn_cast<StoreInst>(
												usr))
										{
											if (auto ci = dyn_cast<ConstantInt>(
													si->getOperand(0)))
											{
												auto val =
													ConstantInt::get(
														si->getOperand(
															  0)
															->getType(),
														elem.second);
												StoreInst *str = new StoreInst(
													val, si->getOperand(1));
												strLogger
													<< "\tSI uses GEP replace \n\t\tFROM: "
													<< *si << "\tTO: "
													<< *str << "\n";
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
	}
	logger << strLogger.str();
	//	handleLocalStructUsesAfterNeck(module, structLocals, modifiedInst, funcName);
	inspectInitalizationPreNeck(module, structLocals, funcName);
}

/*
 * I need to decide which GEP operand that will be the allocate instr (I noticed I need to check only operand 0)
 * I need to do it now to handle only structs, when GetElementPtrInst is used to compute the address of struct
 * the number of operands is 3. getelementptr t* %val, t1 idxInst1, t2 idxInst2
 * TODO this function logic looks similar to the function handleStructLocalVars, except very minor differences
 * processGepInstrPtrStruct is used here while handleStructLocalVars uses processGepInstrStruct
 * I might need to merge the two functions together
 */
void LocalVariables::handlePtrToStrctLocalVars(Module &module,
											   map<tuple<string, uint64_t, int>, uint64_t> &ptrStructLocals,
											   string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nStart CC pointer struct locals\n";
	for (auto elem : ptrStructLocals)
	{
		for (auto curF = module.getFunctionList().begin();
			 curF != module.getFunctionList().end(); curF++)
		{
			string fn = curF->getName();
			if (fn == funcName)
			{
				strLogger << "FUNC Name: " << fn << "\n";
				for (auto curB = curF->begin(); curB != curF->end(); curB++)
				{
					for (auto curI = curB->begin(); curI != curB->end();
						 curI++)
					{
						if (auto gep = dyn_cast<GetElementPtrInst>(curI))
							if (returnIndex(instList, gep) < neckIndex(module, instList, funcName))
								//check if the gep is matching one of the elements in clocals
								if (processGepInstrPtrStruct(gep, elem.first))
								{
									strLogger << "FOUND GEP with 3 args"
											  << "\n";
									//this var counts the number of gep that have been converted to constant
									//perform the constant conversion
									for (auto i : gep->users())
									{
										strLogger << "GEP: " << *gep << "\n";
										strLogger << "\tuser of GEP: " << *i
												  << "\n";
										if (auto si = dyn_cast<StoreInst>(i))
										{
											if (auto ci = dyn_cast<ConstantInt>(
													si->getOperand(0)))
											{
												auto val =
													ConstantInt::get(
														si->getOperand(
															  0)
															->getType(),
														elem.second);
												StoreInst *str = new StoreInst(
													val, si->getOperand(1));
												strLogger
													<< "\tSI uses GEP (PTR strct) replace \n\t\tFROM: "
													<< *si << "\tTO: "
													<< *str << "\n";
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
	logger << strLogger.str();
	handlePtrToStructAfterNeck(module, ptrStructLocals, funcName);
	inspectInitalizationPreNeck(module, ptrStructLocals, funcName);
}

/*
 * This function does similar task to the list instList
 * TODO check if there is any duplication
 */
map<uint64_t, Instruction *> getAllInstr(Module &module)
{
	map<uint64_t, Instruction *> insts;
	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		if (fn == "main")
		{
			uint64_t i = 0;
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
			{
				for (auto curI = curB->begin(); curI != curB->end();
					 curI++, i++)
				{
					Instruction *inst = &*curI;
					insts.emplace(i, inst);
				}
			}
		}
	}
	return insts;
}

void LocalVariables::handleStructInOtherMethods(Function *fn,
												  map<tuple<string, uint64_t, int>, uint64_t> &clocals,
												  raw_string_ostream &strLogger, int idxInst)
{
	strLogger
		<< "\n*****\nHandle structs in Other methods before the neck... Func: "
		<< fn->getName() << "\n";

	for (auto curI = inst_begin(fn); curI != inst_end(fn); curI++)
	{
		if (auto gep = dyn_cast<GetElementPtrInst>(&*curI))
		{
			if (auto opr0Type = dyn_cast<PointerType>(
					gep->getOperand(0)->getType()))
				if (auto pt = dyn_cast<StructType>(opr0Type->getElementType()))
					if (auto op2 = dyn_cast<ConstantInt>(gep->getOperand(2)))
					{
						auto it = clocals.find(
							make_tuple(pt->getStructName(),
									   op2->getValue().getZExtValue(),
									   idxInst));
						if (it != clocals.end())
						{
							for (auto i : gep->users())
							{
								strLogger << "GEP: " << *gep << "\n";
								strLogger << "\tuser of GEP: " << *i
										  << "\n";
								if (auto si = dyn_cast<StoreInst>(i))
								{
									if (auto ci = dyn_cast<ConstantInt>(
											si->getOperand(0)))
									{
										auto val =
											ConstantInt::get(
												si->getOperand(
													  0)
													->getType(),
												it->second);
										strLogger
											<< "\tSI uses GEP replace.. \n\n";
										StoreInst *str = new StoreInst(
											val, si->getOperand(1));
										ReplaceInstWithInst(si, str);
									}
								}
							}
						}
					}
		}
	}
}

/*
 * sometimes the struct variable is initalized in a different method than the main method
 * like the struct x in the main in `rm`: rm_option_init (&x)
 * I need to inspect these methods by checking:
 * 1- all method calls before the main method
 * 2- check the arguments of each method if it contains the struct variable,initalized
 * TODO this situation can happen with other variables
 */
void LocalVariables::inspectInitalizationPreNeck(Module &module,
												   map<tuple<string, uint64_t, int>, uint64_t> &clocals, string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	set<string> structTypes;
	strLogger << "\n*****\nRunning inspectInitalizationPreNec...\n";
	int idxInst = 0;
	for (auto inst : instList)
	{
		if (auto *ai = dyn_cast<AllocaInst>(inst))
		{
			if (ai->getType()->isPointerTy())
			{
				if (auto *st = dyn_cast<StructType>(ai->getType()->getPointerElementType()))
				{
					strLogger << "****st: " << *st << "---idxInst= " << idxInst << "\n";
					for (auto usr : ai->users())
					{
						//TODO I may need to check the args of ci to avoid pass by address 
						if (auto *ci = dyn_cast<CallInst>(usr))
							if (returnIndex(instList, cast<Instruction>(ci)) < neckIndex(module, instList, funcName))
							{
								Function *fn = ci->getCalledFunction();
								if (fn->getName() == "llvm.dbg.declare")
									continue;
								handleStructInOtherMethods(fn, clocals, strLogger, idxInst);
							}
					}
				}
			}
		}
		idxInst++;
	}
	logger << strLogger.str();
}

string getVarName(map<AllocaInst *, std::string> &instrToVarName,
				  AllocaInst *ld)
{
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
													   map<string, uint64_t> &plocals, map<AllocaInst *, uint64_t> instrToIdxInst,
													   vector<Instruction *> instList, map<AllocaInst *, string> instrToVarName,
													   string funcName)
{

	string str;
	raw_string_ostream strLogger(str);
	strLogger << "INSIDE handleLocalPrimitiveUsesAfterNeck\n";
	map<AllocaInst *, uint64_t> updatedInstrToIdxInst;
	for (auto var : instrToIdxInst)
	{
		std::vector<LoadInst *> loadInstUseGep;
		bool isThereStoreInst = 0;
		for (auto i : var.first->users())
		{
			if (returnIndex(instList, cast<Instruction>(i)) > neckIndex(module, instList, funcName))
			{
				//								strLogger << "INS After neck:: " << *i << "\n";
				if (auto si = dyn_cast<StoreInst>(i))
				{
					isThereStoreInst = 1;
					break;
				}
			}
		}
		if (!isThereStoreInst)
			updatedInstrToIdxInst.emplace(var.first, var.second);
	}
	strLogger
		<< "\n*****\nReplacing Load Intructions After neck (Primitive)...\n";
	//check the users of alloc instructions after the neck
	//TODO i might need to check the alloc isn't modified after the neck before making CC
	//I can check that by making sure no store instr users after the neck
	std::vector<llvm::LoadInst *> ldToRemove;
	for (auto var : updatedInstrToIdxInst)
	{
		strLogger << "var: " << *var.first << "\n";
		for (auto usr : var.first->users())
		{
			strLogger << "\tUSER: " << *usr << "\n";
			if (auto ld = dyn_cast<llvm::LoadInst>(usr))
			{
				//chk if the load instr is after the neck
				if (returnIndex(instList, cast<Instruction>(usr)) > neckIndex(module, instList, funcName) && getVarName(instrToVarName,
																														cast<AllocaInst>(ld->getPointerOperand())) != "argc")
				{
					//make sure the alloc instr has a name, some alloc instr don't have a name, so I avoid making CC
					auto constVal = plocals.find(std::to_string(var.second));
					if (constVal != plocals.end())
						if (auto intType = dyn_cast<IntegerType>(
								var.first->getType()->getElementType()))
						{
							auto val = llvm::ConstantInt::get(intType,
															  constVal->second);
							strLogger << "\nLD replace.. \n";
							strLogger << "\tFOUND: " << *var.first << " :: "
									  << var.second << " :: " << *usr << "\n";
							ld->replaceAllUsesWith(val);
							ldToRemove.push_back(ld);
						}
				}
			}
		}
		//		llvm::outs() << "****BEFORE ldToRemove\n";
		//		for (auto ld : ldToRemove)
		//			if (ld->getNumUses() == 0) {
		//				ld->eraseFromParent();
		//				strLogger << "\t\tRemoving converted Load instr: " << *ld
		//						<< "\n";
		//			}
	}
	logger << strLogger.str();
}

void LocalVariables::handlePtrToPrimitiveLocalVariables(Module &module,
														map<uint64_t, pair<uint64_t, uint64_t>> &ptrToPrimtive,
														string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handlePtrToPrimitiveLocalVariables\n";
	//	strLogger << "Size instList: " << instList.size() <<"\n";

	map<uint64_t, Instruction *> idxInstToInst = getAllInstr(module);
	map<AllocaInst *, pair<uint64_t, uint64_t>> instrToIdxInst;

	//here I need to find the alloc instr based on actualIdxInst not the ptrIdxInst
	for (auto ptr : ptrToPrimtive)
	{
		auto it = idxInstToInst.find(ptr.second.first);
		if (it != idxInstToInst.end())
		{
			if (isa<AllocaInst>(it->second))
			{
				strLogger << "PTR:: " << *it->second << "\n";
				instrToIdxInst.emplace(cast<AllocaInst>(it->second), ptr.second);
			}
		}
	}

	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		if (fn == "main")
		{
			strLogger
				<< "*****\nStart Converting Pointer to Primitive locals to constant\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
			{
				for (auto curI = curB->begin(); curI != curB->end(); curI++)
				{
					if (returnIndex(instList, &*curI) < neckIndex(module, instList, funcName))
					{
						//handle load inst
						if (auto ld = dyn_cast<LoadInst>(curI))
						{
							strLogger << "ptr oprd: "
									  << *ld->getPointerOperand() << "---"
									  << *ld->getPointerOperand()->getType()
									  << "\n";
							//							auto al = dyn_cast<AllocaInst>(ld->getOperand(1));
							//							auto inst = instrToIdxInst.find(cast<AllocaInst>(si->getOperand(1)));
							//							auto val = llvm::ConstantInt::get(intType, inst->second.second);
							//							ReplaceInstWithValue(curB->getInstList(), curI,
							//																	val);
							//handle store inst
						}
						else if (auto si = dyn_cast<StoreInst>(curI))
						{
							if (returnIndex(instList, si) < neckIndex(module, instList, funcName))
							{
								strLogger << "SI:  " << *si << "\n";
								//strLogger << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

								if (isa<llvm::PointerType>(
										si->getOperand(1)->getType()) &&
									isa<AllocaInst>(si->getOperand(1)))
								{
									auto al = dyn_cast<AllocaInst>(
										si->getOperand(1));
									auto inst = instrToIdxInst.find(
										cast<AllocaInst>(
											si->getOperand(1)));
									if (inst != instrToIdxInst.end())
									{
										strLogger << "FOUND: " << *inst->first
												  << " --"
												  << *al->getType()->getElementType()
												  << "\n";
										strLogger << "\t" << inst->second.first
												  << " " << inst->second.second
												  << "\n";
										if (auto intType =
												dyn_cast<IntegerType>(
													al->getOperand(0)->getType()))
										{
											auto val = llvm::ConstantInt::get(
												intType,
												inst->second.second);
											strLogger << "\nSI replace.. \n";
											strLogger << "\tFOUND: "
													  << *inst->first << " :: "
													  << inst->second.first
													  << " :: " << *curI << "\n";
											StoreInst *str = new StoreInst(val,
																		   curI->getOperand(1));
											ReplaceInstWithInst(
												curB->getInstList(), curI,
												str);
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

void LocalVariables::handleStringVars(Module &module,
									  map<uint64_t, pair<uint64_t, string>> strList, string funcName)
{
	IRBuilder<> builder(module.getContext());
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleStringVars\n";
	for (auto elem : strList)
	{
		string constName = to_string(elem.first);
		strLogger << "ELEM: " << elem.first;
		auto it = instList[elem.first];
		strLogger << "\tFound elemInst:: " << *it << "\n";
		Constant *ary = llvm::ConstantDataArray::getString(module.getContext(),
														   elem.second.second, true);
		GlobalVariable *gv = new GlobalVariable(module, ary->getType(), true,
												GlobalValue::LinkageTypes::PrivateLinkage, ary, "");
		gv->setInitializer(ary);
		gv->setName(constName);
		Value *gv_i_ref = builder.CreateConstGEP2_32(
			cast<PointerType>(gv->getType())->getElementType(), gv, 0, 0);

		if (auto al = dyn_cast<AllocaInst>(it))
		{
			strLogger << "Found Alloc inst correspong to elem\n";
			//iterate all uses of the instr
			for (auto usr : al->users())
			{
				if (returnIndex(instList, cast<Instruction>(usr)) > neckIndex(module, instList, funcName))
				{
					strLogger << "users:: " << *usr << "\n";
					if (auto ld = dyn_cast<LoadInst>(usr))
					{
						strLogger << "\tFound LD \n";
						StoreInst *str = new StoreInst(gv_i_ref,
													   ld->getPointerOperand());
						str->insertBefore(ld);
					}
					else if (auto st = dyn_cast<StoreInst>(usr))
					{
						strLogger << "\tFound ST \n";
						//i need to replace the old GEP with a new GEP
						if (auto opr = dyn_cast<GetElementPtrInst>(
								st->getOperand(0)))
						{
							StoreInst *str = new StoreInst(gv_i_ref,
														   st->getOperand(1));
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
												   map<string, uint64_t> &plocals, string funcName)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "*****\nRun handleLocalVariables\n";
	map<AllocaInst *, uint64_t> stackVarToIdxInst;

	map<uint64_t, Instruction *> mapIdxInstInst = getAllInstr(module);

	map<AllocaInst *, std::string> instrToVarName;

	//I need to update plocals because I noticed some vars are included but their declaration is after the neck.
	//So I need to remove these stack vars
	map<string, uint64_t> updatedPlocals;

	strLogger
		<< "*****\nFind list of matching instructions that have index in locals.\n";
	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		//the assumption is that majority of the analysis on the local variables
		//should be conducted inside the main, this where the neck is
		if (fn == "main")
		{
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
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
			{
				for (auto curI = curB->begin(); curI != curB->end(); ++curI)
				{
					//to keep the index i consistent and matching the one in KLEE
					//strLogger <<"i: " << i << " --- " << *curI<< "\n";
					if (auto br = dyn_cast<BranchInst>(&*curI))
					{
						//check if the previous instr is br
						auto inst = mapIdxInstInst.find(i - 1);
						if (inst != mapIdxInstInst.end() && isa<BranchInst>(inst->second))
						{
							//strLogger << "****Found TWO Branches\n";
							--i;
						}
					}
					auto id = plocals.find(std::to_string(i));
					if (auto al = dyn_cast<llvm::AllocaInst>(curI))
					{
						if (id != plocals.end())
						{
							if (!isa<StructType>(
									al->getType()->getElementType()))
							{
								stackVarToIdxInst.emplace(
									cast<llvm::AllocaInst>(curI), i);
							}
						}
						//obtain the names of local variables, if applicable
						if (!al->hasName())
						{
							for (auto I = curB->begin(); I != curB->end();
								 I++)
							{
								if (DbgDeclareInst *dbg = dyn_cast<
										DbgDeclareInst>(&(*I)))
								{
									if (const AllocaInst *dbgAI = dyn_cast<
											AllocaInst>(dbg->getAddress()))
										if (dbgAI == al)
										{
											auto ff = returnIndex(instList, dbg) < neckIndex(module,
																							 instList, funcName);
											if (id != plocals.end() && ff)
												updatedPlocals.emplace(
													id->first, id->second);

											if (DILocalVariable *varMD =
													dbg->getVariable())
											{
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
						}
						else
						{
							strLogger << "NAME:: " << al->getName() << "\n";
						}
					}
					i++;
				}
			}
		}
	}

	for (auto curF = module.getFunctionList().begin(), endF =
														   module.getFunctionList().end();
		 curF != endF; ++curF)
	{
		string fn = curF->getName();
		if (fn == "main")
		{
			strLogger << "*****\nStart CC Primitive locals\n";
			for (auto curB = curF->begin(); curB != curF->end(); curB++)
			{
				for (auto curI = curB->begin(); curI != curB->end(); curI++)
				{
					//i need to add if store isnt and 1)its 2nd operand is alloc instr and 2)1st operand is constvalue, the
					//do the replacement
					if (auto ld = dyn_cast<LoadInst>(curI))
					{
						//						strLogger << "lstSize: " << instList.size() << " LD idxInst: " << returnIndex(instList, ld) << "--" << *ld <<"\n";
						if (returnIndex(instList, ld) < neckIndex(module, instList, funcName)) //do the conversion till the neck
							if (auto opr = dyn_cast<llvm::AllocaInst>(
									ld->getPointerOperand()))
							{
								//I need to ignore some local variables like argc, because the number of arguments will be
								//different after running the specalized apps. So argc variable should be delayed
								if (getVarName(instrToVarName, opr) == "argc")
									continue;

								//here I need to check the mapping list of local variables and their values
								auto inst = stackVarToIdxInst.find(opr);
								if (inst != stackVarToIdxInst.end())
								{
									auto constVal = plocals.find(
										std::to_string(inst->second));
									if (constVal != plocals.end())
									{
										if (auto intType =
												dyn_cast<IntegerType>(
													opr->getType()->getElementType()))
										{
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
					}
					else if (auto si = dyn_cast<StoreInst>(curI))
					{
						if (returnIndex(instList, si) < neckIndex(module, instList, funcName))
						{
							//							strLogger << "SI:  " << *si << "\n";
							//							strLogger << "\toperands:: " << si->getNumOperands() << " ---opr0: " << *si->getOperand(0) <<  " ---opr1: " << *si->getOperand(1) << "\n";

							if (isa<ConstantInt>(si->getOperand(0)))
								if (auto opr = dyn_cast<llvm::AllocaInst>(
										si->getOperand(1)))
								{
									auto inst = stackVarToIdxInst.find(opr);
									if (inst != stackVarToIdxInst.end())
									{
										auto constVal = plocals.find(
											std::to_string(inst->second));
										if (constVal != plocals.end())
										{
											if (auto intType =
													dyn_cast<IntegerType>(
														opr->getType()->getElementType()))
											{
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
	handleLocalPrimitiveUsesAfterNeck(module, updatedPlocals, stackVarToIdxInst,
									  instList, instrToVarName, funcName);
}

void LocalVariables::testing(Module &module)
{
	string str;
	raw_string_ostream strLogger(str);
	strLogger << "Inside test procedure...\n";
	for (auto g = module.global_begin(); g != module.global_end(); g++)
	{
		strLogger << "g: " << *g << "\n";
		for (Value::use_iterator u = g->use_begin(); u != g->use_end(); u++)
		{
			strLogger << "use: " << *u->getUser() << "\n";
		}
	}

	for (auto f = module.getFunctionList().begin();
		 f != module.getFunctionList().end(); f++)
	{
		strLogger << "Func: " << f->getName() << "---#USERS: "
				  << f->getNumUses() << "\n";
		for (auto bb = f->begin(); bb != f->end(); bb++)
		{
			for (auto i = bb->begin(); i != bb->end(); i++)
			{
				if (auto st = dyn_cast<StoreInst>(i))
				{
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
					outs() << "\tPTR oprnd: " << *st->getPointerOperand()
						   << "\n";
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
