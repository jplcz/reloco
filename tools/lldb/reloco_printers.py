# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""LLDB data formatters (pretty printers) for the reloco header-only library.

Mirrors ``tools/gdb/reloco_printers.py``: the summary strings are kept
identical so output reads the same in both debuggers.

Load it in a running LLDB session::

    (lldb) command script import /path/to/reloco/tools/lldb/reloco_printers.py

or automatically, by adding the same line to ``~/.lldbinit`` (or a
project-local ``.lldbinit`` when ``target.load-cwd-lldbinit`` is enabled).

Importing the module calls ``__lldb_init_module``, which registers every
formatter in the ``reloco`` type category and enables it.

Type-erased views (``collection_view``/``container_ref``) are intentionally
not covered, for the same reason as in the GDB printers: their size is only
reachable by calling through a vtable function pointer in the target.
"""

import re

try:
    import lldb
except ImportError:  # pragma: no cover - only importable inside LLDB.
    lldb = None

CATEGORY = "reloco"
# Upper bound on children materialised per value, to keep huge containers
# (or corrupted size fields) from stalling the debugger.
MAX_CHILDREN = 1000
MAX_STRING = 256


# -- helpers ------------------------------------------------------------------


def _m(val, name):
    return val.GetChildMemberWithName(name)


def _u(val):
    return val.GetValueAsUnsigned()


def _template_args(type_name):
    """Splits the top-level template argument list of ``type_name``."""
    start = type_name.find("<")
    end = type_name.rfind(">")
    if start < 0 or end < start:
        return []
    args, depth, cur = [], 0, []
    for ch in type_name[start + 1 : end]:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    args.append("".join(cur).strip())
    return args


def _int_template_arg(val, index):
    """Returns non-type template argument ``index`` of ``val``'s type, or None."""
    for name in (val.GetType().GetName(), val.GetType().GetCanonicalType().GetName()):
        args = _template_args(name or "")
        if index < len(args):
            match = re.match(r"-?\d+", args[index])
            if match:
                return int(match.group(0))
    return None


def _type_template_arg(val, index):
    return val.GetType().GetCanonicalType().GetTemplateArgumentType(index)


def _show(val):
    """Best-effort one-line rendering of ``val`` (summary, else value)."""
    if not val.IsValid():
        return "?"
    text = val.GetSummary()
    if text is None:
        text = val.GetValue()
    return "?" if text is None else text


def _hex(val):
    return "0x%x" % _u(val)


def _cstr(val):
    """Reads a NUL-terminated ``const char*`` member, or returns None."""
    addr = _u(val)
    if addr == 0:
        return None
    err = lldb.SBError()
    text = val.GetProcess().ReadCStringFromMemory(addr, MAX_STRING, err)
    return text if err.Success() else None


def _quote(text):
    out = ['"']
    for ch in text:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def _char_string(val, length):
    """Renders a ``CharT*``/``CharT[N]`` plus length the way LLDB renders C strings."""
    length = int(length)
    if length == 0:
        return '""'
    typ = val.GetType()
    elem = typ.GetArrayElementType() if typ.IsArrayType() else typ.GetPointeeType()
    width = elem.GetByteSize() or 1
    shown = min(length, MAX_STRING)
    err = lldb.SBError()
    data = val.GetPointeeData(0, shown)
    raw = data.ReadRawData(err, 0, data.GetByteSize()) if data.IsValid() else None
    if not err.Success() or raw is None:
        return "<unreadable>"
    codec = {1: "utf-8", 2: "utf-16-le", 4: "utf-32-le"}.get(width, "latin-1")
    text = _quote(raw.decode(codec, "replace"))
    return text + ("..." if length > shown else "")


def _named(name, val):
    """Re-roots ``val`` under ``name`` (to match the GDB printers' child names)."""
    addr = val.GetLoadAddress()
    if addr == lldb.LLDB_INVALID_ADDRESS:
        return val
    return val.CreateValueFromAddress(name, addr, val.GetType())


def _elements(ptr, count, elem_type=None):
    """Children ``[0]..[count-1]`` of a ``T*`` (or type-erased ``void*``, given ``elem_type``)."""
    if elem_type is None:
        elem_type = ptr.GetType().GetPointeeType()
    size = elem_type.GetByteSize()
    base = _u(ptr)
    return [ptr.CreateValueFromAddress("[%d]" % i, base + i * size, elem_type) for i in range(min(int(count), MAX_CHILDREN))]


def _read_atomic(val, depth=0):
    """Best-effort read of a ``std::atomic<std::size_t>`` across libstdc++/libc++."""
    if val.GetNumChildren() == 0:
        return _u(val)
    if depth > 3:
        return None
    for field in ("_M_i", "__a_", "__a_value"):
        inner = _m(val, field)
        if inner.IsValid():
            return _read_atomic(inner, depth + 1)
    # libstdc++ nests the value in a base class (std::__atomic_base<T>).
    return _read_atomic(val.GetChildAtIndex(0), depth + 1)


