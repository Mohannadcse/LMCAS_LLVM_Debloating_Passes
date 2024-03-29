#include "Profiler.h"

#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace std;

static llvm::cl::opt<std::string> OutputFile("profile-outfile",
											 llvm::cl::desc("Dump all counters to output filename"),
											 llvm::cl::init(""));

static llvm::cl::opt<bool> ShowCallGraphInfo("profile-callgraph",
											 llvm::cl::desc("Show call graph information"), llvm::cl::init(false));

static llvm::cl::opt<bool> PrintDetails("profile-details",
										llvm::cl::desc("Show more detailed statistics"), llvm::cl::init(false));

static llvm::cl::opt<bool> DisplayDeclarations("profile-list-declarations",
											   llvm::cl::desc("List all the function declarations"),
											   llvm::cl::init(false), llvm::cl::Hidden);

static llvm::cl::opt<bool> ProfileLoops("profile-loops",
										llvm::cl::desc("Show some stats about loops"), llvm::cl::init(false));

static llvm::cl::opt<bool> ProfileSafePointers("profile-safe-pointers",
											   llvm::cl::desc(
												   "Show whether a pointer access is statically safe or not"),
											   llvm::cl::init(false));

static llvm::cl::opt<bool> ProfileVerbose("profile-verbose",
										  llvm::cl::desc("Print some verbose information"),
										  llvm::cl::init(false));

//ByMohannad
static llvm::cl::opt<std::string> AppSize("size",
										  llvm::cl::desc("get the binary size of the app"),
										  llvm::cl::init(""));

std::ofstream DbgFd;

namespace previrt
{

	static Function *
	getCalledFunctionThroughAliasesAndCasts(CallBase &CS)
	{
		DbgFd << "\tINSIDE getCalledFunctionThroughAliasesAndCasts\n";
		// Value *CalledV = CS.getCalledValue();
		// CalledV = CalledV->stripPointerCasts();

		Value *CalledV = CS.getCalledFunction()->stripPointerCasts();

		if (Function *F = dyn_cast<Function>(CalledV))
		{
			return F;
		}

		if (GlobalAlias *GA = dyn_cast<GlobalAlias>(CalledV))
		{
			if (Function *F = dyn_cast<Function>(
					GA->getAliasee()->stripPointerCasts()))
			{
				return F;
			}
		}

		return nullptr;
	}

	vector<std::string> splitString(std::string &str, char delim)
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

	void ProfilerPass::formatCounters(std::vector<Counter> &counters,
									  unsigned &MaxNameLen, unsigned &MaxValLen, bool sort)
	{
		// Figure out how long the biggest Value and Name fields are.
		DbgFd << "\tINSIDE formatCounters\n";
		for (auto c : counters)
		{
			MaxValLen = std::max(MaxValLen, (unsigned)utostr(c.getValue()).size());
			MaxNameLen = std::max(MaxNameLen, (unsigned)c.getName().size());
		}

		if (sort)
		{
			// Sort the fields by name.
			std::stable_sort(counters.begin(), counters.end());
		}
	}

	void ProfilerPass::formatCountersCG(std::vector<CGcounter> &counters,
										unsigned &MaxNameLen, unsigned &MaxValLen)
	{
		// Figure out how long the biggest Value and Name fields are.
		DbgFd << "\tINSIDE formatCountersCG\n";
		for (auto c : counters)
		{
			MaxValLen = std::max(MaxValLen, (unsigned)utostr(c.BasicBlock).size());
			MaxNameLen = std::max(MaxNameLen, (unsigned)c.FunctionName.size());
		}
	}

	void ProfilerPass::incrInstCounter(std::string name, unsigned val)
	{
		DbgFd << "\tINSIDE isSafeMemAccess\n";
		auto it = instCounters.find(name);
		if (it != instCounters.end())
		{
			it->second += val;
		}
		else
		{
			instCounters.insert({name, Counter(name)});
		}
	}

