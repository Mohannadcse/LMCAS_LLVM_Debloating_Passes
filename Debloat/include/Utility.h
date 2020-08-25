/*
 * Utility.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_

#include "llvm/IR/Module.h"
#include <fstream>

using namespace llvm;
using namespace std;

vector<std::string> splitString(std::string &str, char delim);



#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_UTILITY_H_ */
