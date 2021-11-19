/*
 * Utility.cpp
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#include "Utility.h"

using namespace lmcas;

namespace lmcas
{

  vector<std::string> splitString(string &str, char delim)
  {
    vector<std::string> strToVec;

    std::size_t current, previous = 0;
    current = str.find(delim);
    while (current != std::string::npos)
    {
      strToVec.push_back(str.substr(previous, current - previous));
      previous = current + 1;
      current = str.find(delim, previous);
    }
    strToVec.push_back(str.substr(previous, current - previous));
    return strToVec;
  }

  uint32_t neckIndex(Module &module, vector<Instruction *> &instList,
                     string funcName)
  {
    instList.clear();
    uint32_t neckIdxInst = 0;
    for (auto curF = module.getFunctionList().begin(),
              endF = module.getFunctionList().end();
         curF != endF; ++curF)
    {
      string fn = curF->getName().str();
      if (fn == funcName)
      {
        uint64_t i = 0;
        // identify the index of the neck
        for (auto curB = curF->begin(); curB != curF->end(); curB++)
        {
          for (auto curI = curB->begin(); curI != curB->end(); curI++, i++)
          {
            Instruction *inst = &*curI;
            instList.push_back(inst);
            if (auto cs = dyn_cast<CallInst>(curI))
            {
              // if the call is indirect call then getCalledFunction returns null
              if (cs->getCalledFunction())
                if (cs->getCalledFunction()->getName() == "klee_dump_memory")
                {
                  neckIdxInst = i;
                  // strLogger << "Neck Found@: " << neckIdxInst << "---Size: " <<
                  // instList.size() << "\n";
                  return neckIdxInst;
                }
            }
          }
        }
      }
    }
    return neckIdxInst;
  }

  int returnIndex(std::vector<Instruction *> list, Instruction *inst)
  {
    int i = -1;
    auto it = find(list.begin(), list.end(), inst);
    if (it != list.cend())
    {
      i = std::distance(list.begin(), it);
    }
    return i;
  }

  vector<Instruction *> initalizePreNeckInstList(Module &module,
                                                 string funcName)
  {
    vector<Instruction *> instList;
    for (auto curF = module.getFunctionList().begin(),
              endF = module.getFunctionList().end();
         curF != endF; ++curF)
    {
      string fn = curF->getName().str();
      if (fn == funcName)
      {
        uint64_t i = 0;
        for (auto curB = curF->begin(); curB != curF->end(); curB++)
          for (auto curI = curB->begin(); curI != curB->end(); curI++, i++)
          {
            Instruction *inst = &*curI;
            instList.push_back(inst);
            if (auto cs = dyn_cast<CallInst>(curI))
              if (cs->getCalledFunction())
                if (cs->getCalledFunction()->getName() == "klee_dump_memory")
                  // break;
                  return instList;
          }
      }
    }
  }

} // namespace lmcas
