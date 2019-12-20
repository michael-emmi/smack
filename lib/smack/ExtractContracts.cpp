//
// This file is distributed under the MIT License. See LICENSE for details.
//

#define DEBUG_TYPE "contracts"

#include "smack/ExtractContracts.h"
#include "smack/Debug.h"
#include "smack/Naming.h"
#include "smack/SmackOptions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

#include <map>
#include <set>
#include <stack>
#include <vector>

namespace smack {

using namespace llvm;

namespace {

bool isContractFunction(Function *F) {
  auto name = F->getName();
  return name == Naming::CONTRACT_REQUIRES ||
         name == Naming::CONTRACT_ENSURES || name == Naming::CONTRACT_INVARIANT;
}

typedef std::vector<BasicBlock *> BlockList;
typedef std::map<const Loop *, BlockList> LoopMap;

// Return the list of blocks which dominate the given blocks in the given
// function.
// NOTE this procedure assumes blocks are ordered in dominated order, both
// in the given selection of blocks, as well as in the given function.
BlockList blockPrefix(BlockList BBs, Function &F) {
  BlockList prefix;
  if (!BBs.empty()) {
    for (auto &BB : F) {
      prefix.push_back(&BB);
      if (&BB == BBs.back())
        break;
    }
  }

  SDEBUG(dbgs() << "blockPrefix(";
    std::for_each(BBs.begin(), BBs.end(), [](BasicBlock *B){ dbgs() << B->getName() << ", "; });
    dbgs() << ") = {";
    std::for_each(prefix.begin(), prefix.end(), [](BasicBlock *B){ dbgs() << B->getName() << ", "; });
    dbgs() << "}\n");

  return prefix;
}

// Return the list of blocks which dominate the given blocks in the given
// loop, not including the loop head.
// NOTE this procedure assumes blocks are ordered in dominated order, both
// in the given selection of blocks, as well as in the given loop.
BlockList blockPrefix(BlockList BBs, const Loop &L) {
  BlockList prefix;
  if (!BBs.empty()) {
    for (auto BB : L.getBlocks()) {
      if (BB == L.getHeader())
        continue;
      prefix.push_back(BB);
      if (BB == BBs.back())
        break;
    }
  }

  SDEBUG(dbgs() << "blockPrefix(";
    std::for_each(BBs.begin(), BBs.end(), [](BasicBlock *B){ dbgs() << B->getName() << ", "; });
    dbgs() << ") = {";
    std::for_each(prefix.begin(), prefix.end(), [](BasicBlock *B){ dbgs() << B->getName() << ", "; });
    dbgs() << "}\n");

  return prefix;
}

// Split the basic blocks of the given function such that each invocation to
// contract functions is the last non-terminator instruction in its block.
std::tuple<BlockList, LoopMap> splitContractBlocks(Function &F, LoopInfo &LI) {

  BlockList contractBlocks;
  LoopMap invariantBlocks;

  std::deque<BasicBlock *> blocks;
  for (auto &BB : F)
    blocks.push_back(&BB);

  while (!blocks.empty()) {
    auto BB = blocks.front();
    blocks.pop_front();

    for (auto &I : *BB) {
      if (auto CI = dyn_cast<CallInst>(&I)) {
        if (auto *CF = CI->getCalledFunction()) {
          if (isContractFunction(CF)) {
            SDEBUG(errs() << "splitting block at contract invocation: " << *CI
                          << "\n");
            BasicBlock *B = CI->getParent();
            auto NewBB = B->splitBasicBlock(++CI->getIterator());
            LI.getLoopFor(B)->addBasicBlockToLoop(NewBB, LI);
            blocks.push_front(NewBB);

            if (auto L = LI[B])
              invariantBlocks[L].push_back(B);
            else
              contractBlocks.push_back(B);

            break;
          }
        }
      }
    }
  }

  contractBlocks = blockPrefix(contractBlocks, F);

  for (auto B : contractBlocks) {
    SDEBUG(dbgs() << "contract block: " << B->getName() << "\n");
  }

  for (auto const &entry : invariantBlocks) {
    auto L = entry.first;
    auto BBs = entry.second;
    auto prefix = blockPrefix(BBs, *L);
    invariantBlocks[L] = prefix;

    SDEBUG(dbgs() << "invariant blocks entry:\n  " << *L << "\n");
  }

  return std::make_tuple(contractBlocks, invariantBlocks);
}

// Return the list of contract invocations in the given function.
std::vector<CallInst *> getContractInvocations(Function &F) {
  std::vector<CallInst *> CIs;
  for (auto &BB : F)
    for (auto &I : BB)
      if (auto CI = dyn_cast<CallInst>(&I))
        if (auto F = CI->getCalledFunction())
          if (isContractFunction(F))
            CIs.push_back(CI);
  return CIs;
}

// Replace the given invocation with a return of its argument; also remove
// all other contract invocations.
void setReturnToArgumentValue(Function *F, CallInst *II) {

  for (auto &BB : *F) {

    // Replace existing returns with return true.
    if (auto RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
      IRBuilder<> Builder(RI);
      Builder.CreateRet(
          ConstantInt::getTrue(F->getFunctionType()->getReturnType()));
      RI->eraseFromParent();
      continue;
    }

    // Erase contract invocations, and replace the given invocation
    // with a return of its argument.
    for (auto &I : BB) {
      if (auto CI = dyn_cast<CallInst>(&I)) {
        if (auto F = CI->getCalledFunction()) {
          if (isContractFunction(F)) {
            if (CI == II) {
              IRBuilder<> Builder(CI);
              BB.getTerminator()->eraseFromParent();
              Builder.CreateRet(CI->getArgOperand(0));
            }
            CI->eraseFromParent();
            break;
          }
        }
      }
    }
  }
}

// Return a copy of the given function in which the given invocation is
// replaced by a return of its argument value, and all contract
// invocations are removed.
Function *getContractExpr(Function *F, CallInst *I) {
  ValueToValueMapTy VMap;
  SmallVector<ReturnInst *, 8> Returns;

  FunctionType *FT = FunctionType::get(I->getFunctionType()->getParamType(0),
                                       F->getFunctionType()->params(),
                                       F->getFunctionType()->isVarArg());

  Function *NewF = Function::Create(FT, F->getLinkage(), Naming::CONTRACT_EXPR,
                                    F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments over...
  // See implementation of llvm::CloneFunction
  Function::arg_iterator DestA = NewF->arg_begin();
  for (auto &A : F->args()) {
    DestA->setName(A.getName());
    VMap[&A] = &*DestA++;
  }
  CloneFunctionInto(NewF, F, VMap, false, Returns);
  setReturnToArgumentValue(NewF, dyn_cast<CallInst>(VMap[I]));
  return NewF;
}

// Return copies of the given function in for each contract invocation, in
// which each returns the argument value of the corresponding contract
// invocation. The first function of each returned pair is the invoked
// contract function; the second function is the new function created for the
// given invocation.
std::vector<std::tuple<Function *, Function *>> getContractExprs(Function &F) {
  std::vector<std::tuple<Function *, Function *>> Fs;
  for (auto CI : getContractInvocations(F)) {
    Function *newF = getContractExpr(&F, CI);
    Fs.push_back(std::make_tuple(CI->getCalledFunction(), newF));
  }
  return Fs;
}
} // namespace

/// Test whether a block is valid for extraction.
static bool isBlockValidForExtraction(const BasicBlock &BB,
                                      const SetVector<BasicBlock *> &Result,
                                      bool AllowVarArgs, bool AllowAlloca) {
  // taking the address of a basic block moved to another function is illegal
  if (BB.hasAddressTaken())
    return false;

  // don't hoist code that uses another basicblock address, as it's likely to
  // lead to unexpected behavior, like cross-function jumps
  SmallPtrSet<User const *, 16> Visited;
  SmallVector<User const *, 16> ToVisit;

  for (Instruction const &Inst : BB)
    ToVisit.push_back(&Inst);

  while (!ToVisit.empty()) {
    User const *Curr = ToVisit.pop_back_val();
    if (!Visited.insert(Curr).second)
      continue;
    if (isa<BlockAddress const>(Curr))
      return false; // even a reference to self is likely to be not compatible

    if (isa<Instruction>(Curr) && cast<Instruction>(Curr)->getParent() != &BB)
      continue;

    for (auto const &U : Curr->operands()) {
      if (auto *UU = dyn_cast<User>(U))
        ToVisit.push_back(UU);
    }
  }

  // If explicitly requested, allow vastart and alloca. For invoke instructions
  // verify that extraction is valid.
  for (BasicBlock::const_iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
    if (isa<AllocaInst>(I)) {
       if (!AllowAlloca)
         return false;
       continue;
    }

    if (const auto *II = dyn_cast<InvokeInst>(I)) {
      // Unwind destination (either a landingpad, catchswitch, or cleanuppad)
      // must be a part of the subgraph which is being extracted.
      if (auto *UBB = II->getUnwindDest())
        if (!Result.count(UBB))
          return false;
      continue;
    }

    // All catch handlers of a catchswitch instruction as well as the unwind
    // destination must be in the subgraph.
    if (const auto *CSI = dyn_cast<CatchSwitchInst>(I)) {
      if (auto *UBB = CSI->getUnwindDest())
        if (!Result.count(UBB))
          return false;
      for (auto *HBB : CSI->handlers())
        if (!Result.count(const_cast<BasicBlock*>(HBB)))
          return false;
      continue;
    }

    // Make sure that entire catch handler is within subgraph. It is sufficient
    // to check that catch return's block is in the list.
    if (const auto *CPI = dyn_cast<CatchPadInst>(I)) {
      for (const auto *U : CPI->users())
        if (const auto *CRI = dyn_cast<CatchReturnInst>(U))
          if (!Result.count(const_cast<BasicBlock*>(CRI->getParent())))
            return false;
      continue;
    }

    // And do similar checks for cleanup handler - the entire handler must be
    // in subgraph which is going to be extracted. For cleanup return should
    // additionally check that the unwind destination is also in the subgraph.
    if (const auto *CPI = dyn_cast<CleanupPadInst>(I)) {
      for (const auto *U : CPI->users())
        if (const auto *CRI = dyn_cast<CleanupReturnInst>(U))
          if (!Result.count(const_cast<BasicBlock*>(CRI->getParent())))
            return false;
      continue;
    }
    if (const auto *CRI = dyn_cast<CleanupReturnInst>(I)) {
      if (auto *UBB = CRI->getUnwindDest())
        if (!Result.count(UBB))
          return false;
      continue;
    }

    if (const CallInst *CI = dyn_cast<CallInst>(I)) {
      if (const Function *F = CI->getCalledFunction()) {
        auto IID = F->getIntrinsicID();
        if (IID == Intrinsic::vastart) {
          if (AllowVarArgs)
            continue;
          else
            return false;
        }

        // Currently, we miscompile outlined copies of eh_typid_for. There are
        // proposals for fixing this in llvm.org/PR39545.
        if (IID == Intrinsic::eh_typeid_for)
          return false;
      }
    }
  }

  return true;
}

