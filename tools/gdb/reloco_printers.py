# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""GDB pretty printers for the reloco header-only library.

This module can be used in three interchangeable ways:

  1. Sourced directly in a running GDB session::

         (gdb) source /path/to/reloco/tools/gdb/reloco_printers.py

  2. Auto-loaded for a specific binary by placing a ``<binary>-gdb.py``
     next to it (or anywhere on GDB's auto-load path) containing::

         import sys
         sys.path.insert(0, "/path/to/reloco/tools/gdb")
         import reloco_printers
         reloco_printers.register_reloco_printers()

  3. Embedded directly into the binary's ``.debug_gdb_scripts`` section via
     ``include/reloco/gdb_printers.hpp`` (see that header for details); GDB
     then loads and runs this exact source automatically when the binary is
     loaded, with no external file or path configuration required.

In all three cases the printers are registered against ``objfile`` (or the
global printer list, when there is no current object file) the moment this
module is loaded -- see the bottom of the file.
"""

try:
    import gdb
    import gdb.printing
except ImportError:  # pragma: no cover - only importable inside GDB.
    gdb = None


def _read_atomic(val):
    """Best-effort read of a ``std::atomic<std::size_t>`` across libstdc++/libc++."""
    try:
        return int(val)
    except (gdb.error, TypeError):
        pass
    for field in ("_M_i", "__a_"):
        try:
            inner = val[field]
        except gdb.error:
            continue
        try:
            return int(inner)
        except (gdb.error, TypeError):
            try:
                return int(inner["__a_value"])
            except gdb.error:
                continue
    return None


def _char_pointer_string(ptr_val, length):
    """Renders a ``CharT*`` + length pair the way GDB renders C strings."""
    if length == 0:
        return '""'
    try:
        return ptr_val.lazy_string(length=int(length))
    except gdb.error:
        return "<unreadable>"


class RelocoArrayPrinter:
    """Pretty printer for `reloco::array<T, N>`."""

    def __init__(self, val):
        self.val = val
        self.n = int(val.type.template_argument(1))

    def to_string(self):
        return "reloco::array of length %d" % self.n

    def children(self):
        data = self.val["data_"]
        for i in range(self.n):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoSpanPrinter:
    """Pretty printer for `reloco::span<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["m_size"])
        return "reloco::span of length %d" % size

    def children(self):
        size = int(self.val["m_size"])
        ptr = self.val["m_ptr"]
        for i in range(size):
            yield (str(i), ptr[i])

    def display_hint(self):
        return "array"


class RelocoVectorPrinter:
    """Pretty printer for `reloco::vector<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["size_"])
        cap = int(self.val["cap_"])
        return "reloco::vector of length %d, capacity %d" % (size, cap)

    def children(self):
        size = int(self.val["size_"])
        data = self.val["data_"]
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoSsoVectorPrinter:
    """Pretty printer for `reloco::sso_vector<T, InlineCapacity>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["size_"])
        cap = int(self.val["cap_"])
        inline_cap = int(self.val.type.template_argument(1))
        is_inline = int(self.val["data_"]) == int(self.val["inline_storage_"].address)
        return "reloco::sso_vector of length %d, capacity %d (%s, inline capacity %d)" % (
            size,
            cap,
            "inline" if is_inline else "heap",
            inline_cap,
        )

    def children(self):
        size = int(self.val["size_"])
        data = self.val["data_"]
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoInlineVectorPrinter:
    """Pretty printer for `reloco::inline_vector<T, Capacity>`."""

    def __init__(self, val):
        self.val = val
        self.elem_type = val.type.template_argument(0)

    def to_string(self):
        size = int(self.val["size_"])
        cap = int(self.val.type.template_argument(1))
        return "reloco::inline_vector of length %d, capacity %d" % (size, cap)

    def children(self):
        size = int(self.val["size_"])
        data = self.val["storage_"].address.cast(self.elem_type.pointer())
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoFlatSetPrinter:
    """Pretty printer for `reloco::flat_set<T, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["data_"]["size_"])
        return "reloco::flat_set of length %d" % size

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        data = vec["data_"]
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoFlatMapPrinter:
    """Pretty printer for `reloco::flat_map<Key, Mapped, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["data_"]["size_"])
        return "reloco::flat_map of length %d" % size

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        data = vec["data_"]
        for i in range(size):
            entry = data[i]
            yield ("key%d" % i, entry["first"])
            yield ("value%d" % i, entry["second"])

    def display_hint(self):
        return "map"


