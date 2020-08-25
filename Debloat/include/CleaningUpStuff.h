/*
 * CleaningUp.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_
#define SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_

#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/IR/Function.h"

using namespace llvm;

class CleaningUpStuff {
public:
	void removeUnusedStuff(Module &module);
};



#endif /* SOURCE_DIRECTORY__DEBLOAT_CLEANINGUPCLS_H_ */
