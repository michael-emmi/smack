//===- Basic.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the basic data structure analysis pass. It simply assumes
// that all pointers can points to all possible locations.
//
//===----------------------------------------------------------------------===//

#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"

#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

using namespace llvm;

static RegisterPass<BasicDataStructures>
X("dsa-basic", "Basic Data Structure Analysis(No Analysis)");

char BasicDataStructures::ID = 0;

bool BasicDataStructures::runOnModule(Module &M) {
  init(&M.getDataLayout());

  //
  // Create a void pointer type.  This is simply a pointer to an 8 bit value.
  //

  DSNode * GVNodeInternal = new DSNode(GlobalsGraph);
  DSNode * GVNodeExternal = new DSNode(GlobalsGraph);
  for (Module::global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I) {
    if (I->isDeclaration() || (!(I->hasInternalLinkage()))) {
      GlobalsGraph->getNodeForValue(&*I).mergeWith(GVNodeExternal);
    } else {
      GlobalsGraph->getNodeForValue(&*I).mergeWith(GVNodeInternal);
    }
  }

  GVNodeInternal->foldNodeCompletely();
  GVNodeInternal->maskNodeTypes(DSNode::IncompleteNode);

  GVNodeExternal->foldNodeCompletely();
  GVNodeExternal->setExternalMarker();

  // Next step, iterate through the nodes in the globals graph, unioning
  // together the globals into equivalence classes.
  formGlobalECs();

  for (Function &F : M) {
    if (!F.isDeclaration()) {
      DSGraph* G = new DSGraph(GlobalECs, getDataLayout(), *TypeSS, GlobalsGraph);
      DSNode * Node = new DSNode(G);
          
      if (!F.hasInternalLinkage())
        Node->setExternalMarker();

      // Create scalar nodes for all pointer arguments...
      for (auto &Arg : F.args()) {
        if (isa<PointerType>(Arg.getType())) {
          G->getNodeForValue(&Arg).mergeWith(Node);
        }
      }

      for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
        G->getNodeForValue(&*I).mergeWith(Node);
      }

      Node->foldNodeCompletely();
      Node->maskNodeTypes(DSNode::IncompleteNode);

      setDSGraph(F, G);
    }
  }
 
  return false;
}
