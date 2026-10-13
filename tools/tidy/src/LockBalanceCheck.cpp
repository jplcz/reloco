// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-lock-balance: every lock()/lock_shared()/read_lock()/write_lock() must be matched by an unlock on every
// path out of the function. Leaving a lock held is reserved for lock implementations (classes marked as locks,
// RAII guards, RELOCO_LOCK_IMPL, functions that are themselves annotated as acquiring/releasing).
//
// Forward dataflow over the CFG tracking, per lock place, the locks held on every path ("must") and on some path
// ("may"). try_lock results are resolved on the branch edges of the condition that tests them. An unlock without a
// prior lock in the same function is not reported (hand-off); a second unlock of a lock this function already
// released is.

#include "Checks.h"
#include "LockModel.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/CFG.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace reloco::tidy {

namespace {

using Key = std::string;
using Raw = SourceLocation::UIntTy;

enum class Kind { none, acquire, release, try_acquire };
struct Op {
  Kind kind = Kind::none;
  bool shared = false;
};

struct State {
  bool reached = false;
  std::map<Key, Raw> may;
  std::set<Key> must;
  std::set<Key> rel;
  std::map<Key, Raw> tent; // try-acquired, not yet resolved by a branch

  bool operator==(const State &o) const {
    return reached == o.reached && may == o.may && must == o.must && rel == o.rel && tent == o.tent;
  }

  void join(const State &o) {
    if (!o.reached)
      return;
    if (!reached) {
      *this = o;
      return;
    }
    for (const auto &[k, loc] : o.may) {
      auto [it, added] = may.emplace(k, loc);
      if (!added && loc < it->second)
        it->second = loc;
    }
    for (const auto &[k, loc] : o.tent) {
      auto [it, added] = tent.emplace(k, loc);
      if (!added && loc < it->second)
        it->second = loc;
    }
    std::set<Key> m;
    for (const Key &k : must)
      if (o.must.count(k))
        m.insert(k);
    must = std::move(m);
    std::set<Key> r;
    for (const Key &k : rel)
      if (o.rel.count(k))
        r.insert(k);
    rel = std::move(r);
  }
};

bool fnAnnotated(const FunctionDecl *fd, llvm::StringRef name) {
  if (lockmodel::annotated(fd, name))
    return true;
  const FunctionDecl *pt = fd->getPrimaryTemplate() ? fd->getPrimaryTemplate()->getTemplatedDecl() : nullptr;
  if (!pt && fd->getInstantiatedFromMemberFunction())
    pt = fd->getInstantiatedFromMemberFunction();
  return lockmodel::annotated(pt, name);
}

// Functions that are themselves part of a lock implementation: annotated as acquiring or releasing, or marked.
bool isImplementationFn(const FunctionDecl *fd) {
  unsigned ignored = 0;
  for (const char *name : {"reloco_acquires", "reloco_acquires_shared", "reloco_releases", "reloco_try_acquires",
                           "reloco_try_acquires_shared"})
    if (lockmodel::fnArgAnnotation(fd, name, ignored))
      return true;
  return fd->hasAttr<AcquireCapabilityAttr>() || fd->hasAttr<ReleaseCapabilityAttr>() ||
         fd->hasAttr<TryAcquireCapabilityAttr>() || fnAnnotated(fd, "reloco_enter_atomic") ||
         fnAnnotated(fd, "reloco_leave_atomic") || fnAnnotated(fd, "reloco_lock_impl") ||
         lockmodel::inLockImplementation(fd);
}

} // namespace

class LockBalanceCheck::Analysis {
public:
  Analysis(LockBalanceCheck &c, ASTContext &ctx) : check_(c), ctx_(ctx) {}

