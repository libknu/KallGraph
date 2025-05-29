#include "../include/Util.hpp"
#include <algorithm>

llvm::cl::opt<std::string> SpecifyInput(
    "SpecifyInput",
    llvm::cl::desc("specify input such as indirect calls or global variables"),
    llvm::cl::init(""));

int baseNum = DEFCONFIG;

unordered_set<NodeID> BlockedNodes;

unordered_map<NodeID, unordered_map<SVFStmt *, unordered_set<NodeID>>> phiIn;
unordered_map<NodeID, unordered_map<SVFStmt *, unordered_set<NodeID>>> phiOut;

unordered_map<NodeID, unordered_map<SVFStmt *, unordered_set<NodeID>>> selectIn;
unordered_map<NodeID, unordered_map<SVFStmt *, unordered_set<NodeID>>>
    selectOut;

unordered_set<NodeID> traceNodes;
unordered_set<const CallInst *> traceiCalls;

unordered_set<const Function *> SELinuxfuncs;
unordered_set<NodeID> SELinuxNodes;
unordered_set<const CallInst *> SELinuxicalls;

unordered_map<string, unordered_set<Function *>> type2funcs;

unordered_map<string, unordered_map<u32_t, unordered_set<PAGEdge *>>>
    typebasedShortcuts;
unordered_map<string,
              unordered_map<u32_t, unordered_set<unordered_set<PAGEdge *> *>>>
    additionalShortcuts;
unordered_map<string, unordered_set<PAGEdge *>> castSites;
unordered_map<PAGEdge *, unordered_map<u32_t, unordered_set<string>>>
    reverseShortcuts;
unordered_map<PAGNode *, PAGEdge *> gepIn;

unordered_map<const PAGEdge *, long> gep2byteoffset;
unordered_set<const PAGEdge *> variantGep;

unordered_map<StructType *, string> deAnonymousStructs;

unordered_map<const Function *, unordered_set<CallInst *>> GlobalDepFuncs;
unordered_map<CallInst *, unordered_set<CallInst *>> GlobalDepiCalls;
unordered_set<const Function *> newTargets;
unordered_set<CallInst *> newiCalls;
unordered_set<CallInst *> fixediCalls;
unordered_map<const CallInst *, unordered_set<const Function *>> callgraph;
size_t callgraph_size = 0;

unordered_map<NodeID, unordered_set<NodeID>> Real2Formal;
unordered_map<NodeID, unordered_set<NodeID>> Formal2Real;
unordered_map<NodeID, unordered_set<NodeID>> Ret2Call;
unordered_map<NodeID, unordered_set<NodeID>> Call2Ret;

unordered_map<NodeID, const Function *> Param2Funcs;
unordered_map<NodeID, CallInst *> Arg2iCalls;

void sortMap(std::vector<pair<PAGNode *, u64_t>> &sorted,
             unordered_map<PAGNode *, u64_t> &before, int k) {
  sorted.reserve(before.size());
  for (const auto &kv : before) {
    sorted.emplace_back(kv.first, kv.second);
  }
  if (sorted.size() <= k) {
    return;
  }
  std::stable_sort(
      std::begin(sorted), std::end(sorted),
      [](const pair<PAGNode *, u64_t> &a, const pair<PAGNode *, u64_t> &b) {
        return a.second > b.second;
      });
}