	/* Trivial checker for memory safety */
	bool ProfilerPass::isSafeMemAccess(Value *V)
	{
		DbgFd << "INSIDE isSafeMemAccess\n";
		ObjectSizeOpts opt;
		ObjectSizeOffsetVisitor OSOV(*DL, TLI, *Ctx, opt);
		// res.first is size and res.second is offset
		SizeOffsetType res = OSOV.compute(V);
		if (OSOV.bothKnown(res))
		{
			// FIXME: need to add getPointerTypeSizeInBits to offset
			return (res.second.isNonNegative() && res.second.ult(res.first));
		}
		return false;
	}

	void ProfilerPass::processPtrOperand(Value *V)
	{
		DbgFd << "\tINSIDE processPtrOperand\n";
		if (ProfileSafePointers && isSafeMemAccess(V))
		{
			++SafeMemAccess;
		}
		else
		{
			++MemUnknown;
		}
	}

	void ProfilerPass::processMemoryIntrinsicsPtrOperand(Value *V, Value *N)
	{
		DbgFd << "\tINSIDE processMemoryIntrinsicsPtrOperand\n";
		if (ProfileSafePointers)
		{
			if (ConstantInt *CI = dyn_cast<ConstantInt>(N))
			{
				int64_t n = CI->getSExtValue();
				uint64_t size;
				ObjectSizeOpts opt;
				if (getObjectSize(V, size, *DL, TLI, opt))
				{
					if (n >= 0 && ((uint64_t)n < size))
					{
						++SafeMemAccess;
						return;
					}
				}
			}
		}
		++MemUnknown;
	}

	void ProfilerPass::visitFunction(Function &F)
	{
		DbgFd << "\tINSIDE visitFunction\n";
		if (F.isDeclaration())
		{
			return;
		}
		++TotalFuncs;

		if (F.getName().startswith("__occam_spec"))
		{
			++TotalSpecFuncs;
		}

		if (F.getName().startswith("__occam.bounce"))
		{
			++TotalBounceFuncs;
		}

		if (ProfileVerbose)
		{
			errs() << "Function " << F.getName() << "\n";
		}

		if (ProfileLoops)
		{
			LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
			ScalarEvolution &SE =
				getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();
			for (auto L : LI)
			{
				++TotalLoops;
				if (SE.getSmallConstantTripCount(L))
				{
					++TotalBoundedLoops;
				}
			}
		}
	}

	void ProfilerPass::visitBasicBlock(BasicBlock &BB)
	{
		DbgFd << "\tINSIDE visitBasicBlock\n";
		++TotalBlocks;
		if (!BB.getSinglePredecessor())
		{
			++TotalJoins;
		}
	}

	void ProfilerPass::visitCallSite(CallBase CS)
	{
		DbgFd << "\tINSIDE visitCallSite\n";
		++TotalInsts;
		// TODO: incrInstCounter(#OPCODE, 1);
		Function *callee = getCalledFunctionThroughAliasesAndCasts(CS);
		if (callee)
		{
			++TotalDirectCalls;
			if (callee->isDeclaration())
			{
				++TotalExternalCalls;
				ExtFuncs.insert(callee->getName());
			}
		}
		else if (CS.isIndirectCall())
		{
			++TotalIndirectCalls;
			if (ProfileVerbose)
			{
				llvm::errs() << "Indirect call found: " << CS
							 << "\n";
			}
		}
		else if (CS.isInlineAsm())
		{
			++TotalAsmCalls;
			if (ProfileVerbose)
			{
				llvm::errs() << "Asm call found: " << CS << "\n";
			}
		}
		else
		{
			++TotalUnkCalls;
			if (ProfileVerbose)
			{
				llvm::errs() << "Unknown call found: " << CS
							 << "\n";
			}
		}
		CallBase &css = CS;
		// new, malloc, calloc, realloc, and strdup.
		if (isAllocationFn(&css, TLI, true))
		{
			DbgFd << "\t\tINSIDE isAllocationFn Branch\n";
			++TotalAllocations;
		}
	}

