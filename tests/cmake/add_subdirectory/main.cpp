// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <reloco/span.hpp>

int main() {
  int values[] = {1, 2, 3};
  reloco::span<int> view(values);
  return view.size() == 3 && view.front() == 1 ? 0 : 1;
}
