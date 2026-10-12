// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lock-guard
#include <atomic>
#include <reloco/detail/compat.hpp>
#include <reloco/lifetime.hpp>

struct RELOCO_CAPABILITY("mutex") fake_mutex {
  void lock() noexcept RELOCO_ACQUIRE();
  void unlock() noexcept RELOCO_RELEASE();
};

class ok_all_annotated {
  fake_mutex mu_;
  int count_ RELOCO_GUARDED_BY(mu_) = 0;
  const int id_ = 1;
  std::atomic<int> hits_{0};
  int generation_ RELOCO_UNGUARDED = 0;
};

class no_mutex {
  int plain_ = 0;
};

class bad_class {
  fake_mutex mu_;
  int count_ = 0;  // expect: reloco-lock-guard
  long total_ = 0;  // expect: reloco-lock-guard
};

class suppressed_class {
  fake_mutex mu_;
  int count_ = 0;  // NOLINT(reloco-lock-guard)
};