void getBlockedNodes(SVFIR *pag) {
  // dataflow cannot through constants
  const auto ConstNode = pag->getGNode(pag->getConstantNode());
  for (const auto &Edge : ConstNode->getOutgoingEdges(PAGEdge::Addr)) {
    BlockedNodes.insert(Edge->getDstID());
  }
  errs() << "blocked consts: " << BlockedNodes.size() << "\n";

  // dummy nodes in pag
  BlockedNodes.insert({0, 1, 2, 3});

  // llvm.compiler.used
  const auto LCU = pag->getGNode(4);
  if (LCU->hasValue() && LCU->getValue()->hasName() &&
      LCU->getValueName() == "llvm.compiler.used") {
    BlockedNodes.insert(4);
  }

  // Func-calls
  unordered_map<const Function *, unsigned int> Callees;
  unordered_map<const Function *, unordered_set<NodeID>> CalleeNodes;
  for (const auto &CallEdge : pag->getSVFStmtSet(PAGEdge::Call)) {
    auto SVFCallee = SVFUtil::getCallee(
        dyn_cast<CallPE>(CallEdge)->getCallInst()->getCallSite());
    if (!SVFCallee) {
      continue;
    }
    auto callee = SVFCallee->getLLVMFun();
    Callees[callee]++;
    CalleeNodes[callee].insert(CallEdge->getDstID());
  }
  for (auto Callee : Callees) {
    if (Callee.second / (Callee.first->arg_size() + 1) > baseNum * 5) {
      if (Callee.first->getName().find('.') == string::npos ||
          Callee.second / (Callee.first->arg_size() + 1) > baseNum * 10) {
        BlockedNodes.insert(CalleeNodes[Callee.first].begin(),
                            CalleeNodes[Callee.first].end());
      }
    }
  }

  unordered_map<const Function *, unsigned int> Rets;
  unordered_map<const Function *, unordered_set<NodeID>> RetNodes;
  for (const auto &RetEdge : pag->getSVFStmtSet(PAGEdge::Ret)) {
    auto SVFCallee = SVFUtil::getCallee(
        dyn_cast<RetPE>(RetEdge)->getCallInst()->getCallSite());
    if (!SVFCallee) {
      continue;
    }
    auto callee = SVFCallee->getLLVMFun();
    Rets[callee]++;
    RetNodes[callee].insert(RetEdge->getSrcID());
  }
  for (auto ret : Rets) {
    if (ret.second > baseNum * 5) {
      if (ret.first->getName().find('.') == string::npos ||
          ret.second > baseNum * 10) {
        BlockedNodes.insert(RetNodes[ret.first].begin(),
                            RetNodes[ret.first].end());
      }
    }
  }

  for (auto i = 0; i < pag->getNodeNumAfterPAGBuild(); i++) {
    auto node = pag->getGNode(i);
    if (node->getIncomingEdges(PAGEdge::Store).size() > baseNum * 50) {
      BlockedNodes.insert(i);
    }
    if (node->getOutgoingEdges(PAGEdge::Store).size() > baseNum * 10) {
      BlockedNodes.insert(i);
    }
    if (node->getOutgoingEdges(PAGEdge::Copy).size() > baseNum * 15) {
      BlockedNodes.insert(i);
    }
    if (node->getOutgoingEdges(PAGEdge::Load).size() > baseNum * 5) {
      BlockedNodes.insert(i);
    }
  }

  for (auto node : Call2Ret) {
    if (node.second.size() > baseNum) {
      BlockedNodes.insert(node.first);
    }
  }
  for (auto node : Ret2Call) {
    if (node.second.size() > baseNum) {
      BlockedNodes.insert(node.first);
    }
  }
  for (auto node : Formal2Real) {
    if (node.second.size() > baseNum) {
      BlockedNodes.insert(node.first);
    }
  }
  for (auto node : Real2Formal) {
    if (node.second.size() > baseNum * 2.5) {
      BlockedNodes.insert(node.first);
    }
  }

  errs() << "BlockedNodes: " << BlockedNodes.size() << "\n";
}

void setupPhiEdges(SVFIR *pag) {
  for (auto edge : pag->getPTASVFStmtSet(PAGEdge::Phi)) {
    const auto phi = dyn_cast<PhiStmt>(edge);
    const auto dst = edge->getDstNode();
    for (auto var : phi->getOpndVars()) {
      if (BlockedNodes.find(var->getId()) == BlockedNodes.end()) {
        phiIn[dst->getId()][edge].insert(var->getId());
        phiOut[var->getId()][edge].insert(dst->getId());
      }
    }
  }
}