# -- children providers (each returns a list of SBValue) ----------------------


def _array_children(v):
    data = _m(v, "data_")
    return [data.GetChildAtIndex(i) for i in range(min(data.GetNumChildren(), MAX_CHILDREN))]


def _span_children(v):
    return _elements(_m(v, "m_ptr"), _u(_m(v, "m_size")))


def _vector_children(v):
    # reloco's vector storage keeps a type-erased pointer, so take T from the template.
    return _elements(_m(v, "data_"), _u(_m(v, "size_")), _type_template_arg(v, 0))


def _wrapped_vector_children(v):
    return _vector_children(_m(v, "data_"))


def _boxed_slice_children(v):
    return _elements(_m(v, "ptr_"), _u(_m(v, "size_")), _type_template_arg(v, 0))


def _optional_children(v):
    return [_named("value", _m(v, "value_"))] if _u(_m(v, "has_value_")) else []


def _expected_children(v):
    if _u(_m(v, "m_has_value")):
        value = _m(v, "m_value")
        return [_named("value", value)] if value.IsValid() else []
    return [_named("error", _m(v, "m_error"))]


def _pointee_children(member):
    def children(v):
        ptr = _m(v, member)
        if _u(ptr) == 0:
            return []
        return [_named("get()", ptr.Dereference())]

    return children


def _value_ref_children(v):
    ptr = _m(_m(v, "m_ptr"), "ptr_")
    return [_named("value", ptr.Dereference())] if _u(ptr) else []


def _checked_value_children(v):
    return [] if _u(_m(v, "moved_from_")) else [_named("value", _m(v, "value_"))]


def _value_child(v):
    return [_named("value", _m(v, "value_"))]


def _counted_children(is_weak):
    def children(v):
        ptr, block = _m(v, "ptr_"), _m(v, "block_")
        if is_weak or _u(block) == 0 or _u(ptr) == 0:
            return []
        return [_named("get()", ptr.Dereference())]

    return children


def _cow_children(v):
    if _u(_m(v, "owned_")):
        storage = _m(v, "storage_")
        return [storage.CreateValueFromAddress("value", storage.GetLoadAddress(), _type_template_arg(v, 0))]
    ptr = _m(v, "ptr_")
    return [_named("value", ptr.Dereference())] if _u(ptr) else []


def _binary_heap_children(v):
    return _vector_children(_m(v, "data_"))


def _any_type_id(v):
    vtable = _m(v, "vtable_")
    if _u(vtable) == 0:
        return None
    type_id = vtable.Dereference().GetChildMemberWithName("type_id")
    return type_id if type_id.IsValid() else None


def _any_children(v):
    type_id = _any_type_id(v)
    return [] if type_id is None else [_named("type_id", type_id)]


# -- summaries (each returns a string, or None for "children only") -------------


def _array_summary(v):
    return "reloco::array of length %d" % _m(v, "data_").GetNumChildren()


def _span_summary(v):
    return "reloco::span of length %d" % _u(_m(v, "m_size"))


def _vector_summary(v):
    return "reloco::vector of length %d, capacity %d" % (_u(_m(v, "size_")), _u(_m(v, "cap_")))


def _sso_vector_summary(v):
    inline_cap = _int_template_arg(v, 1)
    is_inline = _u(_m(v, "data_")) == _u(_m(v, "inline_storage_"))
    return "reloco::sso_vector of length %d, capacity %d (%s, inline capacity %s)" % (
        _u(_m(v, "size_")),
        _u(_m(v, "cap_")),
        "inline" if is_inline else "heap",
        "?" if inline_cap is None else inline_cap,
    )


def _inline_vector_summary(v):
    return "reloco::inline_vector of length %d, capacity %s" % (_u(_m(v, "size_")), _int_template_arg(v, 1))


def _flat_summary(kind):
    def summary(v):
        return "reloco::%s of length %d" % (kind, _u(_m(_m(v, "data_"), "size_")))

    return summary


def _inline_flat_summary(kind):
    def summary(v):
        vec = _m(v, "data_")
        return "reloco::%s of length %d, capacity %s" % (kind, _u(_m(vec, "size_")), _int_template_arg(vec, 1))

    return summary


def _sso_flat_summary(kind):
    def summary(v):
        vec = _m(v, "data_")
        return "reloco::%s of length %d, capacity %d" % (kind, _u(_m(vec, "size_")), _u(_m(vec, "cap_")))

    return summary


def _string_summary(v):
    return _char_string(_m(v, "data_"), _u(_m(v, "size_")))