	void ProfilerPass::visitBinaryOperator(BinaryOperator &BI)
	{
		DbgFd << "\tINSIDE visitBinaryOperator\n";
		++TotalInsts;
		// TODO: incrInstCounter(#OPCODE, 1);
		if (BI.getOpcode() == BinaryOperator::SDiv || BI.getOpcode() == BinaryOperator::UDiv || BI.getOpcode() == BinaryOperator::SRem || BI.getOpcode() == BinaryOperator::URem || BI.getOpcode() == BinaryOperator::FDiv || BI.getOpcode() == BinaryOperator::FRem)
		{
			const Value *divisor = BI.getOperand(1);
			if (const ConstantInt *CI = dyn_cast<const ConstantInt>(divisor))
			{
				if (CI->isZero())
					++UnsafeIntDiv;
				else
					++SafeIntDiv;
			}
			else if (const ConstantFP *CFP = dyn_cast<const ConstantFP>(
						 divisor))
			{
				if (CFP->isZero())
				{
					++UnsafeFPDiv;
				}
				else
				{
					++SafeFPDiv;
				}
			}
			else
			{
				// cannot figure out statically
				if (BI.getOpcode() == BinaryOperator::SDiv || BI.getOpcode() == BinaryOperator::UDiv || BI.getOpcode() == BinaryOperator::SRem || BI.getOpcode() == BinaryOperator::URem)
				{
					++DivIntUnknown;
				}
				else
				{
					++DivFPUnknown;
				}
			}
		}
		else if (BI.getOpcode() == BinaryOperator::Shl)
		{
			// Check for oversized shift amounts
			if (const ConstantInt *CI = dyn_cast<const ConstantInt>(
					BI.getOperand(1)))
			{
				APInt shift = CI->getValue();
				if (CI->getType()->isIntegerTy())
				{
					APInt bitwidth(shift.getBitWidth(),
								   CI->getType()->getIntegerBitWidth(), true);
					if (shift.slt(bitwidth))
					{
						++SafeLeftShift;
					}
					else
					{
						++UnsafeLeftShift;
					}
				}
				else
				{
					++UnknownLeftShift;
				}
			}
			else
			{
				++UnknownLeftShift;
			}
		}
	}

	void ProfilerPass::visitMemTransferInst(MemTransferInst &I)
	{
		DbgFd << "\tINSIDE visitMemTransferInst\n";
		++TotalInsts;
		++TotalMemInst;
		++TotalMemInst;
		if (isa<MemCpyInst>(&I))
			++MemCpy;
		else if (isa<MemMoveInst>(&I))
			++MemMove;
		processMemoryIntrinsicsPtrOperand(I.getSource(), I.getLength());
		processMemoryIntrinsicsPtrOperand(I.getDest(), I.getLength());
	}

	void ProfilerPass::visitMemSetInst(MemSetInst &I)
	{
		DbgFd << "\tINSIDE visitMemSetInst\n";
		++TotalInsts;
		++TotalMemInst;
		++MemSet;
		processMemoryIntrinsicsPtrOperand(I.getDest(), I.getLength());
	}

	void ProfilerPass::visitAllocaInst(AllocaInst &I)
	{
		DbgFd << "\tINSIDE visitAllocaInst\n";
		++TotalInsts;
		incrInstCounter("Alloca", 1);
	}

	void ProfilerPass::visitLoadInst(LoadInst &I)
	{
		DbgFd << "\tINSIDE visitLoadInst\n";
		++TotalInsts;
		++TotalMemInst;
		incrInstCounter("Load", 1);
		processPtrOperand(I.getPointerOperand());
	}

	void ProfilerPass::visitStoreInst(StoreInst &I)
	{
		DbgFd << "\tINSIDE visitStoreInst\n";
		++TotalInsts;
		++TotalMemInst;
		incrInstCounter("Store", 1);
		processPtrOperand(I.getPointerOperand());
	}

