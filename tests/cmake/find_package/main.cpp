// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <reloco/array.hpp>

int main() {
  reloco::array<int, 3> values{1, 2, 3};
  const auto found = values.try_at(1);
  return found.has_value() && found.value().get() == 2 ? 0 : 1;
}
