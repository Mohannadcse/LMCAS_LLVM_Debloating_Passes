/*
 * GlobalVariables.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "GlobalVariables.h"
#include "llvm/Analysis/CallGraph.h"

#include "Utility.h"

namespace lmcas {
void eraseElem(map<string, uint64_t> &globals, string key) {
  auto it = globals.find(key);
  if (it != globals.end()) {
    globals.erase(it);
  }
}

/** @brief the following logic to eliminate CC of global variables that are
 * after the neck but used in functions other than where the neck is invoked
 * However, sometimes the function call is used as operand of a store instr,
 * for example the function `dump_packet_and_trunc` is an operand for store
 * void (i8*, %struct.pcap_pkthdr*, i8*)*
 * @dump_packet_and_trunc, void (i8*, %struct.pcap_pkthdr*, i8*)** %19
 * I found this example in tcpdump
 * FIXME: i might need to traverse CG in case user functions are not invoked
 * directly in side the function that contains the neck
 */
void GlobalVariables::removeModifiedVarsAfterNeck(
    Module &module, map<string, uint64_t> &newGlobals, string funcName,
    Function *neckCaller) {
  for (auto &gbl : module.globals()) {
    auto it = newGlobals.find(gbl.getName());
    if (it != newGlobals.end()) {
      std::set<string> funcNamesGblUsers;
      logger << "Gbl Name: " << it->first << "\n";
      for (auto usr : gbl.users()) {
        if (auto si = dyn_cast<StoreInst>(usr)) {
          funcNamesGblUsers.insert(si->getFunction()->getName());
        }
      }

      for (auto curI = inst_begin(neckCaller); curI != inst_end(neckCaller);
           curI++) {
        // the func call user that modifes the var should be after the neck
        // once the var is removed, exit the loop
        if (auto *ci = dyn_cast<CallInst>(&*curI)) {
          if (returnIndex(instList, ci) > neckIndex(module, instList, funcName))
            if (ci->getCalledFunction()) {
              if (funcNamesGblUsers.find(
                      ci->getCalledFunction()->getName().str()) !=
                  funcNamesGblUsers.end()) {
                logger << "\nERASE ELEM CI*****: \n";
                eraseElem(newGlobals, gbl.getName());
                break;
              }
            }
        } else if (auto *st = dyn_cast<StoreInst>(&*curI)) {
          if (returnIndex(instList, st) > neckIndex(module, instList, funcName))
            if (funcNamesGblUsers.find(st->getOperand(0)->getName()) !=
                funcNamesGblUsers.end()) {
              logger << "\nERASE ELEM SI***** \n";
              eraseElem(newGlobals, gbl.getName());
              break;
            }
        }
      }
    }
  }
}

void GlobalVariables::handleGlobalVariables(
    Module &module, map<string, uint64_t> &globals,
    set<pair<string, uint64_t>> visitedBbs, string funcName,
    Function *neckCaller) {
  logger << "\nRun handleGlobalVariables\n";
  // set<BasicBlock> visitedBbs = populateBasicBlocks();
  //	updateVisitedBasicBlocks(module, visitedBbs);
  for (auto &gbl : module.getGlobalList()) {
    if (!gbl.getName().contains("str"))
      logger << "gbl:: " << gbl.getName().str() << "\n";
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
    eraseElem(globals, "optind");
    logger << "Remove optind"
           << "\n";
  }

  /** @brief the goal here is to keep global variables that are in this module
   *and remove others that are collected using KLEE this logic can be simplified
   */
  map<string, uint64_t> newGlobals;
  for (auto curF = module.getFunctionList().begin();
       curF != module.getFunctionList().end(); curF++) {
    string fn = curF->getName();
    if (fn == funcName) {
      for (auto curB = curF->begin(); curB != curF->end(); curB++) {
        for (auto curI = curB->begin(); curI != curB->end(); curI++) {
          if (auto si = dyn_cast<StoreInst>(&(*curI))) {
            if (GlobalVariable *gvar =
                    dyn_cast<GlobalVariable>(si->getPointerOperand())) {
              auto it = globals.find(gvar->getName());
              if (it != globals.end()) {
                newGlobals.insert(*it);
              }
            }
          } else if (auto li = dyn_cast<LoadInst>(&(*curI))) {
            if (GlobalVariable *gvar =
                    dyn_cast<GlobalVariable>(li->getPointerOperand())) {
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
    logger << kv.first << " " << kv.second << "\n";
  }

  logger << "BEFORE # ELEM: " << newGlobals.size() << "\n";
  removeModifiedVarsAfterNeck(module, newGlobals, funcName, neckCaller);
  logger << "AFTER # ELEM: " << newGlobals.size() << "\n";

  /** @brief I created a map to store the LD instr and it's corresponding value
   * to handle the following situation, where there are two subsequent LD instr,
   * so when I ReplaceInstWithValue, the counter will be incremanted and thus
   * miss converting the 2nd LD %11 = load i32, i32* @human_output_opts %12 =
   * load i64, i64* @output_block_size
   */
  //
  map<Instruction *, ConstantInt *> loadInstToReplace;

  // make remaining globals constant
  for (auto curF = module.getFunctionList().begin(),
            endF = module.getFunctionList().end();
       curF != endF; ++curF) {
    string fn = curF->getName();

    uint32_t bbnum = 0;
    for (auto curB = curF->begin(), endB = curF->end(); curB != endB;
         ++curB, ++bbnum) {

      for (auto curI = curB->begin(); curI != curB->end(); curI++) {
        if (auto li = dyn_cast<LoadInst>(&(*curI))) {
          if (GlobalVariable *gvar =
                  dyn_cast<GlobalVariable>(li->getPointerOperand())) {
            auto it = newGlobals.find(gvar->getName());
            if (it != newGlobals.end()) {
              GlobalVariable *gvar = module.getGlobalVariable(it->first, true);
              assert(gvar);
              if (auto intType = dyn_cast<IntegerType>(
                      gvar->getType()->getElementType())) {
                auto val = ConstantInt::get(intType, it->second);

                Instruction *in = &*curI;
                loadInstToReplace.emplace(in, val);
              }
            }
          }
        }
      }
    }
  }

  // replace load instr with const
  for (auto elem : loadInstToReplace) {
    BasicBlock::iterator ii(elem.first);
    //		logger << "\tReplace: " << elem.first << " WithVal: " <<
    // elem.second->getZExtValue() << "\n";
    ReplaceInstWithValue(elem.first->getParent()->getInstList(), ii,
                         elem.second);
  }
}
} // namespace lmcas
