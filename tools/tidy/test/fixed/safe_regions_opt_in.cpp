// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-ref-qualified-member

// option: SafeCodeMode=opt-in
// Only code inside safe regions is checked, so vendored code outside never warns.

#include <reloco/lifetime.hpp>

struct vendored {
  int v = 0;
  int &ref() { return v; }  // third-party style: unchecked outside a region
};

RELOCO_BEGIN_SAFE_CODE
struct ours {
  int v = 0;
  int &ref() & noexcept { return v; }  // expect: reloco-ref-qualified-member
  int &ref_ok() & noexcept { return v; }
};
RELOCO_END_SAFE_CODE

struct vendored_again {
  int v = 0;
  int &ref() { return v; }
};

#pragma region reloco_safe_begin
struct literal_pragma {
  int v = 0;
  int &ref() & noexcept { return v; }  // expect: reloco-ref-qualified-member
};
#pragma endregion reloco_safe_end

struct vendored_last {
  int v = 0;
  int &ref() { return v; }
};