class RelocoInlineFlatSetPrinter:
    """Pretty printer for `reloco::inline_flat_set<T, Capacity, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        cap = int(vec.type.template_argument(1))
        return "reloco::inline_flat_set of length %d, capacity %d" % (size, cap)

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        elem_type = vec.type.template_argument(0)
        data = vec["storage_"].address.cast(elem_type.pointer())
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoInlineFlatMapPrinter:
    """Pretty printer for `reloco::inline_flat_map<Key, Mapped, Capacity, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        cap = int(vec.type.template_argument(1))
        return "reloco::inline_flat_map of length %d, capacity %d" % (size, cap)

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        elem_type = vec.type.template_argument(0)
        data = vec["storage_"].address.cast(elem_type.pointer())
        for i in range(size):
            entry = data[i]
            yield ("key%d" % i, entry["first"])
            yield ("value%d" % i, entry["second"])

    def display_hint(self):
        return "map"


class RelocoSsoFlatSetPrinter:
    """Pretty printer for `reloco::sso_flat_set<T, InlineCapacity, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        cap = int(vec["cap_"])
        return "reloco::sso_flat_set of length %d, capacity %d" % (size, cap)

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        data = vec["data_"]
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoSsoFlatMapPrinter:
    """Pretty printer for `reloco::sso_flat_map<Key, Mapped, InlineCapacity, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        cap = int(vec["cap_"])
        return "reloco::sso_flat_map of length %d, capacity %d" % (size, cap)

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        data = vec["data_"]
        for i in range(size):
            entry = data[i]
            yield ("key%d" % i, entry["first"])
            yield ("value%d" % i, entry["second"])

    def display_hint(self):
        return "map"


class RelocoStringPrinter:
    """Pretty printer for `reloco::basic_string<CharT, TraitsT>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = self.val["size_"]
        return _char_pointer_string(self.val["data_"], size)

    def display_hint(self):
        return "string"


class RelocoStringViewPrinter:
    """Pretty printer for `reloco::basic_string_view<CharT, TraitsT>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = self.val["size_"]
        return _char_pointer_string(self.val["data_"], size)

    def display_hint(self):
        return "string"


class RelocoInlineStringPrinter:
    """Pretty printer for `reloco::basic_inline_string<Capacity, CharT, TraitsT>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = self.val["size_"]
        return _char_pointer_string(self.val["data_"], size)

    def display_hint(self):
        return "string"


class RelocoSsoStringPrinter:
    """Pretty printer for `reloco::basic_sso_string<CharT, TraitsT>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = self.val["size_"]
        return _char_pointer_string(self.val["data_"], size)

    def display_hint(self):
        return "string"


class RelocoOptionalPrinter:
    """Pretty printer for `reloco::optional<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if not bool(self.val["has_value_"]):
            return "reloco::optional [no value]"
        return None

    def children(self):
        if bool(self.val["has_value_"]):
            yield ("value", self.val["value_"])


class RelocoExpectedPrinter:
    """Pretty printer for `reloco::expected<T, E>` (and its `void` specialization)."""

    def __init__(self, val):
        self.val = val

    def _has_value(self):
        return bool(self.val["m_has_value"])

    def to_string(self):
        if self._has_value():
            try:
                self.val["m_value"]
            except gdb.error:
                return "reloco::expected<void, ...> [value]"
            return None
        return None

    def children(self):
        if self._has_value():
            try:
                yield ("value", self.val["m_value"])
            except gdb.error:
                pass
        else:
            yield ("error", self.val["m_error"])


