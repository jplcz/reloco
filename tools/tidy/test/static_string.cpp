// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-static-string, reloco-no-raw-pointer

// Fixture for RELOCO_STATIC_STR: the pointer is accepted by reloco-no-raw-pointer, and
// reloco-static-string verifies that only static-lifetime strings flow into it.

#define RELOCO_STATIC_STR [[clang::annotate_type("reloco_static_str")]]

const char *RELOCO_STATIC_STR literal_name() noexcept { return "widget"; }
const char *RELOCO_STATIC_STR forwarded() noexcept { return literal_name(); }
const char *RELOCO_STATIC_STR choose(bool b) noexcept { return b ? "a" : "b"; }
const char *RELOCO_STATIC_STR none() noexcept { return nullptr; }

static const char table[] = "static storage";
const char *RELOCO_STATIC_STR from_table() noexcept { return table; }

const char *RELOCO_STATIC_STR from_buffer(const char *buf) noexcept {  // expect: reloco-no-raw-pointer
  return buf;  // expect: reloco-static-string
}

void log_msg(const char *RELOCO_STATIC_STR msg) noexcept;
void use(char *dynamic) noexcept {  // expect: reloco-no-raw-pointer
  char local[8] = {};
  log_msg("literal");
  log_msg(literal_name());
  log_msg(dynamic);  // expect: reloco-static-string
  log_msg(local);  // expect: reloco-static-string
}

struct holder {
  const char *RELOCO_STATIC_STR name = "x";
  const char *plain = "y";  // expect: reloco-no-raw-pointer
};

void assign(holder &h, const char *dynamic) noexcept {  // expect: reloco-no-raw-pointer
  h.name = "ok";
  h.name = dynamic;  // expect: reloco-static-string
}
