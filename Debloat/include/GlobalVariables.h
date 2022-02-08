/*
 * GlobalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_

// #include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/FileSystem.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <map>
#include <set>

#include "Utility.h"

using namespace llvm;
using namespace std;

// typedef string Global;
// typedef pair<string, uint64_t> BasicBlocks;
namespace lmcas
{
  class GlobalVariables
  {
  private:
    // ofstream logger;
    llvm::raw_fd_ostream *logger;
    vector<Instruction *> instList;
    map<GlobalVariable *, StoreInst *> gblStoreInstAfterNeck;
    map<string, uint64_t> gblIntCCBeforeNeckOnly;
    void removeModifiedVarsAfterNeck(Module &module,
                                     map<string, uint64_t> &newGlobals,
                                     string funcName, Function *neckCaller);

  public:
    void handleGlobalVariables(Module &module, map<string, uint64_t> &globals,
                               set<pair<string, uint64_t>> visitedBbs, string,
                               Function *);
    void handleStringVarsGbl(Module &module, map<string, string> strList);
    GlobalVariables(Module &module, string funcName)
    {
      std::error_code EC;
      logger = new raw_fd_ostream("logger.txt", EC, llvm::sys::fs::OF_Append);
      instList = initalizePreNeckInstList(module, funcName);
    }
  };
} // namespace lmcas

#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_GLOBALVARIABLES_H_ */
