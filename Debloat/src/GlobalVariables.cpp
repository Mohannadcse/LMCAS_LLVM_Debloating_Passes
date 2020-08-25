/*
 * GlobalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "GlobalVariables.h"


//typedef pair<string, uint64_t> BasicBlocks;

void GlobalVariables::handleGlobalVariables(Module &module, map<string, uint64_t> &globals,
		set<pair<string, uint64_t>> visitedBbs) {
	logger << "\nRun handleGlobalVariables\n";
	//set<BasicBlock> visitedBbs = populateBasicBlocks();
//	updateVisitedBasicBlocks(module, visitedBbs);

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
		errs() << kv.first << " " << kv.second << "\n";
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
	logger << "Remaind Variables After 2nd iteration\n";
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
			if (visitedBbs.find(pair<string, uint64_t>(fn, bbnum)) != visitedBbs.end())
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
								auto val = ConstantInt::get(intType,
										it->second);
								//								logger << "\tFOUND: " << it->first
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