void setupSelectEdges(SVFIR *pag) {
  for (auto edge : pag->getPTASVFStmtSet(PAGEdge::Select)) {
    const auto select = dyn_cast<SelectStmt>(edge);
    const auto dst = edge->getDstNode();
    selectIn[dst->getId()][edge].insert(select->getTrueValue()->getId());
    selectIn[dst->getId()][edge].insert(select->getFalseValue()->getId());
    selectOut[select->getTrueValue()->getId()][edge].insert(dst->getId());
    selectOut[select->getFalseValue()->getId()][edge].insert(dst->getId());
  }
}

string printVal(const Value *val) {
  string tmp;
  raw_string_ostream rso(tmp);
  if (isa<Function>(val)) {
    return val->getName().str();
  }
  val->print(rso);
  return rso.str();
}

string printType(const Type *val) {
  string tmp;
  raw_string_ostream rso(tmp);
  val->print(rso);
  if (rso.str().length() > 500) {
    return "too long";
  } else {
    return rso.str();
  }
}

bool deAnonymous = false;
string getStructName(StructType *sttype) {
  auto origin_name = sttype->getStructName().str();
  if (origin_name.find(".anon.") != string::npos) {
    const auto fieldNum = SymbolTableInfo::SymbolInfo()
                              ->getStructInfoIter(sttype)
                              ->second->getNumOfFlattenFields();
    auto stsize = LLVMUtil::getTypeSizeInBytes(sttype);
    return to_string(fieldNum) + "," + to_string(stsize);
  }
  if (origin_name.find("union.") != string::npos) {
    if (origin_name.rfind('.') == 5) {
      return origin_name;
    } else {
      return origin_name.substr(0, origin_name.rfind('.'));
    }
  }
  if (origin_name.find("struct.") != string::npos) {
    if (origin_name.rfind('.') == 6) {
      return origin_name;
    } else {
      return origin_name.substr(0, origin_name.rfind('.'));
    }
  }
  if (deAnonymous) {
    if (deAnonymousStructs.find(sttype) != deAnonymousStructs.end()) {
      return deAnonymousStructs[sttype];
    } else {
      const auto fieldNum = SymbolTableInfo::SymbolInfo()
                                ->getStructInfoIter(sttype)
                                ->second->getNumOfFlattenFields();
      auto stsize = LLVMUtil::getTypeSizeInBytes(sttype);
      return to_string(fieldNum) + "," + to_string(stsize);
    }
  }
  return "";
}

unordered_set<CallInst *> *getSpecificGV(SVFModule *svfmod);

unordered_set<PAGNode *> addrvisited;

bool checkIfAddrTaken(SVFIR *pag, PAGNode *node) {
  if (!addrvisited.insert(node).second) {
    return false;
  }
  if (node->hasOutgoingEdges(PAGEdge::Store)) {
    return true;
  }
  if (phiOut.find(node->getId()) != phiOut.end()) {
    for (auto edge : phiOut[node->getId()]) {
      for (auto nxt : edge.second) {
        if (checkIfAddrTaken(pag, pag->getGNode(nxt))) {
          return true;
        }
      }
    }
  }
  for (auto edge : node->getOutEdges()) {
    if (checkIfAddrTaken(pag, edge->getDstNode())) {
      return true;
    }
  }
  addrvisited.erase(node);
  return false;
}

void addSVFAddrFuncs(SVFModule *svfModule, SVFIR *pag) {
  for (auto F : *svfModule) {
    auto funcnode = pag->getGNode(pag->getValueNode(F->getLLVMFun()));
    addrvisited.clear();
    if (checkIfAddrTaken(pag, funcnode)) {
      type2funcs[printType(F->getLLVMFun()->getType())].insert(F->getLLVMFun());
    }
  }
}

StructType *ifPointToStruct(const Type *tp) {
  if (tp && tp->isPointerTy() && (tp->getNumContainedTypes() > 0)) {
    if (auto ret = dyn_cast<StructType>(tp->getNonOpaquePointerElementType())) {
      return ret;
    } else if (auto ret =
                   dyn_cast<ArrayType>(tp->getNonOpaquePointerElementType())) {
      while (isa<ArrayType>(ret->getArrayElementType())) {
        ret = dyn_cast<ArrayType>(ret->getArrayElementType());
      }
      if (auto st = dyn_cast<StructType>(ret->getArrayElementType())) {
        return st;
      }
    }
  }
  return nullptr;
}

