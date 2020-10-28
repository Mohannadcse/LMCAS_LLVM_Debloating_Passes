/*
 * Predicates.cpp
 *
 *  Created on: Aug 23, 2020
 *      Author: ma481
 */

#include "Predicates.h"

void Predicates::handlePredicates(Module &module) {
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
						outs() << "\topr1: " << c1->getSExtValue() << " :: opr2=" << c2->getSExtValue() << "\n";
						outs() << "\tTy: " <<* c1->getType() << " :: opr2=" << *c2->getType() << "\n";

						if (c1->getSExtValue() == c2->getSExtValue())
							outs() << "\toperands are equal\n";
						else
							outs() << "\toperands aren't equal\n";

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





