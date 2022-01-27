#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <boost/optional.hpp>

#include "llvm/Analysis/MemoryLocation.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/IR/Function.h"
// #include "llvm/PassAnalysisSupport.h"
#include "llvm/Pass.h"

#include "llvm/Support/raw_os_ostream.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "CleaningUpStuff.h"
#include "GlobalVariables.h"
#include "LocalVariables.h"
#include <string.h>

#include <stdlib.h>

using namespace llvm;
using namespace std;
// using namespace lmcas;

#define DEBUG_TYPE "debloat"

namespace lmcas
{
  cl::opt<string>
      GlobalsFile("gblInt",
                  cl::desc("file containing global to constant mapping"));

  cl::opt<string> PrimitiveLocalsFile(
      "plocals", cl::desc("file containing primitive local to constant mapping"));

  cl::opt<string> PtrToPrimitiveLocalsFile(
      "ptrToPrimLocals",
      cl::desc("file containing pointer to primitive local to constant mapping"));

  cl::opt<string> PtrStrctLocalsFile(
      "ptrStructlocals",
      cl::desc("file containing customized local to constant mapping"));

  cl::opt<string> nestedStrctsFile(
      "nestedStrcts",
      cl::desc("file containing nested structs to constant mapping"));

  cl::opt<string>
      StringVarsLcl("stringVarsLcl",
                    cl::desc("file containing mapping of string variables"));

  cl::opt<string>
      StringVarsGbl("stringVarsGbl",
                    cl::desc("file containing mapping of string variables"));

  cl::opt<string> CustomizedLocalsFile(
      "clocals",
      cl::desc("file containing customized local to constant mapping"));

  cl::opt<string> BBFile("bbfile",
                         cl::desc("file containing visited basic blocks"));

  cl::opt<string> appName("appName",
                          cl::desc("specify the name of the app under analysis"));

  cl::opt<bool> SimplifyPredicates("simplifyPredicate", llvm::cl::desc(""),
                                   llvm::cl::init(false));

  cl::opt<bool>
      CleaningUp("cleanUp",
                 llvm::cl::desc("remove unused variables and functions"),
                 llvm::cl::init(false));

  set<pair<string, uint64_t>> populateBasicBlocks()
  {
    set<pair<string, uint64_t>> res;
    ifstream ifs(BBFile.c_str());
    string fn;
    uint64_t bbnum;
    while (ifs >> fn >> bbnum)
    {
      res.emplace(fn, bbnum);
    }
    return res;
  }

