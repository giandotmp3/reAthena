"""YAML reading that mimics what the legacy loader (rapidyaml) hands to parseBodyNode.

Every plain scalar is kept as its source text (a Python str): the legacy code converts text to numbers/bools itself
(asUInt16, asBool, ...), so 'Yes', '0x10' or '1e3' never get YAML 1.1 meanings. Only an unquoted empty / ~ / null scalar
becomes NULL (ryml val_is_null). Mappings become dicts (first duplicate key wins, like ryml's node[name]),
sequences become lists.
"""
import re
import yaml


class _Null(object):
    __slots__ = ()

    def __repr__(self):
        return "NULL"

    def __bool__(self):
        return False


NULL = _Null()


class Map(dict):
    """A YAML mapping. Lookups by key give the FIRST occurrence (ryml node[name]); `pairs` keeps every child in
    document order including duplicate keys, because the legacy `for (child : node)` loops visit duplicates."""
    __slots__ = ("pairs",)


class YamlSyntaxError(Exception):
    pass


class Reject(Exception):
    """The legacy parseBodyNode would 'return 0' for this entry (earlier field patches stay applied)."""


def _conv(node, dups):
    if isinstance(node, yaml.ScalarNode):
        v = node.value
        if node.style is None and v in ("", "~", "null", "Null", "NULL"):
            return NULL
        return v
    if isinstance(node, yaml.SequenceNode):
        return [_conv(n, dups) for n in node.value]
    if isinstance(node, yaml.MappingNode):
        d = Map()
        d.pairs = []
        for k, v in node.value:
            key = k.value if isinstance(k, yaml.ScalarNode) else repr(k)
            val = _conv(v, dups)
            d.pairs.append((key, val))
            if key in d:
                dups.append(key)
                continue
            d[key] = val
        return d
    raise YamlSyntaxError("unsupported node " + repr(node))


def load_text(text, name="<string>"):
    """Parse YAML text. Returns (root, duplicate_keys). root is None for an empty document."""
    try:
        node = yaml.compose(text, Loader=yaml.SafeLoader)
    except yaml.YAMLError as e:
        raise YamlSyntaxError("%s: %s" % (name, str(e).replace("\n", " ")))
    dups = []
    if node is None:
        return None, dups
    return _conv(node, dups), dups


def load_file(path):
    with open(path, "rb") as f:
        raw = f.read()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        text = raw.decode("latin-1")
    return load_text(text, path)


# ---- typed accessors: a failed accessor is the legacy "return 0" (Reject) ---------------------------------

_INT = re.compile(r"^([+-]?)(0[xX][0-9a-fA-F]+|0[oO][0-7]+|0[bB][01]+|[0-9]+)$")


def has(node, name):
    return isinstance(node, dict) and name in node


def is_null_value(v):
    return v is NULL


class Reader(object):
    """Carries the diagnostics sink and the current file (the legacy invalidWarning)."""

    def __init__(self, warn=None):
        self.warnings = []
        self.file = ""
        self._warn = warn

    def warn(self, msg):
        self.warnings.append((self.file, msg))

    # -- accessors --
    def s(self, node, name):
        if not has(node, name):
            self.warn('Missing node "%s".' % name)
            raise Reject(name)
        v = node[name]
        if v is NULL or isinstance(v, (dict, list)):
            self.warn('Node "%s" is missing a value.' % name)
            raise Reject(name)
        return v

    def b(self, node, name):
        v = self.s(node, name)
        t = v.lower()
        if t == "true":
            return True
        if t == "false":
            return False
        self.warn('Unknown boolean value: "%s".' % v)
        raise Reject(name)

    def i(self, node, name, lo, hi):
        v = self.s(node, name)
        m = _INT.match(v.strip())
        if not m:
            self.warn('Node "%s" cannot be parsed as an integer: %r.' % (name, v))
            raise Reject(name)
        sign, body = m.groups()
        base = 10
        if body[:2] in ("0x", "0X"):
            base = 16
            body = body[2:]
        elif body[:2] in ("0o", "0O"):
            base = 8
            body = body[2:]
        elif body[:2] in ("0b", "0B"):
            base = 2
            body = body[2:]
        n = int(body, base)
        if sign == "-":
            n = -n
        if n < lo or n > hi:
            self.warn('Node "%s" value %d out of range %d..%d.' % (name, n, lo, hi))
            raise Reject(name)
        return n

    def u8(self, node, name):
        return self.i(node, name, 0, 0xFF)

    def u16(self, node, name):
        return self.i(node, name, 0, 0xFFFF)

    def u32(self, node, name):
        return self.i(node, name, 0, 0xFFFFFFFF)

    def u64(self, node, name):
        return self.i(node, name, 0, 0xFFFFFFFFFFFFFFFF)

    def i16(self, node, name):
        return self.i(node, name, -0x8000, 0x7FFF)

    def i32(self, node, name):
        return self.i(node, name, -0x80000000, 0x7FFFFFFF)

    def i64(self, node, name):
        return self.i(node, name, -(1 << 63), (1 << 63) - 1)

    def u16_rate(self, node, name, maximum=10000):
        v = self.u16(node, name)
        if v > maximum:
            self.warn('Node "%s" with value %d exceeds maximum of %d.' % (name, v, maximum))
            raise Reject(name)
        if v == 0:
            self.warn('Node "%s" needs to be at least 1.' % name)
            raise Reject(name)
        return v

    def need(self, node, names):
        ok = True
        for n in names:
            if not has(node, n):
                self.warn('Missing node "%s".' % n)
                ok = False
        if not ok:
            self.warn("At least one mandatory node did not exist.")
            raise Reject("mandatory")

    def items(self, node):
        """children of a mapping in order (key, value). A sequence/scalar yields nothing (legacy would misread)."""
        if isinstance(node, Map):
            return list(node.pairs)
        if isinstance(node, dict):
            return list(node.items())
        return []