	void ProfilerPass::visitGetElementPtrInst(GetElementPtrInst &I)
	{
		DbgFd << "\tINSIDE visitGetElementPtrInst\n";
		++TotalInsts;
		incrInstCounter("GetElementPtr", 1);
		if (I.isInBounds())
		{
			++InBoundGEP;
		}
	}

	void ProfilerPass::visitInstruction(Instruction &I)
	{
		DbgFd << "\tINSIDE visitInstruction\n";
		// TODO: incrInstCounter(#OPCODE, 1);
		++TotalInsts;
	}

	ProfilerPass::ProfilerPass() : ModulePass(ID), DL(nullptr), TLIWrapper(nullptr), TLI(nullptr), TotalFuncs("TotalFuncs",
																											  "Number of functions"),
								   TotalSpecFuncs("TotalSpecFuncs",
												  "Number of specialized functions")
								   //, TotalBounceFuncs("TotalBounceFuncs", "Number of bounced functions added by devirt")
								   ,
								   TotalBlocks("TotalBlocks", "Number of basic blocks"), TotalJoins(
																							 "TotalJoins",
																							 "Number of basic blocks with more than one predecessor"),
								   TotalInsts(
									   "TotalInsts", "Number of instructions"),
								   TotalDirectCalls(
									   "TotalDirectCalls", "Number of direct calls"),
								   TotalIndirectCalls(
									   "TotalIndirectCalls", "Number of indirect calls"),
								   TotalAsmCalls(
									   "TotalAsmCalls", "Number of assembly calls"),
								   TotalExternalCalls(
									   "TotalExternalCalls", "Number of external calls"),
								   TotalUnkCalls(
									   "TotalUnkCalls", "Number of unknown calls"),
								   TotalLoops(
									   "TotalLoops", "Number of loops"),
								   TotalBoundedLoops(
									   "TotalBoundedLoops", "Number of bounded loops")
								   ////////
								   ,
								   SafeIntDiv("SafeIntDiv", "Number of safe integer div/rem"), SafeFPDiv(
																								   "SafeFPDiv", "Number of safe FP div/rem"),
								   UnsafeIntDiv(
									   "UnsafeIntDiv", "Number of definite unsafe integer div/rem"),
								   UnsafeFPDiv(
									   "UnsafeFPDiv", "Number of definite unsafe FP div/rem"),
								   DivIntUnknown(
									   "DivIntUnknown", "Number of unknown integer div/rem"),
								   DivFPUnknown(
									   "DivFPUnknown", "Number of unknown FP div/rem")
								   /////////
								   ,
								   TotalMemInst("TotalMemInst", "Number of memory instructions"), MemUnknown(
																									  "MemUnknown", "Statically unknown memory accesses"),
								   SafeMemAccess(
									   "SafeMemAccess", "Statically safe memory accesses"),
								   TotalAllocations(
									   "TotalAllocations", "Malloc-like allocations"),
								   InBoundGEP(
									   "InBoundGEP", "Inbound GetElementPtr"),
								   MemCpy("MemCpy"), MemMove(
														 "MemMove"),
								   MemSet("MemSet")
								   /////////
								   ,
								   SafeLeftShift("SafeLeftShift", "Number of safe left shifts"), UnsafeLeftShift(
																									 "UnsafeLeftShift", "Number of definite unsafe left shifts"),
								   UnknownLeftShift(
									   "UnknownLeftShift", "Number of unknown left shifts")
	{
		DbgFd.open("debug.txt");
		DbgFd << "INSIDE Constructor\n";
	}

	// Collect statistics
	bool ProfilerPass::runOnFunction(Function &F)
	{
		DbgFd << "INSIDE runOnFunction\n";
		DbgFd << "+++++++++++++++++++++++++++++++++";
		DbgFd << "FunctionName: " << F.getName().str() << "\n";
		TLI = &(TLIWrapper->getTLI(F));
		visit(F);
		return false;
	}