  void run(const FunctionDecl *fn) {
    CFG::BuildOptions opts;
    opts.setAllAlwaysAdd();
    const std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), &ctx_, opts);
    if (!cfg)
      return;

    std::vector<State> out(cfg->getNumBlockIDs());
    const CFGBlock &entry = cfg->getEntry();
    for (bool changed = true; changed;) {
      changed = false;
      for (const CFGBlock *b : *cfg) {
        State s;
        if (b == &entry) {
          s.reached = true;
        } else {
          for (const CFGBlock::AdjacentBlock &pred : b->preds())
            if (pred.isReachable() && !noReturnEdge(pred.getReachableBlock(), b))
              s.join(edgeState(pred.getReachableBlock(), b, out[pred->getBlockID()]));
        }
        if (s.reached)
          for (const CFGElement &el : *b)
            step(el, s, false);
        if (!(s == out[b->getBlockID()])) {
          out[b->getBlockID()] = s;
          changed = true;
        }
      }
    }

    // Reporting pass: recompute each block's input once more and report.
    State exitState;
    for (const CFGBlock *b : *cfg) {
      State s;
      if (b == &entry) {
        s.reached = true;
      } else {
        for (const CFGBlock::AdjacentBlock &pred : b->preds())
          if (pred.isReachable() && !noReturnEdge(pred.getReachableBlock(), b))
            s.join(edgeState(pred.getReachableBlock(), b, out[pred->getBlockID()]));
      }
      if (!s.reached)
        continue;
      if (b == &cfg->getExit())
        exitState = s;
      for (const CFGElement &el : *b)
        step(el, s, true);
    }
    for (const auto &[k, raw] : exitState.may) {
      const SourceLocation at = SourceLocation::getFromRawEncoding(raw);
      if (!check_.reported_.insert(raw).second)
        continue;
      const bool always = exitState.must.count(k) != 0;
      check_.diag(at, always ? "%0 is locked here but not unlocked on every path out of the function"
                             : "%0 is locked here but may still be held when the function returns")
          << names_[k];
      check_.howToFix(at, "use an RAII guard ('auto g = lock.scoped_lock();' / 'unique_lock') or unlock before every "
                          "return; only lock implementations may leave a lock held (mark them RELOCO_LOCK_IMPL)");
    }
  }