class RelocoUniquePtrPrinter:
    """Pretty printer for `reloco::unique_ptr<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["ptr_"]
        if int(ptr) == 0:
            return "reloco::unique_ptr [empty]"
        return "reloco::unique_ptr = %s" % str(ptr)

    def children(self):
        ptr = self.val["ptr_"]
        if int(ptr) != 0:
            yield ("get()", ptr.dereference())


class RelocoValuePtrPrinter:
    """Pretty printer for `reloco::value_ptr<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["ptr_"]
        if int(ptr) == 0:
            return "reloco::value_ptr [empty]"
        return "reloco::value_ptr = %s" % str(ptr)

    def children(self):
        ptr = self.val["ptr_"]
        if int(ptr) != 0:
            yield ("get()", ptr.dereference())


class RelocoValueRefPrinter:
    """Pretty printer for `reloco::value_ref<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["m_ptr"]["ptr_"]
        return "reloco::value_ref -> %s" % str(ptr)

    def children(self):
        ptr = self.val["m_ptr"]["ptr_"]
        if int(ptr) != 0:
            yield ("value", ptr.dereference())


class RelocoCheckedValuePrinter:
    """Pretty printer for `reloco::checked_value<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if bool(self.val["moved_from_"]):
            return "reloco::checked_value [moved-from]"
        return None

    def children(self):
        if not bool(self.val["moved_from_"]):
            yield ("value", self.val["value_"])


class RelocoCellPrinter:
    """Pretty printer for `reloco::cell<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "reloco::cell"

    def children(self):
        yield ("value", self.val["value_"])


class RelocoRefCellPrinter:
    """Pretty printer for `reloco::ref_cell<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        state = int(self.val["borrow_state_"])
        if state == 0:
            status = "free"
        elif state < 0:
            status = "borrowed mutably"
        else:
            status = "borrowed shared x%d" % state
        return "reloco::ref_cell [%s]" % status

    def children(self):
        yield ("value", self.val["value_"])


