// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-banned-std-type
#include <optional>
#include <string_view>
#include <vector>

std::optional<int> a;  // expect: reloco-banned-std-type
std::string_view b;  // expect: reloco-banned-std-type
std::vector<int> c;  // expect: reloco-banned-std-type
using ok_alias = int;

std::vector<int> bridged;  // std-interop-ok: deliberate interop
// std-interop-begin: block of bridges
std::optional<int> bridged_block;
// std-interop-end
std::optional<int> after_block;  // expect: reloco-banned-std-type
