#include <cassert>
#include <vector>
#include <set>

#include "llvm/ADT/SetVector.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/MemoryLocation.h"
#if LLVM_VERSION_MAJOR >= 4 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 5)
  #include "llvm/IR/InstIterator.h"
#else
  #include "llvm/Support/InstIterator.h"
#endif
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

class PostNeckConstConversion : public FunctionPass {
  public:
    static char ID;

    PostNeckConstConversion() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override;
    bool mustAlias(Value *v1, Value *v2);
    bool replace(AllocaInst *var, Constant *C, Instruction* I);
    bool propagate(Instruction& Loc, AllocaInst *var, Constant *C, AAResults &AA);
//    AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
    //    AliasSetTracker *Tracker = new AliasSetTracker(AA);
//    virtual

     void getAnalysisUsage(AnalysisUsage& AU) const override
    	{
    	    //AU.addRequired<AliasAnalysis>();
    		AU.addRequired<AAResultsWrapperPass>();
    	    AU.setPreservesAll();
    	}
};

static RegisterPass<PostNeckConstConversion> PC("propagate-constants",
                                           "Propagate constants");
char PostNeckConstConversion::ID;

bool PostNeckConstConversion::mustAlias(Value *v1, Value *v2) {
  // TODO: use alias analysis here
//	auto m = PostNeckConstConversion::AA.alias(v1, v2);
//	outs() << "mm: " << m << "\n";
  if (auto *I1 = dyn_cast<Instruction>(v1)) {
    if (auto *I2 = dyn_cast<Instruction>(v2)) {
    	outs() << "I1: " << *I1->stripPointerCasts() << "\n" <<"I2: "<< *I2->stripPointerCasts() <<"\n";
      return I1->stripPointerCasts() == I2->stripPointerCasts();
    }
  }
  return false;
}

bool mayAlias(Value *v1, Value *v2) {
  // TODO: use alias analysis here

  if (auto *A1 = dyn_cast<AllocaInst>(v1->stripPointerCasts())) {
    if (auto *A2 = dyn_cast<AllocaInst>(v2->stripPointerCasts())) {
      return A1 == A2; // different allocas cannot alias
    }
  }

  return true;
}

bool hasMultiplePredecessors(Instruction *I) {
  auto *blk = I->getParent();
  return &blk->front() == I && !blk->getUniquePredecessor();
}

bool mayModify(Instruction *I, AllocaInst *var) {
  if (!I->mayWriteToMemory())
    return false;

  if (auto *S = dyn_cast<StoreInst>(I)) {
    if (!mayAlias(S->getPointerOperand(), var)) {
      return false;
    }
  }

  // return true on all other cases to be safe
  return true;
}

bool PostNeckConstConversion::replace(AllocaInst *var, Constant *C, Instruction* I) {
  // replace loads of var for C
  llvm::errs() << "  -> Trying replace at " << *I << "\n";
  if (auto *L = dyn_cast<LoadInst>(I)) {
    if (mustAlias(L->getPointerOperand(), var)) {
      llvm::errs() << "Replacing "
                   << "  " << *L << " with " << "  " << *C << "\n";
      L->replaceAllUsesWith(C);
      // DONT erase it, we use it for getting the successors
      return true;
    }
  }
  return false;
}

template <typename Queue, typename Visited>
void queueSuccessors(Queue& queue, Visited& visited, Instruction *curI) {
  // queue.put(successors(l))
//  auto *nextI = curI->getNextNonDebugInstruction();
	auto *nextI = curI->getNextNode();
  if (!isa<DbgInfoIntrinsic>(nextI) && nextI) {
    if (visited.insert(nextI).second) {
    	outs() << "Inserted Next: " << *nextI << "\n";
      queue.insert(nextI);
    }
  } else {
    auto *blk = curI->getParent();

    for (auto *succ : successors(blk)) {
      auto firstI = &succ->front();
      if (visited.insert(firstI).second) {
        queue.insert(firstI);
      }
    }
  }
  llvm::errs() << "Queue size: " << queue.size() << "\n";
}

bool PostNeckConstConversion::propagate(Instruction& Loc, AllocaInst *var, Constant *C, AAResults &AA) {
   llvm::errs() << "Propagate " << *C << " to " << *var << "\nstarting at " << Loc << "\n";
  bool changed = false;
  // NOTE: not very efficient implementation
  std::set<Instruction *> visited;
  std::set<Instruction *> queue;

  queueSuccessors(queue, visited, &Loc);

  while (!queue.empty()) {
    // l = queue.pop()
    auto it = queue.begin();
    auto *curI = *it;
    queue.erase(it);

    /// we cannot continue here as the values from different predecessors
    // may be different
    if (hasMultiplePredecessors(curI))
      continue;

    // replace(V, C, l)
    changed |= replace(var, C, curI);
    if (!mayModify(curI, var)) {
    	outs() << "***Found MayModify\n";
      queueSuccessors(queue, visited, curI);
    }
  }
  return changed;
}

static inline AllocaInst *getVar(Value *val) {
    // TODO: use pointer analysis here
    // TODO: handle globals too
    if (auto *A = dyn_cast<AllocaInst>(val->stripPointerCasts()))
      return A;
    return nullptr;
}

bool PostNeckConstConversion::runOnFunction(Function &F)  {
	AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
//	outs() << "***Tracker: " << Tracker->getAliasSets().size() << "\n";
//	AA.canInstructionRangeModRef(I1, I2, Loc, Mode)
	SetVector<Value *> Pointers;

  if (F.isDeclaration())
    return false;

  bool changed = false;
  for (auto& I : instructions(F)) {
    if (auto *S = dyn_cast<StoreInst>(&I)) {
      if (auto *C = dyn_cast<Constant>(S->getValueOperand())) {
        if (auto *var = getVar(S->getPointerOperand())) {
          changed |= propagate(I, var, C, getAnalysis<AAResultsWrapperPass>().getAAResults());
        }
      }
    }
    if (I.getType()->isPointerTy())
    	Pointers.insert(&I);
  }

	  for (auto p : Pointers){
		  Instruction* i = cast<Instruction>(p);
	  }

  	  auto &AAWP = getAnalysis<AAResultsWrapperPass>();
        AliasSetTracker Tracker(AAWP.getAAResults());
        errs() << "Alias sets for function '" << F.getName() << "':\n";
        for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
          Tracker.add(&*I);

//        Tracker.print(errs());
        outs() << "SIZE:: " << Tracker.getAliasSets().size() << "\n";
//        for (auto as : Tracker.getAliasSets())
//        	outs() << "AS Size= " << as.size() << "\n";

  return changed;
}
