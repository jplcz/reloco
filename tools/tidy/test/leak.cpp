// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-leak
#include <reloco/lifetime.hpp>
#include <utility>

struct RELOCO_MUST_CONSUME txn {
  txn() noexcept = default;
  txn(txn &&) noexcept = default;
  RELOCO_CONSUMES void commit() noexcept;
  void touch() noexcept;
};

void submit(txn t) noexcept;
txn make() noexcept;

void ok_commit() noexcept {
  txn t;
  t.touch();
  t.commit();
}
void ok_handoff() noexcept {
  txn t;
  submit(std::move(t));
}
txn ok_return() noexcept {
  txn t;
  return t;
}
void ok_both_branches(bool c) noexcept {
  txn t;
  if (c)
    t.commit();
  else
    submit(std::move(t));
}
void ok_param(txn t) noexcept { t.commit(); }

void bad_never() noexcept {
  txn t;
  t.touch();
}  // expect: reloco-leak
void bad_one_branch(bool c) noexcept {
  txn t;
  if (c)
    t.commit();
}  // expect: reloco-leak
void bad_early_return(bool c) noexcept {
  txn t;
  if (c)
    return;  // expect: reloco-leak
  t.commit();
}
void bad_param(txn t) noexcept {
  t.touch();
}  // expect: reloco-leak
