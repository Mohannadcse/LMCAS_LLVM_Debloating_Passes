/*
 * GlobalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "GlobalVariables.h"


void GlobalVariables::handleGlobalVariables(Module &module, map<string, uint64_t> &globals,
		set<pair<string, uint64_t>> visitedBbs, string funcName) {
	logger << "\nRun handleGlobalVariables\n";
	//set<BasicBlock> visitedBbs = populateBasicBlocks();
	//	updateVisitedBasicBlocks(module, visitedBbs);

	for (auto &gbl : module.getGlobalList()){
		if (! gbl.getName().contains("str"))
			logger  << "gbl:: " <<gbl.getName().str() << "\n";
	}
	// identify globals in this module and delete the rest
	for (auto it = globals.cbegin(); it != globals.cend();) {
		if (module.getGlobalVariable(it->first, true))
			++it;
		else {
			it = globals.erase(it);
		}
	}

	logger << "Remaind Variables After 1st iteration\n";
	for (auto &&kv : globals) {
		logger << kv.first << " " << kv.second << "\n";
		auto it = globals.find("optind");
		if (it != globals.end()) {
			logger << "Remove optind" << "\n";
			globals.erase(it);
		}
	}

	map<string, uint64_t> newGlobals;
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		string fn = curF->getName();
		if (fn == funcName) {
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
	//	newGlobals.emplace("rfc_email_format", 37);
	logger << "Remaind Variables After 2nd iteration\n";
	for (auto &&kv : newGlobals) {
		logger << kv.first << " " << kv.second << "\n";
	}

	/*
	 * I created a map to store the LD instr and it's corresponding value to handle the following situation, where there are
	 * two subsequent LD instr, so when I ReplaceInstWithValue, the counter will be incremanted and thus miss converting the 2nd LD
	 * %11 = load i32, i32* @human_output_opts
	 * %12 = load i64, i64* @output_block_size
	 */
	//
	map<Instruction*, ConstantInt*> loadInstToReplace;

	// make remaining globals constant
	for (auto curF = module.getFunctionList().begin(), endF =
			module.getFunctionList().end(); curF != endF; ++curF) {
		string fn = curF->getName();

		uint32_t bbnum = 0;
		for (auto curB = curF->begin(), endB = curF->end(); curB != endB;
				++curB, ++bbnum) {

			for (auto curI = curB->begin(); curI != curB->end(); curI++){
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
								auto val = ConstantInt::get(intType,
										it->second);

								Instruction* in = &*curI;
								loadInstToReplace.emplace(in, val);
							}
						}
					}
				}
			}
		}
	}

	//replace load instr with const
	for (auto elem : loadInstToReplace){
		BasicBlock::iterator ii(elem.first);
//		logger << "\tReplace: " << elem.first << " WithVal: " << elem.second->getZExtValue() << "\n";
		ReplaceInstWithValue(elem.first->getParent()->getInstList(), ii, elem.second);
	}
}