def _optional_summary(v):
    return None if _u(_m(v, "has_value_")) else "reloco::optional [no value]"


def _expected_summary(v):
    if _u(_m(v, "m_has_value")) and not _m(v, "m_value").IsValid():
        return "reloco::expected<void, ...> [value]"
    return None


def _pointer_summary(kind, member):
    def summary(v):
        ptr = _m(v, member)
        if _u(ptr) == 0:
            return "reloco::%s [empty]" % kind
        return "reloco::%s = %s" % (kind, _hex(ptr))

    return summary


def _value_ref_summary(v):
    return "reloco::value_ref -> %s" % _hex(_m(_m(v, "m_ptr"), "ptr_"))


def _checked_value_summary(v):
    return "reloco::checked_value [moved-from]" if _u(_m(v, "moved_from_")) else None


def _cell_summary(v):
    return "reloco::cell"


def _ref_cell_summary(v):
    state = _m(v, "borrow_state_").GetValueAsSigned()
    if state == 0:
        status = "free"
    elif state < 0:
        status = "borrowed mutably"
    else:
        status = "borrowed shared x%d" % state
    return "reloco::ref_cell [%s]" % status


def _wrapper_summary(kind):
    def summary(v):
        return "reloco::%s(%s)" % (kind, _show(_m(v, "value_")))

    return summary


def _shared_summary(kind, atomic):
    def summary(v):
        ptr, block = _m(v, "ptr_"), _m(v, "block_")
        if _u(block) == 0:
            return "reloco::%s [empty]" % kind
        counts = block.Dereference()
        shared, weak = _m(counts, "shared_count_"), _m(counts, "weak_count_")
        if atomic:
            use_count, weak_count = _read_atomic(shared), _read_atomic(weak)
        else:
            use_count, weak_count = _u(shared), _u(weak)
        return "reloco::%s = %s (use_count %s, weak_count %s)" % (
            kind,
            _hex(ptr),
            "?" if use_count is None else use_count,
            "?" if weak_count is None else weak_count,
        )

    return summary


def _binary_heap_summary(v):
    return "reloco::binary_heap of length %d" % _u(_m(_m(v, "data_"), "size_"))


def _cow_summary(v):
    return "reloco::cow [%s]" % ("owned" if _u(_m(v, "owned_")) else "borrowed")


def _boxed_slice_summary(v):
    return "reloco::boxed_slice of length %d" % _u(_m(v, "size_"))


def _guarded_mutex_summary(v):
    return "reloco::guarded_mutex"


def _function_ref_summary(v):
    return "reloco::function_ref bound at %s" % _hex(_m(v, "callback_"))


def _type_id_summary(v):
    if _u(_m(v, "tag_")) == 0:
        return "reloco::type_id [none]"
    name = _cstr(_m(v, "name_"))
    if name is not None:
        return "reloco::type_id = %s" % name
    return "reloco::type_id [unnamed] at %s" % _hex(_m(v, "tag_"))


def _any_summary(v):
    type_id = _any_type_id(v)
    if type_id is None:
        return "reloco::any [empty]"
    name = _cstr(_m(type_id, "name_"))
    if name is not None:
        return "reloco::any holding %s" % name
    return "reloco::any [holds an unnamed type; see RELOCO_TYPE_ID_NAME]"


# -- registration -------------------------------------------------------------