	bool ProfilerPass::runOnModule(Module &M)
	{
		DbgFd << "INSIDE runOnModule\n";
		DL = &M.getDataLayout();
		// TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
		TLIWrapper = &getAnalysis<TargetLibraryInfoWrapperPass>();
		Ctx = &M.getContext();

		if (ShowCallGraphInfo)
		{
			CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
			typedef std::pair<Function *, std::pair<unsigned, unsigned>> func_ty;
			std::vector<func_ty> funcs;
			errs() << "[Call graph information]\n";
			errs() << "Total number of functions="
				   << std::distance(M.begin(), M.end()) << "\n";
			for (auto it = scc_begin(&CG); !it.isAtEnd(); ++it)
			{
				auto &scc = *it;
				for (CallGraphNode *cgn : scc)
				{
					if (cgn->getFunction() && !cgn->getFunction()->isDeclaration())
					{
						funcs.push_back(
							{cgn->getFunction(), {cgn->getNumReferences(), std::distance(cgn->begin(), cgn->end())}});
					}
				}
			}

			bool has_rec_func = false;
			for (auto it = scc_begin(&CG); !it.isAtEnd(); ++it)
			{
				auto &scc = *it;
				if (std::distance(scc.begin(), scc.end()) > 1)
				{
					has_rec_func = true;
					errs() << "Found recursive SCC={";
					for (CallGraphNode *cgn : scc)
					{
						if (cgn->getFunction())
							errs() << cgn->getFunction()->getName() << ";";
					}
				}
			}

			if (!has_rec_func)
			{
				errs() << "No recursive functions found\n";
			}

			std::sort(funcs.begin(), funcs.end(),
					  [](func_ty p1, func_ty p2)
					  {
						  return (p1.second.first + p1.second.second) > (p2.second.first + p2.second.second);
					  });

			for (auto &p : funcs)
			{
				Function *F = p.first;
				unsigned numInsts = std::distance(inst_begin(F), inst_end(F));
				CGcounter func(std::string(F->getName()), F->getBasicBlockList().size(), numInsts, p.second.first, p.second.second);
				functions.push_back(func);
				errs() << F->getName() << ":"
					   << " num of BasicBlocks="
					   << F->getBasicBlockList().size() << " num of instructions="
					   << numInsts << " num of callers=" << p.second.first
					   << " num of callees=" << p.second.second << "\n";
			}
		}

		for (auto &F : M)
		{
			runOnFunction(F);
		}

		printCountersCSV(M);

		if (OutputFile != "")
		{
			std::error_code ec;
			llvm::ToolOutputFile out(OutputFile.c_str(), ec, sys::fs::F_Text);
			if (ec)
			{
				errs() << "ERROR: Cannot open file: " << ec.message() << "\n";
			}
			else
			{
				printCounters(out.os());
				out.keep();
			}
		}
		else
		{
			printCounters(errs());
		}

		// if (DisplayDeclarations) {
		//   errs() << "[Non-analyzed (external) functions]\n";
		//   for(auto &p: ExtFuncs)
		//     errs() << p.getKey() << "\n";
		// }

		return false;
	}

	void ProfilerPass::getAnalysisUsage(AnalysisUsage &AU) const
	{
		DbgFd << "INSIDE getAnalysisUsage\n";
		AU.setPreservesAll();
		AU.addRequired<llvm::TargetLibraryInfoWrapperPass>();

		if (ShowCallGraphInfo)
		{
			AU.addRequired<llvm::CallGraphWrapperPass>();
		}

		if (ProfileLoops)
		{
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<ScalarEvolutionWrapperPass>();
		}
	}

