// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef RELOCO_IPC_RING_H
#define RELOCO_IPC_RING_H

#ifdef RELOCO_IPC_LINUX_KERNEL
/* Linux Kernel Space */
#include <asm/barrier.h>
#include <linux/string.h>
#include <linux/types.h>

#define RELOCO_IPC_LOAD_ACQUIRE(ptr) smp_load_acquire(ptr)
#define RELOCO_IPC_STORE_RELEASE(ptr, val) smp_store_release((ptr), (val))
#define RELOCO_IPC_LOAD_RELAXED(ptr) READ_ONCE(*(ptr))
#elif defined(RELOCO_IPC_FREEBSD_KERNEL)
/* ---- FreeBSD Kernel Space ---- */
#include <machine/atomic.h>
#include <sys/systm.h> /* For memcpy */
#include <sys/types.h>

/* FreeBSD atomic API natively supports acquire/release semantics */
#define RELOCO_IPC_LOAD_ACQUIRE(ptr) atomic_load_acq_64((volatile uint64_t *)(ptr))
#define RELOCO_IPC_STORE_RELEASE(ptr, val) atomic_store_rel_64((volatile uint64_t *)(ptr), (val))
#define RELOCO_IPC_LOAD_RELAXED(ptr) atomic_load_64((volatile uint64_t *)(ptr))
#else
/* User Space or Bare-Metal C/C++ */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RELOCO_IPC_LOAD_ACQUIRE(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define RELOCO_IPC_STORE_RELEASE(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
#define RELOCO_IPC_LOAD_RELAXED(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RELOCO_IPC_MAGIC 0x524C434F /* 'RLCO' */
#define RELOCO_IPC_VERSION 1
#define RELOCO_IPC_CACHE_LINE 64

/* Cross-compiler alignment macro */
#if defined(__cplusplus)
#define RELOCO_IPC_ALIGN(x) alignas(x)
#elif defined(__GNUC__) || defined(__clang__)
#define RELOCO_IPC_ALIGN(x) __attribute__((aligned(x)))
#else
#define RELOCO_IPC_ALIGN(x)
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

/**
 * @brief Strictly standard-layout shared memory page.
 * @note All synchronization fields are raw integers. The caller MUST use
 * proper atomic built-ins or std::atomic_ref to read/write them.
 */
struct reloco_ipc_spsc_page {
  /* ---- CACHE LINE 0: VALIDATION HEADER ---- */
  uint32_t magic;
  uint32_t version;
  uint32_t untrusted_elem_size;
  uint32_t untrusted_capacity;

  /* ---- CACHE LINE 1: PRODUCER STATE ---- */
  RELOCO_IPC_ALIGN(RELOCO_IPC_CACHE_LINE)
  uint64_t write_idx;

  /* ---- CACHE LINE 2: CONSUMER STATE ---- */
  RELOCO_IPC_ALIGN(RELOCO_IPC_CACHE_LINE)
  uint64_t read_idx;

  /* ---- CACHE LINE 3+: PAYLOAD ---- */
  RELOCO_IPC_ALIGN(RELOCO_IPC_CACHE_LINE)
  uint8_t payload[];
};

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief Validates the memory page.
 * @return 0 on success, -1 on fatal mismatch.
 */
static inline int reloco_ipc_validate_mount(const struct reloco_ipc_spsc_page *page, uint32_t expected_elem_size,
                                            uint32_t expected_capacity) {
  if (!page)
    return -1;
  if (page->magic != RELOCO_IPC_MAGIC)
    return -1;
  if (page->version != RELOCO_IPC_VERSION)
    return -1;
  if (page->untrusted_elem_size != expected_elem_size)
    return -1;
  if (page->untrusted_capacity != expected_capacity)
    return -1;
  return 0;
}

/* =========================================================================
 * PRODUCER API (Writer Context)
 * ========================================================================= */

struct reloco_ipc_producer {
  struct reloco_ipc_spsc_page *page;
  uint64_t cached_read_idx;
  uint32_t capacity;  /* Trusted, local copy */
  uint32_t elem_size; /* Trusted, local copy */
};

/**
 * @brief Mounts the page for writing.
 */
static inline int reloco_ipc_producer_init(struct reloco_ipc_producer *p, struct reloco_ipc_spsc_page *page,
                                           const uint32_t elem_size, const uint32_t expected_capacity) {
  if (reloco_ipc_validate_mount(page, elem_size, expected_capacity) != 0)
    return -1;
  p->page = page;
  p->cached_read_idx = RELOCO_IPC_LOAD_ACQUIRE(&page->read_idx);
  /* Lock the verified constants into local trusted memory */
  p->capacity = expected_capacity;
  p->elem_size = elem_size;
  return 0;
}

/**
 * @brief Attempts to write `count` elements. All-or-nothing.
 * @return Number of elements written (will be `count` or `0` if full).
 */
static inline uint32_t reloco_ipc_try_write(struct reloco_ipc_producer *p, const void *data, uint32_t count) {
  struct reloco_ipc_spsc_page *page = p->page;
  uint64_t w = RELOCO_IPC_LOAD_RELAXED(&page->write_idx);
  uint64_t r = p->cached_read_idx;
  uint32_t cap = p->capacity;
  uint64_t available;
  uint32_t mask;
  uint32_t physical_w;
  uint32_t first_chunk;
  uint32_t elem_sz;
  const uint8_t *src;

  uint64_t in_use = w - r;

  /* Demand-driven cache refresh */
  if (in_use >= cap || cap - in_use < count) {
    r = RELOCO_IPC_LOAD_ACQUIRE(&page->read_idx);
    p->cached_read_idx = r;
    in_use = w - r;
  }

  /* IPC SECURITY BOUNDARY: Detect malicious consumer index spoofing */
  if (in_use > cap) {
    return 0; /* Abort: Consumer advanced read_idx beyond write_idx */
  }

  available = cap - in_use;
  if (available < count) {
    return 0; /* Not enough space for all-or-nothing write */
  }

  mask = cap - 1;
  physical_w = w & mask;
  first_chunk = cap - physical_w;
  if (first_chunk > count)
    first_chunk = count;

  elem_sz = p->elem_size;
  src = (const uint8_t *)data;

  memcpy(page->payload + (physical_w * elem_sz), src, first_chunk * elem_sz);
  if (first_chunk < count) {
    memcpy(page->payload, src + (first_chunk * elem_sz), (count - first_chunk) * elem_sz);
  }

  /* Commit write (Release ensures memory writes are visible before idx update) */
  RELOCO_IPC_STORE_RELEASE(&page->write_idx, w + count);
  return count;
}

/* =========================================================================
 * CONSUMER API (Reader Context)
 * ========================================================================= */

struct reloco_ipc_consumer {
  struct reloco_ipc_spsc_page *page;
  uint64_t cached_write_idx;
  uint32_t capacity;  /* Trusted, local copy */
  uint32_t elem_size; /* Trusted, local copy */
};

/**
 * @brief Mounts the page for reading.
 */
static inline int reloco_ipc_consumer_init(struct reloco_ipc_consumer *c, struct reloco_ipc_spsc_page *page,
                                           uint32_t elem_size, uint32_t expected_capacity) {
  if (reloco_ipc_validate_mount(page, elem_size, expected_capacity) != 0)
    return -1;
  c->page = page;
  c->cached_write_idx = RELOCO_IPC_LOAD_ACQUIRE(&page->write_idx);
  /* Lock the verified constants into local trusted memory */
  c->capacity = expected_capacity;
  c->elem_size = elem_size;
  return 0;
}

/**
 * @brief Reads up to `max_count` elements into `dest`.
 * @return Number of elements actually read.
 */
static inline uint32_t reloco_ipc_try_read(struct reloco_ipc_consumer *c, void *dest, uint32_t max_count) {
  struct reloco_ipc_spsc_page *page = c->page;
  uint64_t r = RELOCO_IPC_LOAD_RELAXED(&page->read_idx);
  uint64_t w = c->cached_write_idx;
  uint32_t to_read;
  uint32_t cap;
  uint32_t mask;
  uint32_t physical_r;
  uint32_t first_chunk;
  uint32_t elem_sz;
  uint8_t *dst;
  uint64_t available = w - r;

  cap = c->capacity;

  /* Demand-driven cache refresh */
  if (available < max_count) {
    w = RELOCO_IPC_LOAD_ACQUIRE(&page->write_idx);
    c->cached_write_idx = w;
    available = w - r;
  }

  /* IPC SECURITY BOUNDARY: Detect malicious producer index spoofing */
  if (available > cap) {
    return 0; /* Abort: Producer advanced write_idx out of bounds */
  }

  to_read = (uint32_t)(available < max_count ? available : max_count);
  if (to_read == 0) {
    return 0;
  }

  mask = cap - 1;
  physical_r = r & mask;
  first_chunk = cap - physical_r;
  if (first_chunk > to_read)
    first_chunk = to_read;

  elem_sz = c->elem_size;
  dst = (uint8_t *)dest;

  memcpy(dst, page->payload + (physical_r * elem_sz), first_chunk * elem_sz);
  if (first_chunk < to_read) {
    memcpy(dst + (first_chunk * elem_sz), page->payload, (to_read - first_chunk) * elem_sz);
  }

  /* Commit read (Release ensures we are done reading memory before recycling slot) */
  RELOCO_IPC_STORE_RELEASE(&page->read_idx, r + to_read);
  return to_read;
}

#ifdef __cplusplus
}
#endif

#endif /* RELOCO_IPC_RING_H */