// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <reloco/string_view.hpp>

int main() {
  reloco::string_view view = "value";
  return view.try_front().has_value() && view.try_front().value().get() == 'v' ? 0 : 1;
}