	void ProfilerPass::printCountersCSV(Module &M)
	{
		ofstream ofs;
		std::string filename = "report.csv";

		std::ifstream ofs_check(filename);
		ofs.open(filename, std::ofstream::out | std::ofstream::app);

		if (!ofs_check.good())
		{
			ofs << "AppName,Pass,TotalInstr,TotalFunc,TotalBBs,Size\n";
		}
		if (AppSize != "")
		{
			auto id = M.getModuleIdentifier();
			std::vector<std::string> vec;
			if (id.find("/") != std::string::npos)
			{
				vec = splitString(id, '/');
				ofs << vec[vec.size() - 1] << ",";
			}
			else
			{
				vec = splitString(id, '_');
				ofs << vec[0] << ",";
			}

			ofs << vec[vec.size() - 1].substr(0, vec[vec.size() - 1].find(".")) << ",";

			ofs << TotalInsts.Value << ",";
			ofs << TotalFuncs.Value << ",";
			ofs << TotalBlocks.Value << ",";
			ofs << AppSize << "\n";
		}
		else
		{
			outs() << "ERROR: the size is empty\n";
		}
		ofs.close();
	}

	void ProfilerPass::printCounters(raw_ostream &O)
	{
		DbgFd << "INSIDE printCounters\n";
		unsigned MaxNameLen = 0, MaxValLen = 0;
		unsigned MaxNameLenCG = 0, MaxValLenCG = 0;
		/*
	O << "[CG analysis]\n";
	for (auto f : functions){
		formatCountersCG(functions, MaxNameLenCG, MaxValLenCG);
		O << format(">> %*s BasicBlocks: %*u Instructions: %*u Callers: %*u Callees: %*u\n",
				MaxNameLenCG, f.FunctionName.c_str(), MaxValLenCG, f.BasicBlock, MaxValLenCG, f.Instructions,
				MaxValLenCG, f.Callers, MaxValLenCG, f.Callees);
	}

	O << "[CFG analysis]\n";

	std::vector<Counter> cfg_counters { TotalFuncs, TotalSpecFuncs,
			TotalBounceFuncs, TotalBlocks, TotalInsts, TotalDirectCalls,
			TotalExternalCalls, TotalAsmCalls, TotalIndirectCalls, TotalUnkCalls };

	if (ProfileLoops) {
		cfg_counters.push_back(TotalLoops);
		cfg_counters.push_back(TotalBoundedLoops);
	}

	formatCounters(cfg_counters, MaxNameLen, MaxValLen, false);
	for (auto c : cfg_counters) {
		O
				<< format("%*u %-*s\n", MaxValLen, c.getValue(), MaxNameLen,
						c.getDesc().c_str());
	}*/

		if (PrintDetails)
		{
			// instruction counters
			MaxNameLen = MaxValLen = 0;
			std::vector<Counter> inst_counters;
			inst_counters.reserve(instCounters.size());
			for (auto &p : instCounters)
			{
				inst_counters.push_back(p.second);
			}
			formatCounters(inst_counters, MaxNameLen, MaxValLen);
			if (inst_counters.empty())
			{
				O << "No information about each kind of instruction\n";
			}
			else
			{
				O << "Number of each kind of instructions:\n";
				for (auto c : inst_counters)
				{
					O
						<< format("%*u %-*s\n", MaxValLen, c.getValue(),
								  MaxNameLen, c.getDesc().c_str());
				}
			}
		}

		// Memory counters
		/*
	MaxNameLen = MaxValLen = 0;
	std::vector<Counter> mem_counters;
	if (PrintDetails) {
		mem_counters = { TotalMemInst, instCounters["Store"],
				instCounters["Load"], MemCpy, MemMove, MemSet,
				instCounters["GetElementPtr"], InBoundGEP,
				instCounters["Alloca"], TotalAllocations, SafeMemAccess,
				MemUnknown };
	} else {
		mem_counters = { TotalMemInst, SafeMemAccess, MemUnknown };
	}

	formatCounters(mem_counters, MaxNameLen, MaxValLen, false);
	O << "[Memory analysis]\n";
	for (auto c : mem_counters) {
		O
				<< format("%*u %-*s\n", MaxValLen, c.getValue(), MaxNameLen,
						c.getDesc().c_str());
	}
	*/
	}

	char ProfilerPass::ID = 0;

} // end namespace previrt

static llvm::RegisterPass<previrt::ProfilerPass> X("Pprofiler",
												   "Count number of functions, instructions, memory accesses, etc.", false,
												   false);
