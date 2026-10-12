// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-ref-qualified-member

// Fixture for reloco-ref-qualified-member.

struct [[gsl::Pointer]] handle {
  int *p;
  int get() const noexcept { return *p; }  // handle const accessors are fine
};

struct widget {
  int v = 0;

  int value() const noexcept { return v; }          // value result: fine on temporaries
  int &ref() noexcept { return v; }                 // expect: reloco-ref-qualified-member
  int &ref_ok() & noexcept { return v; }
  const int &cref() const noexcept { return v; }    // expect: reloco-ref-qualified-member
  const int &cref_lv() const & noexcept { return v; } // expect: reloco-ref-qualified-member
  const int &cref_ok() const & noexcept { return v; }
  const int &cref_ok() const && = delete;
  void set(int x) noexcept { v = x; }               // expect: reloco-ref-qualified-member
  void set_ok(int x) & noexcept { v = x; }
  static int make() noexcept { return 1; }
  widget &operator=(const widget &) noexcept = default;
  virtual void hook() noexcept {}
};

template <typename T> struct box {
  T v;
  T &get() noexcept { return v; }                   // expect: reloco-ref-qualified-member
  T &get_ok() & noexcept { return v; }
};