void handleAnonymousStruct(SVFModule *svfModule, SVFIR *pag) {
  unordered_map<StructType *, unordered_set<GlobalVariable *>> AnonymousTypeGVs;
  for (auto ii = svfModule->global_begin(), ie = svfModule->global_end();
       ii != ie; ii++) {
    auto gv = *ii;
    if (auto gvtype = ifPointToStruct(gv->getType())) {
      if (getStructName(gvtype) == "") {
        AnonymousTypeGVs[gvtype].insert(gv);
      }
    }
  }
  for (auto edge : pag->getSVFStmtSet(PAGEdge::Copy)) {
    if (edge->getSrcNode()->getType() && edge->getDstNode()->getType()) {
      auto srcType = ifPointToStruct(edge->getSrcNode()->getType());
      auto dstType = ifPointToStruct(edge->getDstNode()->getType());
      if (srcType && dstType && (srcType != dstType)) {
        if (AnonymousTypeGVs.find(srcType) != AnonymousTypeGVs.end()) {
          if (getStructName(dstType) != "") {
            deAnonymousStructs[srcType] = getStructName(dstType);
            AnonymousTypeGVs.erase(srcType);
          }
        } else if (AnonymousTypeGVs.find(dstType) != AnonymousTypeGVs.end()) {
          if (getStructName(srcType) != "") {
            deAnonymousStructs[dstType] = getStructName(srcType);
            AnonymousTypeGVs.erase(dstType);
          }
        }
      }
    }
  }
  for (auto const edge : pag->getSVFStmtSet(PAGEdge::Gep)) {
    const auto gepstmt = dyn_cast<GepStmt>(edge);
    if (auto callinst = dyn_cast<CallInst>(edge->getValue())) {
      if (callinst->getCalledFunction()->getName().find("memset") ==
          string::npos) {
        if (callinst->arg_size() >= 2) {
          if (auto st1 = ifPointToStruct(
                  callinst->getArgOperand(0)->stripPointerCasts()->getType())) {
            if (auto st2 = ifPointToStruct(callinst->getArgOperand(1)
                                               ->stripPointerCasts()
                                               ->getType())) {
              auto st1name = getStructName(st1);
              auto st2name = getStructName(st2);
              if (st1name != "" && st2name == "") {
                deAnonymousStructs[st2] = st1name;
                if (AnonymousTypeGVs.find(st2) != AnonymousTypeGVs.end()) {
                  AnonymousTypeGVs.erase(st2);
                }
              } else if (st1name == "" && st2name != "") {
                deAnonymousStructs[st1] = st2name;
                if (AnonymousTypeGVs.find(st1) != AnonymousTypeGVs.end()) {
                  AnonymousTypeGVs.erase(st1);
                }
              }
            }
          }
        }
      }
    }
  }
  deAnonymous = true;
}

long varStructVisit(GEPOperator *gepop) {
  long ret = 0;
  bool first = true;
  for (bridge_gep_iterator gi = bridge_gep_begin(*gepop),
                           ge = bridge_gep_end(*gepop);
       gi != ge; ++gi) {
    Type *gepTy = *gi;
    Value *offsetVal = gi.getOperand();
    auto idx = dyn_cast<ConstantInt>(offsetVal);
    if (idx) {
      if (first) {
        first = false;
        continue;
      } else {
        if (gepTy->isArrayTy()) {
          continue;
        } else if (auto sttype = dyn_cast<StructType>(gepTy)) {
          for (auto fd = 0; fd < idx->getSExtValue(); fd++) {
            auto rtype = sttype->getElementType(fd);
            ret += LLVMUtil::getTypeSizeInBytes(rtype);
          }
        } else {
          assert(sttype && "could only be struct");
        }
      }
    }
  }
  return ret;
}