private:
  // A stable key for the lock an expression names, or empty when it cannot be tracked.
  Key place(const Expr *e, int depth = 0) {
    if (!e || depth > 6)
      return {};
    e = e->IgnoreParenImpCasts();
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = llvm::dyn_cast<VarDecl>(dre->getDecl());
      if (!vd)
        return {};
      if (vd->getType()->isReferenceType() && vd->hasInit() && !llvm::isa<ParmVarDecl>(vd))
        return place(vd->getInit(), depth + 1);
      return "v" + std::to_string(reinterpret_cast<uintptr_t>(vd->getCanonicalDecl()));
    }
    if (llvm::isa<CXXThisExpr>(e))
      return "this";
    if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
      const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl());
      if (!fd)
        return {};
      const Key base = place(me->getBase(), depth + 1);
      if (base.empty())
        return {};
      return base + "." + std::to_string(reinterpret_cast<uintptr_t>(fd->getCanonicalDecl()));
    }
    return {};
  }

  Op classify(const CXXMemberCallExpr *call) const {
    const CXXMethodDecl *md = call->getMethodDecl();
    if (!md)
      return {};
    if (const auto *a = md->getAttr<AcquireCapabilityAttr>())
      return {Kind::acquire, a->isShared()};
    if (md->hasAttr<ReleaseCapabilityAttr>())
      return {Kind::release, false};
    if (const auto *a = md->getAttr<TryAcquireCapabilityAttr>())
      return {Kind::try_acquire, a->isShared()};
    if (fnAnnotated(md, "reloco_enter_atomic"))
      return {Kind::acquire, false};
    if (fnAnnotated(md, "reloco_leave_atomic"))
      return {Kind::release, false};
    const Expr *obj = call->getImplicitObjectArgument();
    const CXXRecordDecl *rd = obj ? lockmodel::recordOfType(obj->getType()) : nullptr;
    if (!rd || !lockmodel::isLockRecord(rd) || !md->getIdentifier())
      return {};
    return llvm::StringSwitch<Op>(md->getName())
        .Case("lock", Op{Kind::acquire, false})
        .Case("write_lock", Op{Kind::acquire, false})
        .Case("lock_shared", Op{Kind::acquire, true})
        .Case("read_lock", Op{Kind::acquire, true})
        .Case("unlock", Op{Kind::release, false})
        .Case("unlock_shared", Op{Kind::release, false})
        .Case("read_unlock", Op{Kind::release, false})
        .Case("write_unlock", Op{Kind::release, false})
        .Case("try_lock", Op{Kind::try_acquire, false})
        .Case("try_write_lock", Op{Kind::try_acquire, false})
        .Case("try_lock_shared", Op{Kind::try_acquire, true})
        .Case("try_read_lock", Op{Kind::try_acquire, true})
        .Default(Op{});
  }

  // Acquire-annotated calls that hand back a guard (e.g. guarded_mutex::lock, possibly wrapped in a result) release
  // through the guard's destructor, so they are not tracked.
  static bool yieldsGuard(QualType t) {
    const CXXRecordDecl *rd = lockmodel::recordOfType(t);
    if (!rd)
      return false;
    if (lockmodel::isLockImplementation(rd) && !lockmodel::isLockRecord(rd))
      return true;
    if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
      for (const TemplateArgument &arg : spec->getTemplateArgs().asArray())
        if (arg.getKind() == TemplateArgument::Type && yieldsGuard(arg.getAsType()))
          return true;
    return false;
  }

  // A call to a free function annotated RELOCO_ACQUIRES(n) and friends; the lock is argument n, usually `&m` or a
  // pointer parameter.
  bool freeLockOp(const CallExpr *call, Op &op, Key &key, std::string &display) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd)
      return false;
    unsigned index = 0;
    if (lockmodel::fnArgAnnotation(fd, "reloco_acquires", index))
      op = {Kind::acquire, false};
    else if (lockmodel::fnArgAnnotation(fd, "reloco_acquires_shared", index))
      op = {Kind::acquire, true};
    else if (lockmodel::fnArgAnnotation(fd, "reloco_releases", index))
      op = {Kind::release, false};
    else if (lockmodel::fnArgAnnotation(fd, "reloco_try_acquires", index))
      op = {Kind::try_acquire, false};
    else if (lockmodel::fnArgAnnotation(fd, "reloco_try_acquires_shared", index))
      op = {Kind::try_acquire, true};
    else
      return false;
    if (index >= call->getNumArgs() || call->getArg(index)->isTypeDependent())
      return false;
    const Expr *target = call->getArg(index)->IgnoreParenImpCasts();
    if (const auto *u = llvm::dyn_cast<UnaryOperator>(target); u && u->getOpcode() == UO_AddrOf)
      target = u->getSubExpr()->IgnoreParenImpCasts();
    key = place(target);
    if (key.empty())
      return false;
    if (!names_.count(key)) {
      llvm::raw_string_ostream os(display);
      target->printPretty(os, nullptr, ctx_.getPrintingPolicy());
      names_[key] = os.str();
    }
    return true;
  }

  // The lock a call operates on, or empty when it is not a tracked lock operation.
  bool lockOp(const Stmt *st, Op &op, Key &key, std::string &display) {
    if (const auto *free = llvm::dyn_cast<CallExpr>(st); free && !llvm::isa<CXXMemberCallExpr>(free) &&
                                                          !llvm::isa<CXXOperatorCallExpr>(free))
      return freeLockOp(free, op, key, display);
    const auto *call = llvm::dyn_cast<CXXMemberCallExpr>(st);
    if (!call)
      return false;
    const Expr *obj = call->getImplicitObjectArgument();
    if (!obj || obj->isTypeDependent())
      return false;
    // Guards and scoped locks release in their destructors; their own lock()/unlock() are not tracked.
    const CXXRecordDecl *rd = lockmodel::recordOfType(obj->getType());
    if (rd && lockmodel::isLockImplementation(rd) && !lockmodel::isLockRecord(rd))
      return false;
    op = classify(call);
    if (op.kind == Kind::none || yieldsGuard(call->getType()))
      return false;
    key = place(obj);
    if (key.empty())
      return false;
    if (!names_.count(key)) {
      llvm::raw_string_ostream os(display);
      obj->printPretty(os, nullptr, ctx_.getPrintingPolicy());
      names_[key] = os.str();
    }
    return true;
  }

  void step(const CFGElement &el, State &s, bool report) {
    if (el.getKind() != CFGElement::Statement)
      return;
    Op op;
    Key key;
    std::string display;
    const Stmt *st = el.castAs<CFGStmt>().getStmt();
    if (!lockOp(st, op, key, display))
      return;
    const SourceLocation at = st->getBeginLoc();
    switch (op.kind) {
    case Kind::acquire:
      if (report && !op.shared && s.must.count(key) && !recursive(st) && check_.reported_.insert(at.getRawEncoding()).second) {
        check_.diag(at, "%0 is locked again while this function already holds it") << names_[key];
        check_.howToFix(at, "unlock it first, or use a recursive lock type if re-entry is intended");
      }
      s.may[key] = at.getRawEncoding();
      s.must.insert(key);
      s.rel.erase(key);
      s.tent.erase(key);
      break;
    case Kind::try_acquire:
      s.tent[key] = at.getRawEncoding();
      break;
    case Kind::release: {
      const bool wasTentative = s.tent.erase(key) != 0;
      if (s.may.erase(key) != 0 || wasTentative) {
        s.must.erase(key);
        s.rel.insert(key);
      } else if (report && s.rel.count(key) && check_.reported_.insert(at.getRawEncoding()).second) {
        check_.diag(at, "%0 is unlocked again after this function already released it") << names_[key];
        check_.howToFix(at, "remove the extra unlock; use an RAII guard so release happens exactly once");
      }
      break;
    }
    case Kind::none:
      break;
    }
  }

  static bool recursive(const Stmt *st) {
    const auto *call = llvm::dyn_cast<CXXMemberCallExpr>(st);
    if (!call)
      return false;
    const CXXRecordDecl *rd = lockmodel::recordOfType(call->getImplicitObjectArgument()->getType());
    return rd && rd->getIdentifier() && rd->getName().contains("recursive");
  }

  // A block ending in a noreturn call (trap, abort) reaches the exit block in the CFG, but control never gets there, so
  // a lock still held on that path is not a leak.
  static bool noReturnEdge(const CFGBlock *pred, const CFGBlock *succ) {
    return pred->hasNoReturnElement() && succ->succ_empty();
  }

  // The state flowing along the edge from `pred` to `succ`: a try_lock tested by the predecessor's branch condition
  // becomes held on the success edge and is dropped on the failure edge.
  State edgeState(const CFGBlock *pred, const CFGBlock *succ, State s) {
    if (!s.reached || s.tent.empty())
      return s;
    const Stmt *cond = pred->getTerminatorCondition();
    if (!cond || !llvm::isa<Expr>(cond))
      return s;
    const Expr *e = llvm::cast<Expr>(cond)->IgnoreParenImpCasts();
    bool invert = false;
    while (const auto *u = llvm::dyn_cast<UnaryOperator>(e)) {
      if (u->getOpcode() != UO_LNot)
        break;
      invert = !invert;
      e = u->getSubExpr()->IgnoreParenImpCasts();
    }
    Op op;
    Key key;
    std::string display;
    if (!lockOp(e, op, key, display) || op.kind != Kind::try_acquire)
      return s;
    auto it = s.tent.find(key);
    if (it == s.tent.end())
      return s;
    int index = 0;
    for (const CFGBlock::AdjacentBlock &a : pred->succs()) {
      if (a.getReachableBlock() == succ)
        break;
      ++index;
    }
    const bool success = (index == 0) != invert;
    const Raw loc = it->second;
    s.tent.erase(it);
    if (success) {
      s.may[key] = loc;
      s.must.insert(key);
      s.rel.erase(key);
    }
    return s;
  }

  LockBalanceCheck &check_;
  ASTContext &ctx_;
  std::map<Key, std::string> names_;
};

void LockBalanceCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void LockBalanceCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn->isInvalidDecl() || fn->isDependentContext() || !fn->getBody() || fn->isDefaulted())
    return;
  if (isImplementationFn(fn) || exempt(*result.SourceManager, fn->getLocation()))
    return;
  Analysis(*this, *result.Context).run(fn);
}

} // namespace reloco::tidy