  map<string, uint64_t> populateIntVarsGbl()
  {
    map<string, uint64_t> res;
    ifstream ifs(GlobalsFile.c_str());
    uint64_t value;
    string line, name;
    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        name = tmpVect[0];
        value = strtoul(tmpVect[1].c_str(), nullptr, 10);
        res.emplace(name, value);
      }
    }
    return res;
  }

  map<uint64_t, pair<uint64_t, uint64_t>> populatePtrPrimitiveLocals()
  {
    map<uint64_t, pair<uint64_t, uint64_t>> res;
    ifstream ifs(PtrToPrimitiveLocalsFile.c_str());
    uint64_t ptrIdx;
    uint64_t actualIdx;
    uint64_t value;
    string line;
    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        ptrIdx = strtoul(tmpVect[0].c_str(), nullptr, 10);
        actualIdx = strtoul(tmpVect[1].c_str(), nullptr, 10);
        value = strtoul(tmpVect[2].c_str(), nullptr, 10);
        res.emplace(ptrIdx, make_pair(actualIdx, value));
      }
    }
    return res;
  }

  string getAppName()
  {
    string app = appName.c_str();
    return app;
  }

  map<string, string> populateStringVarsGbl()
  {
    map<string, string> res;
    ifstream ifs(StringVarsGbl.c_str());
    string line, name, value;
    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        name = tmpVect[0];
        value = tmpVect[1];
        res.emplace(name, value);
      }
    }
    // llvm::outs() << "populateStringVarsGbl: " << res.size() << "\n";
    // for (auto r : res)
    //   llvm::outs() << r.first << "  ::  " << r.second << "\n";
    return res;
  }

  map<uint64_t, pair<uint64_t, string>> populateStringVarsLcl()
  {
    map<uint64_t, pair<uint64_t, string>> res;
    ifstream ifs(StringVarsLcl.c_str());
    uint64_t ptrIdx;
    uint64_t actualIdx;
    string value, fileName, line;
    for (int i = 0; std::getline(ifs, line); i++)
    {
      // sometimes klee exports the name of the bitcode as a string
      // I need to exclude the lines that contain the filename
      /*if (i == 0){
    fileName = line;
    continue;
}*/
      if (strstr(line.c_str(), getAppName().c_str()) == NULL)

        if (line.find(" ") != std::string::npos)
        {
          auto tmpVect = splitString(line, ' ');
          ptrIdx = strtoul(tmpVect[0].c_str(), nullptr, 10);
          actualIdx = strtoul(tmpVect[1].c_str(), nullptr, 10);
          value = tmpVect[2]; // strtoul(tmpVect[2].c_str(), nullptr, 10);
          // i need to check the actualIdx not equal -1 before adding to the list.
          // I already chk that in KLEE but still the string variable is exported
          for (int c = 3; c < tmpVect.size(); c++)
          {
            value = value + ' ' + tmpVect[c];
          }

          // if (actualIdx != -1 && value.find(fileName) == std::string::npos)
          res.emplace(ptrIdx, make_pair(actualIdx, value));
        }
    }
    return res;
  }

  map<string, uint64_t> populatePrimitiveLocals()
  {
    map<string, uint64_t> res;
    ifstream ifs(PrimitiveLocalsFile.c_str());
    uint64_t value;
    string line, name;
    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        name = tmpVect[0];
        value = strtoul(tmpVect[1].c_str(), nullptr, 10);
        res.emplace(name, value);
      }
    }
    return res;
  }

  map<tuple<string, uint64_t, int>, uint64_t> populatePtrStrctLocals()
  {
    map<tuple<string, uint64_t, int>, uint64_t> ret;

    ifstream ifs(PtrStrctLocalsFile.c_str());
    uint64_t value, structElem;
    string line, structName;
    int allocIdx;

    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        structName = tmpVect[0];
        structElem = strtoul(tmpVect[1].c_str(), nullptr, 10);
        value = strtoul(tmpVect[2].c_str(), nullptr, 10);
        allocIdx = atoi(tmpVect[3].c_str());
        ret.emplace(make_tuple(structName, structElem, allocIdx), value);
      }
    }
    return ret;
  }

  map<tuple<string, uint64_t, int>, uint64_t> populateStructLocals()
  {
    map<tuple<string, uint64_t, int>, uint64_t> ret;

    ifstream ifs(CustomizedLocalsFile.c_str());
    uint64_t value, structElem;
    string line, structName;
    int allocIdx;

    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        splitString(line, ' ');
        auto tmpVect = splitString(line, ' ');
        structName = tmpVect[0];
        structElem = strtoul(tmpVect[1].c_str(), nullptr, 10);
        value = strtoul(tmpVect[2].c_str(), nullptr, 10);
        allocIdx = atoi(tmpVect[3].c_str());
        ret.emplace(make_tuple(structName, structElem, allocIdx), value);
      }
    }
    return ret;
  }

  // mainStrct-idxInMainStrct-structElem-idxStructElem-value--isPtrElem
  void populateNestedStructLocals(
      map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t>
          *nestedStrctNoPtr,
      map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t>
          *nestedStrctPtr)
  {

    ifstream ifs(nestedStrctsFile.c_str());
    uint64_t idxInMainStrct, idxStructElem, value, isPtrElem;
    std::string line, mainStrct, structElem;
    int allocIdx;

    for (int i = 0; std::getline(ifs, line); i++)
    {
      if (line.find(" ") != std::string::npos)
      {
        auto tmpVect = splitString(line, ' ');
        mainStrct = tmpVect[0];
        structElem = tmpVect[2];
        idxInMainStrct = strtoul(tmpVect[1].c_str(), nullptr, 10);
        idxStructElem = strtoul(tmpVect[3].c_str(), nullptr, 10);
        value = strtoul(tmpVect[4].c_str(), nullptr, 10);
        isPtrElem = strtoul(tmpVect[5].c_str(), nullptr, 10);
        allocIdx = strtoul(tmpVect[6].c_str(), nullptr, 10);
        if (isPtrElem == 1)
        {
          nestedStrctPtr->emplace(make_tuple(mainStrct, structElem,
                                             idxInMainStrct, idxStructElem,
                                             allocIdx),
                                  value);
        }
        else
        {
          nestedStrctNoPtr->emplace(make_tuple(mainStrct, structElem,
                                               idxInMainStrct, idxStructElem,
                                               allocIdx),
                                    value);
        }
      }
    }
  }
  /// return only fn, then I can get the name
  // boost::optional<pair<string,CallInst*>>
  boost::optional<llvm::Function &> findTheNeckHostingFunc(Module &module)
  {
    for (auto fn = module.getFunctionList().begin();
         fn != module.getFunctionList().end(); fn++)
      for (auto bb = fn->getBasicBlockList().begin();
           bb != fn->getBasicBlockList().end(); bb++)
        for (auto I = bb->getInstList().begin(); I != bb->getInstList().end();
             I++)
          if (CallInst *callSite = dyn_cast<CallInst>(I))
            if (callSite->getCalledFunction() &&
                callSite->getCalledFunction()->getName() == "klee_dump_memory")
            {
              // return std::make_pair(fn->getName().str(),callSite);
              Function &f = const_cast<Function &>(fn->getFunction());
              return f;
            }
    return boost::none;
  }

  /*
   * this method adds the missing basic blocks till reach the neck. as klee
   * captures only the executed BBs
   */
  void updateVisitedBasicBlocks(Module &module,
                                set<pair<string, uint64_t>> &visitedBbs)
  {
    string name;
    int i;
    for (auto fn = module.getFunctionList().begin();
         fn != module.getFunctionList().end(); fn++)
    {
      name = fn->getName().str();
      i = 0;
      for (auto bb = fn->getBasicBlockList().begin();
           bb != fn->getBasicBlockList().end(); bb++)
      {
        for (auto I = bb->getInstList().begin(); I != bb->getInstList().end();
             I++)
        {
          if (CallInst *callSite = dyn_cast<CallInst>(I))
          {
            if (callSite->getCalledFunction() &&
                callSite->getCalledFunction()->getName() == "klee_dump_memory")
            {
              goto updateBB;
            }
          }
        }
        i++;
      }
    }
  updateBB:
    for (int c = 0; c < i; c++)
    {
      visitedBbs.emplace(name, c);
    }
  }

  struct Debloat : public ModulePass
  {
    static char ID; // Pass identification, replacement for typeid
    Debloat() : ModulePass(ID) {}

    bool runOnModule(Module &module) override
    {

      //		compute SCC for a CFG
      // Use LLVM's Strongly Connected Components (SCCs) iterator to produce
      // a reverse topological sort of SCCs.
      /*for (auto &F : module.getFunctionList()){
                    if (F.getName() == "main"){
                            outs() << "Func: " << F.getName() << "\n";
                            for (scc_iterator<Function *> I = scc_begin(&F),
                                                          IE = scc_end(&F);
                                                          I != IE; ++I) {
                              // Obtain the vector of BBs in this SCC and print
       it out. const std::vector<BasicBlock *> &SCCBBs = *I; outs() << "  SCC:
       "; for (std::vector<BasicBlock *>::const_iterator BBI = SCCBBs.begin(),
                                                                             BBIE
       = SCCBBs.end(); BBI != BBIE; ++BBI) { std::string Str; raw_string_ostream
       OS(Str);

                                          (*BBI)->printAsOperand(OS, false);

                                      outs() << OS.str() << "  ";
                              }
                              outs() << "\n";
                            }
                    }
            }*/

      //		AliasAnalysis& AA = getAnalysis<AliasAnalysis>();
      //		AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
      if (!CleaningUp)
      {
        set<pair<string, uint64_t>> visitedBbs = populateBasicBlocks();
        updateVisitedBasicBlocks(module, visitedBbs);
        map<string, uint64_t> gblInt = populateIntVarsGbl();
        map<string, string> strGbl = populateStringVarsGbl();
        map<string, uint64_t> plocals = populatePrimitiveLocals();
        map<uint64_t, pair<uint64_t, uint64_t>> ptrPrimLocals =
            populatePtrPrimitiveLocals();
        map<uint64_t, pair<uint64_t, string>> strVars = populateStringVarsLcl();
        map<tuple<std::string, uint64_t, int>, uint64_t> ptrStrLocals =
            populatePtrStrctLocals();
        map<tuple<std::string, uint64_t, int>, uint64_t> structLocals =
            populateStructLocals();

        map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t>
            nestedStrctNoPtr, nestedStrctPtr;
        populateNestedStructLocals(&nestedStrctNoPtr, &nestedStrctPtr);

        if (auto neckCallerFuncInfo = findTheNeckHostingFunc(module))
        {
          string funcName = neckCallerFuncInfo->getName().str();
          auto neckCaller = neckCallerFuncInfo.get_ptr();

          GlobalVariables gv(module, funcName);

          if (gblInt.size() != 0)
          {
            outs() << "\nConvert Global variables: " << gblInt.size() << "\n";
            gv.handleGlobalVariables(module, gblInt, visitedBbs, funcName,
                                     neckCaller);
          }

          if (strGbl.size() != 0)
          {
            outs() << "\nConvert Global string variables: " << strGbl.size() << "\n";
            gv.handleStringVarsGbl(module, strGbl);
          }

          LocalVariables lv(module, funcName);

          if (plocals.size() != 0)
          {
            outs() << "\nConvert Primitive Locals: " << plocals.size() << "\n";
            lv.handlePrimitiveLocalVariables(module, plocals, funcName,
                                             neckCaller);
          }

          if (ptrPrimLocals.size() != 0)
          {
            outs() << "\nConvert Pointer Primitive Locals: "
                   << ptrPrimLocals.size() << "\n";
            lv.handlePtrToPrimitiveLocalVariables(module, ptrPrimLocals, funcName,
                                                  neckCaller);
          }

          if (structLocals.size() != 0)
          {
            outs() << "\nConvert Struct to Locals: " << structLocals.size()
                   << "\n";
            lv.handleStructLocalVars(module, structLocals, funcName, neckCaller);
          }

          if (ptrStrLocals.size() != 0)
          {
            outs() << "\nConvert pointer to struct Locals: "
                   << ptrStrLocals.size() << "\n";
            lv.handlePtrToStrctLocalVars(module, ptrStrLocals, funcName,
                                         neckCaller);
          }

          if (strVars.size() != 0)
          {
            outs() << "\nConvert string variables: " << strVars.size() << "\n";
            lv.handleStringVars(module, strVars, funcName, neckCaller);
          }

          if (nestedStrctNoPtr.size() != 0)
          {
            outs() << "\nConvert nested struct (no ptr) variables: "
                   << nestedStrctNoPtr.size() << "\n";
            lv.handleNestedStrct(module, nestedStrctNoPtr, funcName, neckCaller);
          }

          if (nestedStrctPtr.size() != 0)
          {
            outs() << "\nConvert nested struct ptr variables: "
                   << nestedStrctPtr.size() << "\n";
            lv.handlePtrToNestedStrct(module, nestedStrctPtr, funcName,
                                      neckCaller);
          }
        }
      }

      if (CleaningUp)
      {
        outs() << "\nPerform CleaningUp\n";
        CleaningUpStuff cp;
        cp.removeUnusedStuff(module);
        //			AliasAnalysis &AA =
        // getAnalysis<AAResultsWrapperPass>().getAAResults();
        // lv.testing(module);
      }
      return true;
    }

    /* bool runOnFunction(Function &F) override {
AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  errs() << "Hello: ";
        errs().write_escaped(F.getName()) << '\n';
SetVector<Value *> Pointers;
for (Argument &A : F.args())
if (A.getType()->isPointerTy())
  Pointers.insert(&A);
for (Instruction &I : instructions(F))
if (I.getType()->isPointerTy())
  Pointers.insert(&I);

outs() << "mm: " <<Pointers.size() << "\n";
for (auto m : Pointers){
  outs() << "\tptr: "<< *m << "\n";
  Instruction* i = cast<Instruction>(m);
  outs() << "\t\tmem: " << i->mayWriteToMemory() << "\n";
}
for (Value *P1 : Pointers)
for (Value *P2 : Pointers)
  (void)AA.alias(P1, MemoryLocation::UnknownSize, P2,
                 MemoryLocation::UnknownSize);
for (auto i = inst_begin(F); i != inst_end(F); i++){
  Instruction &Inst = *i;
  outs() << "Inst: " << *i <<"\n";
  if (isa<PointerType>(i->getType())){
          outs() << "\tPOINTER_TYPE" << "\n";
          if (auto ld = dyn_cast<LoadInst>(&Inst)){
                  outs() << "\t#oprds: " << ld->getNumOperands() << "\n";
                  outs() << "\t\tld: " << *ld <<"\n";
                  outs() << "\t\tld: " << *ld->stripPointerCasts() <<"\n";
                  outs() << "\t\ttype: " <<
*ld->getPointerOperand()->stripPointerCasts()<<"\n";

                  if (isa<PointerType>(ld->getOperand(0)->getType())){
                          outs() << "\t\tFOUND PTR\n";
                  }
          }
//	    		if (auto al = dyn_cast<AllocaInst>(&Inst)){
//	    			outs() << "\t#oprds: " << al->getNumOperands()
<< "\n";
//	    			outs() << "\t\ttype: " <<
*al->getOperand(0)->getType() <<"\n";
//	    		}
  }
//	    	outs() << "\t\tmem: " << i->mayWriteToMemory() << "\n";
}

return false;
}*/

    /*void getAnalysisUsage(AnalysisUsage& AU) const
{
//AU.addRequired<AliasAnalysis>();
  AU.addRequired<AAResultsWrapperPass>();
AU.setPreservesAll();
}*/
  };

  char Debloat::ID = 0;
  static RegisterPass<Debloat> X("debloat", "Debloat Pass");
} // namespace lmcas