long regularStructVisit(StructType *sttype, s32_t idx, PAGEdge *gep) {
  // ret byteoffset
  long ret = 0;
  const auto stinfo =
      SymbolTableInfo::SymbolInfo()->getStructInfoIter(sttype)->second;
  u32_t lastOriginalType = 0;
  for (auto i = 0; i <= idx; i++) {
    if (stinfo->getOriginalElemType(i)) {
      lastOriginalType = i;
    }
  }
  // Cumulate previous byteoffset
  for (auto i = 0; i < lastOriginalType; i++) {
    if (stinfo->getOriginalElemType(i)) {
      auto rtype = const_cast<Type *>(stinfo->getOriginalElemType(i));
      ret += LLVMUtil::getTypeSizeInBytes(rtype);
    }
  }
  // Check if completed
  if (idx - lastOriginalType >= 0) {
    auto embType = stinfo->getOriginalElemType(lastOriginalType);
    while (embType && embType->isArrayTy()) {
      embType = embType->getArrayElementType();
    }
    if (embType && embType->isStructTy()) {
      ret += regularStructVisit(
          const_cast<StructType *>(dyn_cast<StructType>(embType)),
          idx - lastOriginalType, gep);
    }
  }
  const auto stname = getStructName(sttype);
  if (stname != "") {
    typebasedShortcuts[stname][ret].insert(gep);
    reverseShortcuts[gep][ret].insert(stname);
  }
  return ret;
}

void getSrcNodes(PAGNode *node, unordered_set<PAGNode *> &visitedNodes) {
  if (visitedNodes.insert(node).second) {
    for (auto edge : node->getIncomingEdges(PAGEdge::Copy)) {
      getSrcNodes(edge->getSrcNode(), visitedNodes);
    }
  }
}

