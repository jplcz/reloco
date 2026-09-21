// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Testbed for reloco's GDB pretty printers. See tools/gdb/testbed/README.md
// for how to build/run this and how to add a case for a new printer.
//
// Every variable below carries a GDB_CHECK marker comment giving a variable
// name and an expected output substring, separated by "=>". `run.sh`
// extracts those comments, breaks at the GDB_BREAK marker near the end of
// main(), runs a print command for each variable, and checks that the
// printed output contains the expected substring.

#include <reloco/array.hpp>
#include <reloco/checked_value.hpp>
#include <reloco/expected.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/function_ref.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/optional.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/span.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>
#include <reloco/vector.hpp>

namespace {

int add_one(int x) { return x + 1; }

} // namespace

int main() {
  // -- array -----------------------------------------------------------
  reloco::array<int, 3> arr_int{{10, 20, 30}};
  // GDB_CHECK: arr_int => reloco::array of length 3

  // -- span ------------------------------------------------------------
  reloco::span<int> span_int(arr_int.data(), arr_int.size());
  // GDB_CHECK: span_int => reloco::span of length 3

  // -- vector ------------------------------------------------------------
  reloco::vector<int> vec_int;
  auto reserve_res = vec_int.try_reserve(4);
  (void)reserve_res;
  (void)vec_int.try_push_back(1);
  (void)vec_int.try_push_back(2);
  (void)vec_int.try_push_back(3);
  // GDB_CHECK: vec_int => reloco::vector of length 3, capacity 4

  // -- inline_vector -------------------------------------------------------
  reloco::inline_vector<int, 4> inline_vec_int;
  (void)inline_vec_int.try_push_back(4);
  (void)inline_vec_int.try_push_back(5);
  // GDB_CHECK: inline_vec_int => reloco::inline_vector of length 2, capacity 4

  // -- flat_set ------------------------------------------------------------
  reloco::flat_set<int> set_int;
  (void)set_int.try_insert(3);
  (void)set_int.try_insert(1);
  (void)set_int.try_insert(2);
  // GDB_CHECK: set_int => reloco::flat_set of length 3

  // -- flat_map ------------------------------------------------------------
  reloco::flat_map<int, int> map_int;
  (void)map_int.try_insert(2, 20);
  (void)map_int.try_insert(1, 10);
  // GDB_CHECK: map_int => reloco::flat_map of length 2

  // -- inline_flat_set -------------------------------------------------------
  reloco::inline_flat_set<int, 4> inline_set_int;
  (void)inline_set_int.try_insert(6);
  (void)inline_set_int.try_insert(5);
  // GDB_CHECK: inline_set_int => reloco::inline_flat_set of length 2, capacity 4

  // -- inline_flat_map -------------------------------------------------------
  reloco::inline_flat_map<int, int, 4> inline_map_int;
  (void)inline_map_int.try_insert(7, 70);
  // GDB_CHECK: inline_map_int => reloco::inline_flat_map of length 1, capacity 4

  // -- basic_string / basic_string_view ------------------------------------
  auto str_hello_res = reloco::string::try_create(reloco::string_view("hello"));
  reloco::string str_hello = str_hello_res ? std::move(str_hello_res.value()) : reloco::string{};
  // GDB_CHECK: str_hello => "hello"
  reloco::string_view sv_hello(str_hello.data(), str_hello.size());
  // GDB_CHECK: sv_hello => "hello"

  // -- optional ------------------------------------------------------------
  reloco::optional<int> opt_empty;
  // GDB_CHECK: opt_empty => reloco::optional [no value]
  reloco::optional<int> opt_full(42);
  // GDB_CHECK: opt_full => value = 42

  // -- expected ------------------------------------------------------------
  reloco::expected<int, reloco::error> exp_ok(7);
  // GDB_CHECK: exp_ok => value = 7
  reloco::expected<int, reloco::error> exp_err(reloco::unexpected(reloco::error::out_of_range));
  // GDB_CHECK: exp_err => out_of_range

  // -- unique_ptr ------------------------------------------------------------
  auto uptr_res = reloco::unique_ptr<int>::try_create(99);
  reloco::unique_ptr<int> uptr_full = uptr_res ? std::move(uptr_res.value()) : reloco::unique_ptr<int>{};
  // GDB_CHECK: uptr_full => reloco::unique_ptr =
  reloco::unique_ptr<int> uptr_empty;
  // GDB_CHECK: uptr_empty => reloco::unique_ptr [empty]

  // -- value_ptr / value_ref ------------------------------------------------
  int value_target = 5;
  reloco::value_ptr<int> vptr_int(&value_target);
  // GDB_CHECK: vptr_int => reloco::value_ptr =
  reloco::value_ref<int> vref_int(value_target);
  // GDB_CHECK: vref_int => reloco::value_ref ->

  // -- checked_value ------------------------------------------------------------
  reloco::checked_value<int> checked_int(3);
  // GDB_CHECK: checked_int => value = 3

  // -- shared_ptr / weak_ptr ------------------------------------------------
  auto sptr_res = reloco::try_create_combined_shared<int>(8);
  reloco::shared_ptr<int> sptr_int = sptr_res ? std::move(sptr_res.value()) : reloco::shared_ptr<int>{};
  // GDB_CHECK: sptr_int => reloco::shared_ptr =
  reloco::weak_ptr<int> wptr_int(sptr_int);
  // GDB_CHECK: wptr_int => reloco::weak_ptr =

  // -- function_ref ------------------------------------------------------------
  reloco::function_ref<int(int)> fref_add_one(add_one);
  // GDB_CHECK: fref_add_one => reloco::function_ref bound at

  // ADD_NEW_CASE_HERE: declare your new type's test variable above this
  // line, with its own `// GDB_CHECK:` comment, before the GDB_BREAK marker.

  int gdb_break_here = 0; // GDB_BREAK
  (void)gdb_break_here;
  return 0;
}
