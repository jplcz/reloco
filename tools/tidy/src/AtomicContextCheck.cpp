// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-atomic-context: kernel-style atomic context checking.
//
//  * RELOCO_MIGHT_SLEEP functions must not be called while atomic context is held. A function that calls a sleeping
//    function is inferred to sleep (transitively, over bodies visible in the translation unit).
//  * RELOCO_ATOMIC_ONLY functions (per-CPU data) must only be called while atomic context is held.
//  * Atomic context is established by RELOCO_ATOMIC_SCOPE RAII objects (to the end of their block, exactly where
//    the destructor runs), RELOCO_ENTER_ATOMIC / RELOCO_LEAVE_ATOMIC calls, RELOCO_ATOMIC_TOKEN parameters,
//    RELOCO_ATOMIC_CALLBACK lambdas, and function annotations RELOCO_ATOMIC_ONLY / RELOCO_ATOMIC_ENTRY.
//
// The analysis is a forward dataflow over the CFG tracking the minimum and maximum atomic depth on any path
// reaching a point: "may sleep" is reported when the maximum is positive, "needs atomic" when the minimum is zero.

#include "Checks.h"
#include "LockModel.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"

#include <algorithm>
#include <memory>

namespace reloco::tidy {

namespace {

bool declHas(const Decl *d, llvm::StringRef name) {
  if (!d)
    return false;
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

// The annotation on the record, or on the primary template for a specialisation.
bool recordHas(const CXXRecordDecl *rd, llvm::StringRef name) {
  if (!rd)
    return false;
  if (declHas(rd, name))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return declHas(spec->getSpecializedTemplate()->getTemplatedDecl(), name);
  return false;
}

const CXXRecordDecl *recordOf(QualType t);

// A scope object: RELOCO_ATOMIC_SCOPE, or a RELOCO_ATOMIC_GUARD template specialised on a RELOCO_ATOMIC_LOCK type.
bool isScopeRecord(const CXXRecordDecl *rd) {
  if (!rd)
    return false;
  if (lockmodel::classHas(rd, "reloco_atomic_scope"))
    return true;
  const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd);
  if (!spec || !declHas(spec->getSpecializedTemplate()->getTemplatedDecl(), "reloco_atomic_guard"))
    return false;
  for (const TemplateArgument &a : spec->getTemplateArgs().asArray())
    if (a.getKind() == TemplateArgument::Type && lockmodel::isAtomicLockRecord(recordOf(a.getAsType())))
      return true;
  return false;
}

const CXXRecordDecl *recordOf(QualType t) {
  t = t.getNonReferenceType();
  if (t.isNull() || t->isDependentType() || t->isArrayType())
    return nullptr;
  return t.getCanonicalType()->getAsCXXRecordDecl();
}

// An annotation on the function itself, its template pattern, or the class it is a member of.
bool fnHas(const FunctionDecl *fd, llvm::StringRef name) {
  if (!fd)
    return false;
  if (declHas(fd, name))
    return true;
  if (const FunctionDecl *pattern = fd->getTemplateInstantiationPattern(); pattern && declHas(pattern, name))
    return true;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd))
    return recordHas(md->getParent(), name);
  return false;
}

bool isExempt(const FunctionDecl *fd) { return fnHas(fd, "reloco_atomic_exempt"); }

const FunctionDecl *calleeOf(const Stmt *s) {
  if (const auto *call = llvm::dyn_cast<CallExpr>(s))
    return call->getDirectCallee();
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(s))
    return ce->getConstructor();
  return nullptr;
}

bool paramHas(const FunctionDecl *fd, unsigned i, llvm::StringRef name) {
  for (const FunctionDecl *r : fd->redecls())
    if (i < r->getNumParams() && declHas(r->getParamDecl(i), name))
      return true;
  return false;
}

// Atomic depth range over all paths reaching a point.
struct State {
  bool reached = false;
  int lo = 0;
  int hi = 0;
  SourceLocation entry; // where the outermost atomic scope of the deepest path began