class RelocoNonZeroPrinter:
    """Pretty printer for `reloco::non_zero<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "reloco::non_zero(%s)" % str(self.val["value_"])


class _RelocoSharedPtrPrinterBase:
    """Shared implementation for `reloco::shared_ptr<T>`/`reloco::weak_ptr<T>`."""

    is_weak = False
    kind = "shared_ptr"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["ptr_"]
        block = self.val["block_"]
        if int(block) == 0:
            return "reloco::%s [empty]" % self.kind
        shared = _read_atomic(block.dereference()["shared_count_"])
        weak = _read_atomic(block.dereference()["weak_count_"])
        return "reloco::%s = %s (use_count %s, weak_count %s)" % (
            self.kind,
            str(ptr),
            "?" if shared is None else shared,
            "?" if weak is None else weak,
        )

    def children(self):
        ptr = self.val["ptr_"]
        block = self.val["block_"]
        if int(block) != 0 and int(ptr) != 0 and not self.is_weak:
            yield ("get()", ptr.dereference())


class RelocoSharedPtrPrinter(_RelocoSharedPtrPrinterBase):
    """Pretty printer for `reloco::shared_ptr<T>`."""

    is_weak = False
    kind = "shared_ptr"


class RelocoWeakPtrPrinter(_RelocoSharedPtrPrinterBase):
    """Pretty printer for `reloco::weak_ptr<T>`."""

    is_weak = True
    kind = "weak_ptr"


class _RelocoRcPrinterBase:
    """Shared implementation for `reloco::rc<T>`/`reloco::weak_rc<T>`.

    Unlike `shared_ptr`/`weak_ptr`, `rc`/`weak_rc` are single-threaded, so
    `shared_count_`/`weak_count_` are plain (non-atomic) `std::size_t`
    fields, read directly rather than through `_read_atomic`.
    """

    is_weak = False
    kind = "rc"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        ptr = self.val["ptr_"]
        block = self.val["block_"]
        if int(block) == 0:
            return "reloco::%s [empty]" % self.kind
        shared = int(block.dereference()["shared_count_"])
        weak = int(block.dereference()["weak_count_"])
        return "reloco::%s = %s (use_count %d, weak_count %d)" % (self.kind, str(ptr), shared, weak)

    def children(self):
        ptr = self.val["ptr_"]
        block = self.val["block_"]
        if int(block) != 0 and int(ptr) != 0 and not self.is_weak:
            yield ("get()", ptr.dereference())


class RelocoRcPrinter(_RelocoRcPrinterBase):
    """Pretty printer for `reloco::rc<T>`."""

    is_weak = False
    kind = "rc"


class RelocoWeakRcPrinter(_RelocoRcPrinterBase):
    """Pretty printer for `reloco::weak_rc<T>`."""

    is_weak = True
    kind = "weak_rc"


class RelocoBinaryHeapPrinter:
    """Pretty printer for `reloco::binary_heap<T, Compare>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        return "reloco::binary_heap of length %d" % size

    def children(self):
        vec = self.val["data_"]
        size = int(vec["size_"])
        data = vec["data_"]
        for i in range(size):
            yield (str(i), data[i])

    def display_hint(self):
        return "array"


class RelocoCowPrinter:
    """Pretty printer for `reloco::cow<T>`."""

    def __init__(self, val):
        self.val = val
        self.elem_type = val.type.template_argument(0)

    def to_string(self):
        return "reloco::cow [%s]" % ("owned" if bool(self.val["owned_"]) else "borrowed")

    def children(self):
        if bool(self.val["owned_"]):
            ptr = self.val["storage_"].address.cast(self.elem_type.pointer())
            yield ("value", ptr.dereference())
        else:
            ptr = self.val["ptr_"]
            if int(ptr) != 0:
                yield ("value", ptr.dereference())


class RelocoBoxedSlicePrinter:
    """Pretty printer for `reloco::boxed_slice<T>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val["size_"])
        return "reloco::boxed_slice of length %d" % size

    def children(self):
        size = int(self.val["size_"])
        ptr = self.val["ptr_"]
        for i in range(size):
            yield (str(i), ptr[i])

    def display_hint(self):
        return "array"


class RelocoGuardedMutexPrinter:
    """Pretty printer for `reloco::guarded_mutex<T, MutexT>`.

    Shows the guarded value unconditionally: the underlying OS mutex's
    lock state can't be introspected portably (e.g. `std::mutex` is an
    opaque `pthread_mutex_t`), so this mirrors the raw struct dump for the
    value while still hiding the mutex's own internals.
    """

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "reloco::guarded_mutex"

    def children(self):
        yield ("value", self.val["value_"])