# (display name, type regex, summary function or None, children function or None)
_SPECS = [
    ("array", r"^reloco::array<.*>$", _array_summary, _array_children),
    ("span", r"^reloco::span<.*>$", _span_summary, _span_children),
    ("vector", r"^reloco::vector<.*>$", _vector_summary, _vector_children),
    ("inline_vector", r"^reloco::inline_vector<.*>$", _inline_vector_summary, _vector_children),
    ("sso_vector", r"^reloco::sso_vector<.*>$", _sso_vector_summary, _vector_children),
    ("flat_set", r"^reloco::flat_set<.*>$", _flat_summary("flat_set"), _wrapped_vector_children),
    ("flat_map", r"^reloco::flat_map<.*>$", _flat_summary("flat_map"), _wrapped_vector_children),
    ("inline_flat_set", r"^reloco::inline_flat_set<.*>$", _inline_flat_summary("inline_flat_set"), _wrapped_vector_children),
    ("inline_flat_map", r"^reloco::inline_flat_map<.*>$", _inline_flat_summary("inline_flat_map"), _wrapped_vector_children),
    ("sso_flat_set", r"^reloco::sso_flat_set<.*>$", _sso_flat_summary("sso_flat_set"), _wrapped_vector_children),
    ("sso_flat_map", r"^reloco::sso_flat_map<.*>$", _sso_flat_summary("sso_flat_map"), _wrapped_vector_children),
    ("basic_string", r"^reloco::basic_string<.*>$", _string_summary, None),
    ("basic_string_view", r"^reloco::basic_string_view<.*>$", _string_summary, None),
    ("basic_inline_string", r"^reloco::basic_inline_string<.*>$", _string_summary, None),
    ("basic_sso_string", r"^reloco::basic_sso_string<.*>$", _string_summary, None),
    ("optional", r"^reloco::optional<.*>$", _optional_summary, _optional_children),
    ("expected", r"^reloco::expected<.*>$", _expected_summary, _expected_children),
    ("unique_ptr", r"^reloco::unique_ptr<.*>$", _pointer_summary("unique_ptr", "ptr_"), _pointee_children("ptr_")),
    ("value_ptr", r"^reloco::value_ptr<.*>$", _pointer_summary("value_ptr", "ptr_"), _pointee_children("ptr_")),
    ("value_ref", r"^reloco::value_ref<.*>$", _value_ref_summary, _value_ref_children),
    ("checked_value", r"^reloco::checked_value<.*>$", _checked_value_summary, _checked_value_children),
    ("cell", r"^reloco::cell<.*>$", _cell_summary, _value_child),
    ("ref_cell", r"^reloco::ref_cell<.*>$", _ref_cell_summary, _value_child),
    ("non_zero", r"^reloco::non_zero<.*>$", _wrapper_summary("non_zero"), None),
    ("wrapping", r"^reloco::wrapping<.*>$", _wrapper_summary("wrapping"), None),
    ("saturating", r"^reloco::saturating<.*>$", _wrapper_summary("saturating"), None),
    ("checked", r"^reloco::checked<.*>$", _wrapper_summary("checked"), None),
    ("shared_ptr", r"^reloco::shared_ptr<.*>$", _shared_summary("shared_ptr", True), _counted_children(False)),
    ("weak_ptr", r"^reloco::weak_ptr<.*>$", _shared_summary("weak_ptr", True), _counted_children(True)),
    ("rc", r"^reloco::rc<.*>$", _shared_summary("rc", False), _counted_children(False)),
    ("weak_rc", r"^reloco::weak_rc<.*>$", _shared_summary("weak_rc", False), _counted_children(True)),
    ("binary_heap", r"^reloco::binary_heap<.*>$", _binary_heap_summary, _binary_heap_children),
    ("cow", r"^reloco::cow<.*>$", _cow_summary, _cow_children),
    ("boxed_slice", r"^reloco::boxed_slice<.*>$", _boxed_slice_summary, _boxed_slice_children),
    ("guarded_mutex", r"^reloco::guarded_mutex<.*>$", _guarded_mutex_summary, _value_child),
    ("function_ref", r"^reloco::function_ref<.*>$", _function_ref_summary, None),
    ("type_id", r"^reloco::type_id$", _type_id_summary, None),
    ("any", r"^reloco::any$", _any_summary, _any_children),
]


def _make_summary(fn):
    def summary(valobj, internal_dict):
        try:
            # LLDB prints a literal "None" for a None summary; "" means "no summary text".
            return fn(valobj.GetNonSyntheticValue()) or ""
        except Exception as exc:  # A broken formatter must never break the debugger.
            return "<reloco formatter error: %s>" % exc

    return summary


def _make_synthetic(fn):
    class Synthetic:
        def __init__(self, valobj, internal_dict):
            self.valobj = valobj.GetNonSyntheticValue()
            self.children = []

        def update(self):
            try:
                self.children = fn(self.valobj)
            except Exception:
                self.children = []
            return False

        def num_children(self, max_children=None):
            return len(self.children)

        def get_child_at_index(self, index):
            return self.children[index] if 0 <= index < len(self.children) else None

        def get_child_index(self, name):
            for i, child in enumerate(self.children):
                if child.GetName() == name:
                    return i
            return -1

        def has_children(self):
            return len(self.children) > 0

    return Synthetic


def register_reloco_printers(debugger):
    """Registers the reloco formatters with ``debugger`` (idempotent)."""
    module = __name__
    for name, regex, summary_fn, children_fn in _SPECS:
        if summary_fn is not None:
            attr = "_summary_" + name
            globals()[attr] = _make_summary(summary_fn)
            debugger.HandleCommand(
                'type summary add --category %s --regex "%s" --python-function %s.%s%s'
                % (CATEGORY, regex, module, attr, " --expand" if children_fn else "")
            )
        if children_fn is not None:
            attr = "_Synthetic_" + name
            globals()[attr] = _make_synthetic(children_fn)
            debugger.HandleCommand(
                'type synthetic add --category %s --regex "%s" --python-class %s.%s' % (CATEGORY, regex, module, attr)
            )
    debugger.HandleCommand("type category enable %s" % CATEGORY)


def __lldb_init_module(debugger, internal_dict):
    register_reloco_printers(debugger)