  bool operator==(const State &o) const { return reached == o.reached && lo == o.lo && hi == o.hi; }
  void join(const State &o) {
    if (!o.reached)
      return;
    if (!reached) {
      *this = o;
      return;
    }
    if (o.hi > hi || (o.hi == hi && entry.isInvalid()))
      entry = o.entry;
    lo = std::min(lo, o.lo);
    hi = std::max(hi, o.hi);
  }
};

} // namespace

// One analysis context: the function being analysed plus access to the check's diagnostics and caches.
class AtomicContextCheck::Analysis {
public:
  Analysis(AtomicContextCheck &check, ASTContext &ctx) : check_(check), ctx_(ctx), sm_(ctx.getSourceManager()) {}

  // The call chain through which `fd` may sleep, e.g. "mutex::lock"; empty when it does not.
  std::string sleepChain(const FunctionDecl *fd) {
    if (!fd || isExempt(fd))
      return {};
    fd = fd->getCanonicalDecl();
    if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd); md && md->getParent()->isLambda())
      return {}; // a lambda body is judged where the lambda is created
    if (fnHas(fd, "reloco_atomic_only") || fnHas(fd, "reloco_atomic_entry"))
      return {}; // by contract these never sleep; violations are reported inside them
    if (fnHas(fd, "reloco_might_sleep"))
      return name(fd);
    if (auto it = check_.sleepMemo_.find(fd); it != check_.sleepMemo_.end())
      return it->second;
    if (!check_.sleepBusy_.insert(fd).second)
      return {};
    std::string chain;
    const FunctionDecl *def = nullptr;
    if (fd->hasBody(def) && def->getBody()) {
      struct Finder : RecursiveASTVisitor<Finder> {
        Analysis &a;
        std::string chain;
        explicit Finder(Analysis &an) : a(an) {}
        bool shouldVisitLambdaBody() const { return false; }
        bool shouldVisitTemplateInstantiations() const { return true; }
        bool visit(const Stmt *s) {
          if (const FunctionDecl *callee = calleeOf(s)) {
            if (std::string c = a.sleepChain(callee); !c.empty()) {
              chain = a.name(callee) == c ? c : a.name(callee) + " -> " + c;
              return false;
            }
          }
          return true;
        }
        bool VisitCallExpr(CallExpr *c) { return visit(c); }
        bool VisitCXXConstructExpr(CXXConstructExpr *c) { return visit(c); }
      } finder(*this);
      finder.TraverseStmt(def->getBody());
      chain = finder.chain;
    }
    check_.sleepBusy_.erase(fd);
    check_.sleepMemo_[fd] = chain;
    return chain;
  }

  // Analyses `fn` starting in `base`; lambdas created inside are analysed in the state they are created in.
  void run(const FunctionDecl *fn, State base) {
    if (!fn->hasBody() || fn->isInvalidDecl() || fn->isDependentContext())
      return;
    CFG::BuildOptions opts;
    opts.setAllAlwaysAdd();
    opts.AddImplicitDtors = true;
    opts.AddTemporaryDtors = true;
    opts.AddInitializers = true;
    const std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), &ctx_, opts);
    if (!cfg)
      return;

    std::vector<State> in(cfg->getNumBlockIDs()), out(cfg->getNumBlockIDs());
    const CFGBlock &entry = cfg->getEntry();
    for (bool changed = true; changed;) {
      changed = false;
      for (const CFGBlock *b : *cfg) {
        State s;
        if (b == &entry) {
          s = base;
        } else {
          for (const CFGBlock::AdjacentBlock &pred : b->preds())
            if (pred.isReachable())
              s.join(out[pred->getBlockID()]);
        }
        in[b->getBlockID()] = s;
        if (s.reached)
          for (const CFGElement &el : *b)
            step(el, s, nullptr);
        if (!(s == out[b->getBlockID()])) {
          out[b->getBlockID()] = s;
          changed = true;
        }
      }
    }

    std::vector<std::pair<const LambdaExpr *, State>> lambdas;
    for (const CFGBlock *b : *cfg) {
      State s = in[b->getBlockID()];
      if (!s.reached)
        continue;
      for (const CFGElement &el : *b)
        step(el, s, &lambdas);
    }
    for (auto &[lambda, st] : lambdas) {
      State inner = st;
      if (isCallbackArgument(lambda)) {
        ++inner.lo;
        ++inner.hi;
        if (inner.entry.isInvalid())
          inner.entry = lambda->getBeginLoc();
      }
      if (const CXXMethodDecl *op = lambda->getCallOperator(); op && lambdaSeen_.insert(op).second)
        run(op, inner);
    }
  }

  // Ambient state of a function from its annotations and parameters.
  State baseOf(const FunctionDecl *fn) {
    State s;
    s.reached = true;
    bool atomic = fnHas(fn, "reloco_atomic_only") || fnHas(fn, "reloco_atomic_entry");
    for (const ParmVarDecl *p : fn->parameters())
      atomic = atomic || recordHas(recordOf(p->getType()), "reloco_atomic_token");
    if (atomic) {
      s.lo = s.hi = 1;
      s.entry = fn->getLocation();
    }
    return s;
  }

  std::string name(const FunctionDecl *fd) const {
    std::string n;
    if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd); md && md->getParent()->getIdentifier())
      n = md->getParent()->getNameAsString() + "::";
    return n + fd->getNameAsString();
  }

