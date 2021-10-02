/*
 * CleaningUp.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "CleaningUpStuff.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"

using namespace std;

void CleaningUpStuff::dfsutils(CallGraph &cg, Function* f, std::set<const Function*>&reachableFunctionsFromMain){
	string str;
	raw_string_ostream strLogger(str);
	for (auto n = cg.begin(); n !=cg.end(); n++){

		if (n->first && n->first->getFunction().getName() == f->getName()){
			strLogger << "Callee: " << n->first->getName() <<"\n";
			if (n->second){
				CallGraphNode *cgn = n->second.get();
				strLogger << "\tSize: " << cgn->size() << "\n";
				for (int i = 0; i < cgn->size(); i++){
					if(cgn->operator[](i)->getFunction() && cgn->operator[](i)->getFunction()->isDeclaration()){
						strLogger<<"\tidx"<< i << ": " <<cgn->operator[](i)->getFunction()->getName()<<"\n";
						reachableFunctionsFromMain.insert(cgn->operator[](i)->getFunction());
						dfsutils(cg, cgn->operator[](i)->getFunction(), reachableFunctionsFromMain);
					}
				}
			}
		}
	}
	logger << strLogger.str();
}

//This method removes unused local/global variables and functions
void CleaningUpStuff::removeUnusedStuff(Module &module) {
	string str;
	raw_string_ostream strLogger(str);

	strLogger << "TotalFuncBefore: " << module.getFunctionList().size() <<"\n";

	/*
	 * The entry point is the mian function. Therefore, I need to identify all called methods inside the main
	 * I store them in a set (i don't want repeated nodes)
	 * then I can get the invoked functions from each function
	 * Here I don't care about constructing paths from main to reachable functions
	 */
	std::set<const Function*> reachableFunctionsFromMain;
	auto cg = CallGraph(module);
	strLogger << "CG:: \n";
	for (auto n = cg.begin(); n !=cg.end(); n++){
		if (n->first && n->first->getName() == "main"){
			reachableFunctionsFromMain.insert(n->first);
			strLogger << "func: " << n->first->getName() << "\n";
			if (n->second){
				CallGraphNode *cgn = n->second.get();
				strLogger << "\tSize: " << cgn->size() << "\n";
				for (int i = 0; i < cgn->size(); i++){
					if(cgn->operator[](i)->getFunction()){
						strLogger <<"\tidx:"<< i << ": " <<cgn->operator[](i)->getFunction()->getName()<<"\n";
						reachableFunctionsFromMain.insert(cgn->operator[](i)->getFunction());
						dfsutils(cg, cgn->operator[](i)->getFunction(), reachableFunctionsFromMain);
					}
				}
			}
		}
	}

	std::vector<Function*> funcToBeRemovedCG;
	strLogger << "Reachable Funcs: \n";
		for (auto e : reachableFunctionsFromMain){
			strLogger << e->getName() << " >> ";
		}
		strLogger << "\n\n";

	strLogger << "Removing Functions from the module....\n";
	for (auto curF = module.getFunctionList().begin(); curF != module.getFunctionList().end(); curF++){
		strLogger << "Iterating: " << curF->getName() << "\n";
		if (reachableFunctionsFromMain.find(&curF->getFunction()) == reachableFunctionsFromMain.end()){
			Function &f = const_cast<Function&>(curF->getFunction());
			strLogger << "\tRemoving process of func: " << f.getName()  << "-- Uses: " << curF->getNumUses() << "\n";
			for (User* usr : f.users()){
				if (Instruction* inst = dyn_cast<Instruction>(usr)){
					strLogger << "\t\tUsers: " << *inst << " --Name: " <<inst->getParent()->getParent()->getName()<<"\n";
					//inst->eraseFromParent();
					strLogger << "\t\tRemoved users correctly!" <<"\n";
				}
			}
			if (curF->getNumUses() == 0){
				funcToBeRemovedCG.push_back(&*curF);
				strLogger << "Removed func correctly!\n";
			}
		}
	}

	strLogger << "\n\nfuncToBeRemovedCG \n";
	for (auto f : funcToBeRemovedCG) {
		strLogger << "FuncName: " << f->getName() << "\n";
		f->eraseFromParent();
		strLogger << "\tRemoved func correctly!\n";
	}
	strLogger << "NumFuncAfterCleanCG: " << module.getFunctionList().size() << "\n";

	std::vector<Function*> funcToBeRemoved;
	std::vector<GlobalVariable*> gblVarsToBeRemoved;
	std::vector<Instruction*> localVarsToBeRemoved;
	std::vector<Instruction*> storeInstToBeRemoved;
	//identify uses of a function, if it's zero then remove the function
	for (auto curF = module.getFunctionList().begin();
			curF != module.getFunctionList().end(); curF++) {
		if (curF->getName() != "main") {
			//			strLogger << "FuncName: " << curF->getName() << " NumUses= " << curF->getNumUses() << "\n";
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
			//TODO if the uses of the variable are all store,
			//then I can safely remove it
			//remove alloc inst that only used once in a store instr.
			//because this is probably where the var is initialized
			//the alloc inst should be the 2nd operand of the store instr
			//I created 2 Instruction vectors: aloc instrs and stor instr. Because I need to remove store instrs before alloc instr
			//otherwise, I'll receive errors if the alloc was removed before its store insr
			for (auto I = inst_begin(*curF); I != inst_end(*curF); I++) {
				Instruction *i = &*I;
				if (auto ai = dyn_cast<AllocaInst>(i)) {
					//						strLogger << "Var: " << *ai << " , Used = " << ai->getNumUses() << "\n";
					for (auto u : ai->users()) {
						//							strLogger << "\tUses: " << *u <<"\n";
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

	for (auto str : storeInstToBeRemoved) {
		str->eraseFromParent();
	}

	for (auto l : localVarsToBeRemoved) {
		l->eraseFromParent();
	}

	logger << strLogger.str();
}