void setupStores(SVFIR *pag) {
  for (auto edge : pag->getSVFStmtSet(PAGEdge::Store)) {
    unordered_set<PAGNode *> srcNodes;
    unordered_set<PAGNode *> dstNodes;
    for (auto srcLoad : edge->getSrcNode()->getIncomingEdges(PAGEdge::Load)) {
      getSrcNodes(srcLoad->getSrcNode(), srcNodes);
    }
    getSrcNodes(edge->getDstNode(), dstNodes);

    for (auto dstNode : dstNodes) {
      if (gepIn.find(dstNode) != gepIn.end() &&
          reverseShortcuts.find(gepIn[dstNode]) != reverseShortcuts.end()) {
        for (auto srcNode : srcNodes) {
          if (gepIn.find(srcNode) != gepIn.end() &&
              reverseShortcuts.find(gepIn[srcNode]) != reverseShortcuts.end()) {
            for (auto srcIdx : reverseShortcuts[gepIn[srcNode]]) {
              for (auto srcName : srcIdx.second) {
                for (auto dstIdx : reverseShortcuts[gepIn[dstNode]]) {
                  for (auto dstName : dstIdx.second) {
                    if (srcName != dstName) {
                      additionalShortcuts[dstName][dstIdx.first].insert(
                          &typebasedShortcuts[srcName][srcIdx.first]);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  errs() << "additional shortcuts: " << additionalShortcuts.size() << "\n";
  reverseShortcuts.clear();
  gepIn.clear();
}

StructType *gotStructSrc(PAGNode *node,
                         unordered_set<PAGNode *> &visitedNodes) {
  if (!visitedNodes.insert(node).second) {
    return nullptr;
  }
  for (auto nxt : node->getIncomingEdges(PAGEdge::Copy)) {
    if (auto nxtType = nxt->getSrcNode()->getType()) {
      if (nxtType->isPointerTy() && nxtType->getNumContainedTypes() > 0) {
        auto elemType = nxtType->getPointerElementType();
        while (elemType->isArrayTy()) {
          elemType = elemType->getArrayElementType();
        }
        if (elemType->isStructTy()) {
          return dyn_cast<StructType>(elemType);
        }
      }
      if (auto ret = gotStructSrc(nxt->getSrcNode(), visitedNodes)) {
        return ret;
      }
    }
  }
  visitedNodes.erase(node);
  return nullptr;
}

void collectByteoffset(SVFIR *pag) {
  for (auto const edge : pag->getSVFStmtSet(PAGEdge::Gep)) {
    gepIn[edge->getDstNode()] = edge;
    const auto gepstmt = dyn_cast<GepStmt>(edge);
    if (auto callinst = dyn_cast<CallInst>(edge->getValue())) {
      if (callinst->getCalledFunction()->getName().find("memset") ==
          string::npos) {
        unordered_set<PAGNode *> visitedNodes;
        if (auto sttype = gotStructSrc(edge->getSrcNode(), visitedNodes)) {
          gep2byteoffset[edge] =
              regularStructVisit(sttype, gepstmt->getConstantFieldIdx(), edge);
        } else {
          if (!gepstmt->isVariantFieldGep() && gepstmt->isConstantOffset() &&
              edge->getSrcNode()->getOutgoingEdges(PAGEdge::Gep).size() < 20) {
            gep2byteoffset[edge] = gepstmt->getConstantFieldIdx();
          } else {
            variantGep.insert(edge);
          }
        }
      }
    } else {
      if (auto type = edge->getSrcNode()->getType()) {
        if (type->isPointerTy() && type->getNumContainedTypes() > 0) {
          auto elemType = type->getPointerElementType();
          while (elemType && elemType->isArrayTy()) {
            elemType = elemType->getArrayElementType();
          }
          if (elemType) {
            if (!gepstmt->isConstantOffset()) {
              // contains non-const index
              if (elemType->isSingleValueType()) {
                // gep i8*, %var
                variantGep.insert(edge);
              } else if (elemType->isStructTy()) {
                if (gepstmt->isVariantFieldGep()) {
                  // gep struct.A*, %var
                  gep2byteoffset[edge] = 0;
                } else {
                  gep2byteoffset[edge] =
                      varStructVisit(const_cast<GEPOperator *>(
                          dyn_cast<GEPOperator>(edge->getValue())));
                }
              } else {
                assert(false && "no ther case 1");
              }
            } else {
              if (elemType->isSingleValueType()) {
                gep2byteoffset[edge] = LLVMUtil::getTypeSizeInBytes(
                                           type->getPointerElementType()) *
                                       gepstmt->accumulateConstantOffset();
              } else if (elemType->isStructTy()) {
                gep2byteoffset[edge] =
                    regularStructVisit(dyn_cast<StructType>(elemType),
                                       gepstmt->getConstantFieldIdx(), edge);
              } else {
                assert(false && "no other case 2");
              }
            }
          }
        } else {
          errs() << printVal(edge->getValue()) << "\n";
        }
      }
    }
  }
  errs() << "gep2byteoffset: " << gep2byteoffset.size() << "\n";
  errs() << "variantGep: " << variantGep.size() << "\n";
}

void processCastSites(SVFIR *pag, SVFModule *mod) {
  for (auto edge : pag->getSVFStmtSet(SVFStmt::Copy)) {
    if (edge->getSrcNode()->getType() != edge->getDstNode()->getType()) {
      if (edge->getSrcNode()->getType()) {
        if (auto sttype = ifPointToStruct(edge->getSrcNode()->getType())) {
          castSites[getStructName(sttype)].insert(edge);
        }
      }
      if (edge->getDstNode()->getType()) {
        if (auto sttype = ifPointToStruct(edge->getDstNode()->getType())) {
          castSites[getStructName(sttype)].insert(edge);
        }
      }
    }
  }
  for (auto func : *mod) {
    auto llvmfunc = func->getLLVMFun();
    for (auto &bb : *llvmfunc) {
      for (auto &inst : bb) {
        if (auto call = dyn_cast<CallInst>(&inst)) {
          if (call->getCalledFunction() &&
              call->getCalledFunction()->getName().contains("llvm.memcpy")) {
            Value *Dst = call->getArgOperand(0); // i8*
            string dststr = "dst";
            auto dst_pg = pag->getGNode(pag->getValueNode(Dst));
            SVFStmt *dst_cast = nullptr;
            for (auto dst_dst : dst_pg->getIncomingEdges(SVFStmt::Copy)) {
              if (auto sttype =
                      ifPointToStruct(dst_dst->getSrcNode()->getType())) {
                dststr = getStructName(sttype);
                dst_cast = dst_dst;
                break;
              }
            }
            Value *Src = call->getArgOperand(1); // i8*
            string srcstr = "src";
            auto src_pg = pag->getGNode(pag->getValueNode(Src));
            SVFStmt *src_cast = nullptr;
            for (auto src_src : src_pg->getIncomingEdges(SVFStmt::Copy)) {
              if (auto sttype =
                      ifPointToStruct(src_src->getSrcNode()->getType())) {
                srcstr = getStructName(sttype);
                src_cast = src_src;
                break;
              }
            }
            if (dststr == srcstr) {
              castSites[srcstr].erase(dst_cast);
              castSites[srcstr].erase(src_cast);
            }
          }
        }
      }
    }
  }
}

void readCallGraph(string filename, SVFModule *mod, SVFIR *pag) {
  unordered_map<string, CallInst *> callinstsmap;
  unordered_map<string, Function *> funcsmap;
  for (auto func : *mod) {
    auto llvmfunc = func->getLLVMFun();
    string funcname = llvmfunc->getName().str();
    funcsmap[funcname] = llvmfunc;
    for (auto &bb : *llvmfunc) {
      for (auto &inst : bb) {
        if (auto callinst = dyn_cast<CallInst>(&inst)) {
          if (callinst->isIndirectCall()) {
            callinstsmap[to_string(pag->getValueNode(callinst))] = callinst;
          }
        }
      }
    }
  }

  ifstream fin(filename);
  string callsite, callee;
  u32_t calleenum;
  while (!fin.eof()) {
    fin >> callsite;
    fin >> calleenum;
    for (long i = 0; i < calleenum; i++) {
      fin >> callee;
      callgraph[callinstsmap[callsite]].insert(funcsmap[callee]);
    }
  }
  fin.close();
}

void setupDependence(SVFIR *pag, SVFModule *mod) {
  for (auto func : *mod) {
    auto llvmfunc = func->getLLVMFun();
    for (auto &arg : llvmfunc->args()) {
      if (auto pagnodenum = pag->getValueNode(&arg)) {
        if (BlockedNodes.find(pagnodenum) == BlockedNodes.end()) {
          Param2Funcs[pagnodenum] = llvmfunc;
        }
      }
    }
    for (auto &bb : *llvmfunc) {
      for (auto &inst : bb) {
        if (auto callinst = dyn_cast<CallInst>(&inst)) {
          if (callinst->isIndirectCall()) {
            for (int i = 0; i < callinst->arg_size(); i++) {
              auto arg = callinst->getArgOperand(i);
              if (auto pagnodenum = pag->getValueNode(arg)) {
                if (BlockedNodes.find(pagnodenum) == BlockedNodes.end()) {
                  Arg2iCalls[pagnodenum] = callinst;
                }
              }
            }
          }
        }
      }
    }
  }
}

void setupCallGraph(SVFIR *_pag) {
  for (const auto callinst : callgraph) {
    const auto argsize = callinst.first->arg_size();
    for (const auto callee : callinst.second) {
      if (argsize == callee->arg_size()) {
        for (unsigned int i = 0; i < argsize; i++) {
          if (_pag->hasValueNode(
                  callinst.first->getArgOperand(i)->stripPointerCasts()) &&
              _pag->hasValueNode(callee->getArg(i))) {
            const auto real = _pag->getValueNode(
                callinst.first->getArgOperand(i)->stripPointerCasts());
            const auto formal = _pag->getValueNode(callee->getArg(i));
            Real2Formal[real].insert(formal);
            Formal2Real[formal].insert(real);
            for (auto &bb : *callee) {
              for (auto &inst : bb) {
                if (auto retinst = dyn_cast<ReturnInst>(&inst)) {
                  if (retinst->getNumOperands() != 0 &&
                      callee->getReturnType()->isPointerTy()) {
                    const auto retval =
                        _pag->getValueNode(retinst->getReturnValue());
                    Ret2Call[retval].insert(_pag->getValueNode(callinst.first));
                    Call2Ret[_pag->getValueNode(callinst.first)].insert(retval);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  errs() << "Real2Formal " << Real2Formal.size() << "\n";
  errs() << "Formal2Real " << Formal2Real.size() << "\n";
  errs() << "Ret2Call " << Ret2Call.size() << "\n";
  errs() << "Call2Ret " << Call2Ret.size() << "\n";
}

bool checkTwoTypes(
    Type *src, Type *dst,
    unordered_map<const Type *, unordered_set<const Type *>> &castmap) {
  if (src == dst) {
    return true;
  } else if (src != nullptr && dst != nullptr &&
             castmap[src].find(dst) != castmap[src].end()) {
    return true;
  }
  return false;
}

unordered_map<const Type *, unordered_set<const Type *>> castmap;

void processCastMap(SVFIR *pag) {
  for (auto edge : pag->getSVFStmtSet(PAGEdge::Copy)) {
    if (auto srcType = edge->getSrcNode()->getType()) {
      if (auto dstType = edge->getDstNode()->getType()) {
        if (srcType != dstType) {
          castmap[srcType].insert(dstType);
          castmap[dstType].insert(srcType);
        }
      }
    }
  }
}

bool checkIfMatch(const CallInst *callinst, const Function *callee) {
  bool typeMatch = true;
  if (auto icallType = (callinst->getCalledOperand()->getType())) {
    if (checkTwoTypes(callinst->getCalledOperand()->getType(),
                      callee->getType(), castmap)) {

    } else if (callinst->arg_size() != callee->arg_size()) {
      typeMatch = false;
    } else if (checkTwoTypes(callinst->getType(), callee->getReturnType(),
                             castmap)) {
      for (auto i = 0; i < callinst->arg_size(); i++) {
        auto icallargType = callinst->getArgOperand(i)->getType();
        auto calleeargType = callee->getArg(i)->getType();
        if (!checkTwoTypes(icallargType, calleeargType, castmap)) {
          if (icallargType->isPointerTy() && calleeargType->isPointerTy()) {
            auto icallpto = icallargType->getPointerElementType();
            auto calleepto = calleeargType->getPointerElementType();
            if ((icallpto->isStructTy() && !calleepto->isStructTy()) ||
                (!icallpto->isStructTy() && calleepto->isStructTy())) {
              typeMatch = false;
              break;
            }
          }
        }
      }
    } else if (callinst->getType()->isPointerTy() &&
               callee->getReturnType()->isPointerTy()) {
      auto icallpto = callinst->getType()->getPointerElementType();
      auto calleepto = callee->getReturnType()->getPointerElementType();
      if ((icallpto->isStructTy() && !calleepto->isStructTy()) ||
          (!icallpto->isStructTy() && calleepto->isStructTy())) {
        typeMatch = false;
      }
    }
  }
  return typeMatch;
}

void processArguments(int argc, char **argv, int &arg_num, char **arg_value,
                      std::vector<std::string> &moduleNameVec) {
  for (int i = 0; i < argc; ++i) {
    string argument(argv[i]);
    if (argument.find("@") == 0) {
      bool first_ir_file = true;
      ifstream fin(argument.substr(1));
      string tmp;
      fin >> tmp;
      while (!fin.eof()) {
        if (LLVMUtil::isIRFile(tmp)) {
          if (find(moduleNameVec.begin(), moduleNameVec.end(), tmp) ==
              moduleNameVec.end()) {
            moduleNameVec.push_back(tmp);
          }
          if (first_ir_file) {
            arg_value[arg_num] = argv[i];
            arg_num++;
            first_ir_file = false;
          }
        }
        fin >> tmp;
      }
    } else {
      arg_value[arg_num] = argv[i];
      arg_num++;
    }
  }
}