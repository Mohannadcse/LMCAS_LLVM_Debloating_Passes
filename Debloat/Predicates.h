/*
 * Predicates.h
 *
 *  Created on: Aug 23, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_PREDICATES_H_
#define SOURCE_DIRECTORY__DEBLOAT_PREDICATES_H_

#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/BasicBlock.h"

using namespace llvm;

class Predicates {
public:
	void handlePredicates(Module &module);
};



#endif /* SOURCE_DIRECTORY__DEBLOAT_PREDICATES_H_ */
