// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-signed-index
#include <cstddef>
#include <vector>

int ok_unsigned(const std::vector<int> &v, std::size_t i) noexcept { return v[i]; }
int ok_constant(const std::vector<int> &v) noexcept { return v[2]; }
int ok_masked(const int (&a)[16], int i) noexcept { return a[i & 15]; }  // NOLINT(reloco-no-c-array)
int ok_mod_unsigned(const std::vector<int> &v, unsigned i) noexcept { return v.at(i % 4u); }
int ok_small_unsigned(const int (&a)[4], unsigned char i) noexcept { return a[i & 3]; }  // NOLINT(reloco-no-c-array)

int bad_int(const std::vector<int> &v, int i) noexcept { return v[i]; }  // expect: reloco-signed-index
int bad_at(const std::vector<int> &v, long i) noexcept { return v.at(i); }  // expect: reloco-signed-index
int bad_negative_constant(const std::vector<int> &v) noexcept { return v[-1]; }  // expect: reloco-signed-index
int bad_array(const int *p, int i) noexcept { return p[i]; }  // expect: reloco-signed-index
int bad_sub(const std::vector<int> &v, int i) noexcept { return v[i - 1]; }  // expect: reloco-signed-index
int suppressed(const std::vector<int> &v, int i) noexcept { return v[i]; }  // NOLINT(reloco-signed-index)
