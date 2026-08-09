"""
GDB pretty-printers for Absolute runtime layouts.

Load:
  (gdb) source /path/to/absolute_gdb.py

Array descriptor (1D):  [data*, owner*, i64 length]
Managed handle (i64):   low32 = slot, high32 = generation
Task:                   opaque pointer
"""

import gdb


class AbsoluteArrayPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            addr = int(self.val.address) if self.val.address else int(self.val)
        except Exception:
            return str(self.val)
        try:
            void_ptr = gdb.lookup_type('void').pointer()
            i64 = gdb.lookup_type('long long')
            base = gdb.Value(addr).cast(void_ptr.pointer())
            data = base.dereference().cast(void_ptr)
            owner = (base + 1).dereference().cast(void_ptr)
            length = (base + 2).cast(i64.pointer()).dereference()
            return f'{{data={data} owner={owner} len={int(length)}}}'
        except Exception as exc:
            return f'<array decode error: {exc}>'

    def children(self):
        return []


class AbsoluteManagedPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            raw = int(self.val)
        except Exception:
            return str(self.val)
        if raw == 0:
            return 'null managed'
        slot = raw & 0xFFFFFFFF
        gen = (raw >> 32) & 0xFFFFFFFF
        return f'managed slot={slot} gen={gen}'


class AbsoluteTaskPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            ptr = int(self.val)
        except Exception:
            return str(self.val)
        return 'null task' if ptr == 0 else f'task 0x{ptr:x}'


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter('absolute')
    pp.add_printer('array', r'^absolute\.array\..*', AbsoluteArrayPrinter)
    pp.add_printer('managed', r'^AbsoluteManagedHandle$', AbsoluteManagedPrinter)
    pp.add_printer('task', r'^AbsoluteTaskHandle$', AbsoluteTaskPrinter)
    return pp


def register():
    gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer(), replace=True)


register()