class RelocoFunctionRefPrinter:
    """Pretty printer for `reloco::function_ref<Signature>`."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        callback = self.val["callback_"]
        return "reloco::function_ref bound at %s" % str(callback)


def _build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("reloco")
    pp.add_printer("reloco::array", r"^reloco::array<.*>$", RelocoArrayPrinter)
    pp.add_printer("reloco::span", r"^reloco::span<.*>$", RelocoSpanPrinter)
    pp.add_printer("reloco::vector", r"^reloco::vector<.*>$", RelocoVectorPrinter)
    pp.add_printer("reloco::inline_vector", r"^reloco::inline_vector<.*>$", RelocoInlineVectorPrinter)
    pp.add_printer("reloco::flat_set", r"^reloco::flat_set<.*>$", RelocoFlatSetPrinter)
    pp.add_printer("reloco::flat_map", r"^reloco::flat_map<.*>$", RelocoFlatMapPrinter)
    pp.add_printer("reloco::inline_flat_set", r"^reloco::inline_flat_set<.*>$", RelocoInlineFlatSetPrinter)
    pp.add_printer("reloco::inline_flat_map", r"^reloco::inline_flat_map<.*>$", RelocoInlineFlatMapPrinter)
    pp.add_printer("reloco::sso_vector", r"^reloco::sso_vector<.*>$", RelocoSsoVectorPrinter)
    pp.add_printer("reloco::sso_flat_set", r"^reloco::sso_flat_set<.*>$", RelocoSsoFlatSetPrinter)
    pp.add_printer("reloco::sso_flat_map", r"^reloco::sso_flat_map<.*>$", RelocoSsoFlatMapPrinter)
    pp.add_printer("reloco::basic_string", r"^reloco::basic_string<.*>$", RelocoStringPrinter)
    pp.add_printer("reloco::basic_string_view", r"^reloco::basic_string_view<.*>$", RelocoStringViewPrinter)
    pp.add_printer("reloco::basic_inline_string", r"^reloco::basic_inline_string<.*>$", RelocoInlineStringPrinter)
    pp.add_printer("reloco::basic_sso_string", r"^reloco::basic_sso_string<.*>$", RelocoSsoStringPrinter)
    pp.add_printer("reloco::optional", r"^reloco::optional<.*>$", RelocoOptionalPrinter)
    pp.add_printer("reloco::expected", r"^reloco::expected<.*>$", RelocoExpectedPrinter)
    pp.add_printer("reloco::unique_ptr", r"^reloco::unique_ptr<.*>$", RelocoUniquePtrPrinter)
    pp.add_printer("reloco::value_ptr", r"^reloco::value_ptr<.*>$", RelocoValuePtrPrinter)
    pp.add_printer("reloco::value_ref", r"^reloco::value_ref<.*>$", RelocoValueRefPrinter)
    pp.add_printer("reloco::checked_value", r"^reloco::checked_value<.*>$", RelocoCheckedValuePrinter)
    pp.add_printer("reloco::cell", r"^reloco::cell<.*>$", RelocoCellPrinter)
    pp.add_printer("reloco::ref_cell", r"^reloco::ref_cell<.*>$", RelocoRefCellPrinter)
    pp.add_printer("reloco::non_zero", r"^reloco::non_zero<.*>$", RelocoNonZeroPrinter)
    pp.add_printer("reloco::shared_ptr", r"^reloco::shared_ptr<.*>$", RelocoSharedPtrPrinter)
    pp.add_printer("reloco::weak_ptr", r"^reloco::weak_ptr<.*>$", RelocoWeakPtrPrinter)
    pp.add_printer("reloco::rc", r"^reloco::rc<.*>$", RelocoRcPrinter)
    pp.add_printer("reloco::weak_rc", r"^reloco::weak_rc<.*>$", RelocoWeakRcPrinter)
    pp.add_printer("reloco::binary_heap", r"^reloco::binary_heap<.*>$", RelocoBinaryHeapPrinter)
    pp.add_printer("reloco::cow", r"^reloco::cow<.*>$", RelocoCowPrinter)
    pp.add_printer("reloco::boxed_slice", r"^reloco::boxed_slice<.*>$", RelocoBoxedSlicePrinter)
    pp.add_printer("reloco::guarded_mutex", r"^reloco::guarded_mutex<.*>$", RelocoGuardedMutexPrinter)
    pp.add_printer("reloco::function_ref", r"^reloco::function_ref<.*>$", RelocoFunctionRefPrinter)
    return pp


def register_reloco_printers(objfile=None):
    """Registers the reloco pretty printers with GDB.

    Idempotent: re-registering (e.g. because this module was sourced twice,
    or embedded in several object files) simply replaces the previous
    registration under the same name.
    """
    if gdb is None:
        return

    target = objfile if objfile is not None else gdb
    gdb.printing.register_pretty_printer(target, _build_pretty_printer(), replace=True)


if gdb is not None:
    register_reloco_printers(gdb.current_objfile())