private:
  void step(const CFGElement &el, State &s, std::vector<std::pair<const LambdaExpr *, State>> *lambdas) {
    const bool report = lambdas != nullptr;
    switch (el.getKind()) {
    case CFGElement::Statement:
    case CFGElement::Constructor: {
      const Stmt *st = el.castAs<CFGStmt>().getStmt();
      if (const auto *le = llvm::dyn_cast<LambdaExpr>(st)) {
        if (lambdas)
          lambdas->emplace_back(le, s);
        return;
      }
      call(st, s, report);
      return;
    }
    case CFGElement::AutomaticObjectDtor: {
      const VarDecl *vd = el.castAs<CFGAutomaticObjDtor>().getVarDecl();
      if (vd && isScopeRecord(recordOf(vd->getType())) && !vd->getType()->isArrayType())
        leave(s);
      return;
    }
    case CFGElement::TemporaryDtor: {
      const CXXBindTemporaryExpr *bt = el.castAs<CFGTemporaryDtor>().getBindTemporaryExpr();
      if (bt && isScopeRecord(recordOf(bt->getType())))
        leave(s);
      return;
    }
    default:
      return;
    }
  }

  static void enter(State &s, SourceLocation at) {
    if (s.hi == 0 || s.entry.isInvalid())
      s.entry = at;
    ++s.lo;
    ++s.hi;
  }
  static void leave(State &s) {
    s.lo = std::max(0, s.lo - 1);
    s.hi = std::max(0, s.hi - 1);
  }

  void call(const Stmt *st, State &s, bool report) {
    const FunctionDecl *fd = calleeOf(st);
    if (!fd)
      return;
    const SourceLocation at = st->getBeginLoc();
    // A scope object coming into existence: constructed in place, or returned by value from a factory.
    const bool makesScope = llvm::isa<CXXConstructExpr>(st) &&
                            isScopeRecord(recordOf(llvm::cast<CXXConstructExpr>(st)->getType()));
    const auto *callExpr = llvm::dyn_cast<CallExpr>(st);
    const bool returnsScope = callExpr && !callExpr->getType()->isReferenceType() &&
                              isScopeRecord(recordOf(callExpr->getType()));
    if (isExempt(fd)) {
      if (makesScope || returnsScope)
        enter(s, at);
      return;
    }
    if (report) {
      if (s.hi > 0) {
        std::string chain = sleepChain(fd);
        if (!chain.empty())
          diagSleep(at, fd, chain, s);
      }
      if (s.lo == 0 && fnHas(fd, "reloco_atomic_only"))
        diagNeedsAtomic(at, fd);
    }
    if (makesScope || returnsScope || fnHas(fd, "reloco_enter_atomic"))
      enter(s, at);
    if (fnHas(fd, "reloco_leave_atomic"))
      leave(s);
  }

  void diagSleep(SourceLocation at, const FunctionDecl *fd, const std::string &chain, const State &s) {
    if (!shouldReport(at))
      return;
    const std::string via = chain == name(fd) ? "" : " (via " + chain + ")";
    check_.diag(at, "call to '%0' may sleep%1, but this point is in atomic context") << name(fd) << via;
    if (s.entry.isValid())
      check_.diag(s.entry, "atomic context is entered here", DiagnosticIDs::Note);
    check_.howToFix(at, "end the atomic scope before the call (let the guard go out of scope), use a non-blocking variant "
                        "such as try_lock, or, if the callee does not sleep in this configuration, mark it "
                        "RELOCO_ATOMIC_EXEMPT with a comment saying why");
  }

  void diagNeedsAtomic(SourceLocation at, const FunctionDecl *fd) {
    if (!shouldReport(at))
      return;
    check_.diag(at, "'%0' requires atomic context, but this point may be preemptible") << name(fd);
    check_.howToFix(at, "take an RAII guard (RELOCO_ATOMIC_SCOPE, e.g. an irq or preemption guard) before the call, pass "
                        "the proof token, or mark the enclosing function RELOCO_ATOMIC_ONLY when every caller holds "
                        "atomic context");
  }

  bool shouldReport(SourceLocation at) {
    return !check_.exempt(sm_, at) && check_.reported_.insert(at.getRawEncoding()).second;
  }

  // The lambda is an argument for a parameter marked RELOCO_ATOMIC_CALLBACK.
  bool isCallbackArgument(const LambdaExpr *le) {
    DynTypedNode node = DynTypedNode::create(*static_cast<const Stmt *>(le));
    for (int depth = 0; depth < 8; ++depth) {
      auto parents = ctx_.getParents(node);
      if (parents.empty())
        return false;
      node = parents[0];
      const auto *call = node.get<CallExpr>();
      const auto *ctor = node.get<CXXConstructExpr>();
      if (!call && !ctor)
        continue;
      const FunctionDecl *fd = call ? call->getDirectCallee() : ctor->getConstructor();
      if (!fd)
        return false;
      const unsigned n = call ? call->getNumArgs() : ctor->getNumArgs();
      const unsigned skip = call && llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1 : 0;
      for (unsigned i = skip; i < n; ++i) {
        const Expr *arg = call ? call->getArg(i) : ctor->getArg(i);
        if (arg->getSourceRange().fullyContains(le->getSourceRange()) && i - skip < fd->getNumParams())
          return paramHas(fd, i - skip, "reloco_atomic_callback");
      }
      return false;
    }
    return false;
  }

  AtomicContextCheck &check_;
  ASTContext &ctx_;
  const SourceManager &sm_;
  std::set<const CXXMethodDecl *> lambdaSeen_;
};

void AtomicContextCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void AtomicContextCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn->isInvalidDecl() || fn->isDependentContext())
    return;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && md->getParent()->isLambda())
    return; // analysed with its enclosing function, in the state the lambda is created in
  if (isExempt(fn) || exempt(*result.SourceManager, fn->getLocation()))
    return;

  Analysis an(*this, *result.Context);
  if (fnHas(fn, "reloco_might_sleep") && (declHas(fn, "reloco_atomic_only") || declHas(fn, "reloco_atomic_entry")) &&
      reported_.insert(fn->getLocation().getRawEncoding()).second)
    diag(fn->getLocation(), "'%0' is marked both RELOCO_MIGHT_SLEEP and RELOCO_ATOMIC_ONLY/RELOCO_ATOMIC_ENTRY") << an.name(fn);
  if (fn->isThisDeclarationADefinition() && fn->getBody())
    an.run(fn, an.baseOf(fn));
}

} // namespace reloco::tidy
