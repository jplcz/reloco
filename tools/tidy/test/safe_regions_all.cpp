// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-ref-qualified-member

// Default mode (reloco, microfmt, structo): everything is checked except unsafe regions.

#include <reloco/lifetime.hpp>

struct checked {
  int v = 0;
  int &ref() noexcept { return v; }  // expect: reloco-ref-qualified-member
};

RELOCO_BEGIN_UNSAFE_CODE
struct vendored {
  int v = 0;
  int &ref() { return v; }
};
RELOCO_END_UNSAFE_CODE

RELOCO_BEGIN_SAFE_CODE
struct explicit_safe {
  int v = 0;
  int &ref() noexcept { return v; }  // expect: reloco-ref-qualified-member
};
RELOCO_END_SAFE_CODE

struct checked_again {
  int v = 0;
  int &ref() noexcept { return v; }  // expect: reloco-ref-qualified-member
};
