// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Model-parameterized fault_injection.hpp/fault_injection_patterns.hpp
// coverage: compiled once per RELOCO_TLS_MODEL (RELOCO_TLS_MODEL_
// THREAD_LOCAL/_SINGLE/_PTHREAD, each forced via a -DRELOCO_TLS_MODEL=...
// command-line define on its own CMake target -- see CMakeLists.txt's
// jplcz_reloco_fault_injection_*_tests targets), matching
// tests/test_tls_provider_models.cpp's own rationale: every backend's
// actual behavior is exercised, not just whichever one happens to be the
// default/host-selected one. This matters specifically for
// fault_injection.hpp, since it auto-selects between its two internal
// designs based on RELOCO_TLS_MODEL (see fault_injection.hpp's own
// file-level docs' "Opting back into one TLS slot per `Tag`" section):
// RELOCO_TLS_MODEL_PTHREAD exercises the shared, single-slot intrusive
// stack (RELOCO_FAULT_INJECTION_UNLIMITED_TLS == 0), while
// RELOCO_TLS_MODEL_THREAD_LOCAL/_SINGLE exercise the one-slot-per-`Tag`
// design (RELOCO_FAULT_INJECTION_UNLIMITED_TLS == 1) -- tests/
// test_fault_injection.cpp and tests/test_fault_injection_patterns.cpp
// only ever exercise whichever one the default main test binary happens
// to select.
#define RELOCO_ENABLE_FAULT_INJECTION

#include <gtest/gtest.h>
#include <reloco/fault_injection_patterns.hpp>

#if RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_THREAD_LOCAL
#define FAULT_INJECTION_MODEL_SUITE FaultInjectionModelTest_ThreadLocal
#elif RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_SINGLE
#define FAULT_INJECTION_MODEL_SUITE FaultInjectionModelTest_Single
#elif RELOCO_TLS_MODEL == RELOCO_TLS_MODEL_PTHREAD
#define FAULT_INJECTION_MODEL_SUITE FaultInjectionModelTest_Pthread
#else
#error "test_fault_injection_backends.cpp: unhandled RELOCO_TLS_MODEL"
#endif

namespace {

struct model_simple_point {};
struct model_outer_point {};
struct model_inner_point {};
struct model_set_point {};
struct model_fire_once_point {};

} // namespace

TEST(FAULT_INJECTION_MODEL_SUITE, NoOpWhenNothingArmed) {
  EXPECT_FALSE(reloco::fault_armed<model_simple_point>());
  RELOCO_FAULT_POINT(model_simple_point); // must not crash/assert with nothing armed
}

TEST(FAULT_INJECTION_MODEL_SUITE, HookFiresOnlyWhileArmed) {
  int fire_count = 0;
  auto hook = [&] { ++fire_count; };

  {
    reloco::fault_injector<model_simple_point> fi(hook);
    EXPECT_TRUE(reloco::fault_armed<model_simple_point>());
    RELOCO_FAULT_POINT(model_simple_point);
    RELOCO_FAULT_POINT(model_simple_point);
    EXPECT_EQ(2, fire_count);
  }

  EXPECT_FALSE(reloco::fault_armed<model_simple_point>());
  RELOCO_FAULT_POINT(model_simple_point);
  EXPECT_EQ(2, fire_count); // no further increments once disarmed
}

TEST(FAULT_INJECTION_MODEL_SUITE, NestedDifferentTagsBothRemainFindable) {
  int outer_calls = 0;
  int inner_calls = 0;
  auto hook_outer = [&] { ++outer_calls; };
  auto hook_inner = [&] { ++inner_calls; };

  reloco::fault_injector<model_outer_point> outer(hook_outer);
  RELOCO_FAULT_POINT(model_outer_point);
  EXPECT_EQ(1, outer_calls);

  {
    reloco::fault_injector<model_inner_point> inner(hook_inner);
    RELOCO_FAULT_POINT(model_inner_point);
    RELOCO_FAULT_POINT(model_outer_point); // outer_point still findable while inner is nested
    EXPECT_EQ(1, inner_calls);
    EXPECT_EQ(2, outer_calls);
  }

  RELOCO_FAULT_POINT(model_outer_point);
  EXPECT_EQ(3, outer_calls);
  EXPECT_EQ(1, inner_calls); // inner_point's hook did not fire again after its guard died
}

TEST(FAULT_INJECTION_MODEL_SUITE, ConvenienceMacroSetOverwritesUnconditionally) {
  RELOCO_FAULT_SET(fi, model_set_point, int, 99);

  int x = 1;
  RELOCO_FAULT_POINT_ARGS(model_set_point, x);
  EXPECT_EQ(99, x);
}

TEST(FAULT_INJECTION_MODEL_SUITE, ConvenienceMacroFireOnceFiresExactlyOnce) {
  int fires = 0;
  RELOCO_FAULT_FIRE_ONCE(fi, model_fire_once_point, [&] { ++fires; });

  RELOCO_FAULT_POINT(model_fire_once_point);
  RELOCO_FAULT_POINT(model_fire_once_point);
  RELOCO_FAULT_POINT(model_fire_once_point);
  EXPECT_EQ(1, fires);
}