 /// Build a set of blocks to extract if the input blocks are viable.
 static SetVector<BasicBlock *>
 buildExtractionBlockSet(ArrayRef<BasicBlock *> BBs, DominatorTree *DT,
                         bool AllowVarArgs, bool AllowAlloca) {
   assert(!BBs.empty() && "The set of blocks to extract must be non-empty");
   SetVector<BasicBlock *> Result;

   // Loop over the blocks, adding them to our set-vector, and aborting with an
   // empty set if we encounter invalid blocks.
   for (BasicBlock *BB : BBs) {
     // If this block is dead, don't process it.
     if (DT && !DT->isReachableFromEntry(BB))
       continue;

     if (!Result.insert(BB))
       llvm_unreachable("Repeated basic blocks in extraction input");
   }

   SDEBUG(dbgs() << "Region front block: " << Result.front()->getName()
                     << '\n');

   for (auto *BB : Result) {
     if (!isBlockValidForExtraction(*BB, Result, AllowVarArgs, AllowAlloca))
       return {};

     // Make sure that the first block is not a landing pad.
     if (BB == Result.front()) {
       if (BB->isEHPad()) {
         SDEBUG(dbgs() << "The first block cannot be an unwind block\n");
         return {};
       }
       continue;
     }

     // All blocks other than the first must not have predecessors outside of
     // the subgraph which is being extracted.
     for (auto *PBB : predecessors(BB))
       if (!Result.count(PBB)) {
         SDEBUG(dbgs() << "No blocks in this region may have entries from "
                              "outside the region except for the first block!\n"
                           << "Problematic source BB: " << BB->getName() << "\n"
                           << "Problematic destination BB: " << PBB->getName()
                           << "\n");
         return {};
       }
   }

   return Result;
 }

bool ExtractContracts::runOnModule(Module &M) {
  bool modified = false;

  std::vector<Function *> Fs;
  std::vector<Function *> newFs;

  for (Function &F : M)
    if (!F.isDeclaration())
      Fs.push_back(&F);

  for (auto F : Fs) {
    BlockList contractBlocks;
    LoopMap invariantBlocks;
    auto &LI = getAnalysis<LoopInfoWrapperPass>(*F).getLoopInfo();
    std::tie(contractBlocks, invariantBlocks) = splitContractBlocks(*F, LI);

    if (!contractBlocks.empty() || !invariantBlocks.empty()) {
      SDEBUG(errs() << "function " << F->getName() << " after splitting: " << *F
                    << "\n");
      modified = true;
    }

    if (!contractBlocks.empty()) {
      auto *newF = CodeExtractor(contractBlocks).extractCodeRegion();

      std::vector<CallInst *> Is;
      for (auto V : newF->users())
        if (auto I = dyn_cast<CallInst>(V))
          Is.push_back(I);

      for (auto I : Is) {
        IRBuilder<> Builder(I);

        // insert one contract invocation per invocation in the original
        // function
        for (auto Fs : getContractExprs(*newF)) {
          std::vector<Value *> Args;
          for (auto &A : I->arg_operands())
            Args.push_back(A);
          auto *E = Builder.CreateCall(std::get<1>(Fs), Args);
          Builder.CreateCall(std::get<0>(Fs), {E});
          newFs.push_back(std::get<1>(Fs));
        }
        I->eraseFromParent();
      }
      newF->eraseFromParent();
      SDEBUG(errs() << "function " << F->getName()
                    << " after contract extraction: " << *F << "\n");
    }

    for (auto const &entry : invariantBlocks) {
      auto BBs = entry.second;
      CodeExtractor extractor(BBs);
      buildExtractionBlockSet(BBs, nullptr, false, false);
      assert(extractor.isEligible() && "Code is not eligible for extraction.");

      auto *newF = extractor.extractCodeRegion();
      assert(newF && "Extracted function is empty.");

      std::vector<CallInst *> Is;
      for (auto V : newF->users())
        if (auto I = dyn_cast<CallInst>(V))
          Is.push_back(I);

      for (auto I : Is) {
        IRBuilder<> Builder(I);

        // insert one invariant invocation per invocation in the original loop
        for (auto Fs : getContractExprs(*newF)) {
          std::vector<Value *> Args;
          for (auto &A : I->arg_operands())
            Args.push_back(A);
          auto *E = Builder.CreateCall(std::get<1>(Fs), Args);
          Builder.CreateCall(std::get<0>(Fs), {E});
          newFs.push_back(std::get<1>(Fs));
        }
        I->eraseFromParent();
      }
      newF->eraseFromParent();
      SDEBUG(errs() << "function " << F->getName()
                    << " after invariant extraction: " << *F << "\n");
    }
  }

  for (auto F : newFs) {
    SDEBUG(errs() << "added function:" << *F);
  }

  return modified;
}

void ExtractContracts::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
}

// Pass ID variable
char ExtractContracts::ID = 0;

// Register the pass
static RegisterPass<ExtractContracts> X("extract-contracts",
                                        "Extract Contracts");
} // namespace smack
