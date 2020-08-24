/*
 * CleaningUp.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "CleaningUpStuff.h"

//This method removes unused local/global variables and functions
void CleaningUpStuff::removeUnusedStuff(Module &module) {
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




