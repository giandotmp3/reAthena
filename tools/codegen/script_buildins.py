#!/usr/bin/env python3
"""Generate the script buildin inventory (doc/script_buildins.tsv) from the legacy VM.

The map server declares every script command once, in `buildin_func[]` in src/map/script.cpp:

    BUILDIN_DEF(name, "args"),                          // command `name`, C function buildin_name
    BUILDIN_DEF2(func, "alias", "args"),                // command `alias`, C function buildin_func
    BUILDIN_DEF_DEPRECATED(name, "args", "date"),
    BUILDIN_DEF2_DEPRECATED(func, "alias", "args", "date"),

plus whatever src/custom/script_def.inc adds. This script reads that table and writes one row per
script command: its name, the C function behind it, the argument signature string (the format
add_buildin_func() parses: i=int, s=string, v=any value, r=reference, l=label, ?=optional,
*=repeat the rest), the deprecation date (or -), the preprocessor condition the entry sits under
(or -), and whether the C function body takes the attached player (script_rid2sd and friends),
which says whether the command can do anything useful without a player.

The inventory is the API surface of ScriptHost's VM (REIMAGINED_ARCHITECTURE.md §3.2 step 2) and
the denominator of the buildin corpus coverage (tools/ci/script_corpus.sh). CI fails when a
buildin is added, removed or changed without regenerating it:

    python3 tools/codegen/script_buildins.py           # regenerate
    python3 tools/codegen/script_buildins.py --check   # exit 1 if out of date
    python3 tools/codegen/script_buildins.py --summary # print the counts
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT_CPP = ROOT / "src/map/script.cpp"
SRC_DIR = ROOT / "src"  # `#include <custom/...>` is relative to src/
OUTPUT = ROOT / "doc/script_buildins.tsv"
GENERATOR = "tools/codegen/script_buildins.py"

TABLE_START = re.compile(r"^struct\s+script_function\s+buildin_func\s*\[\s*\]\s*=\s*\{")
DEF_RE = re.compile(r"\bBUILDIN_DEF(2)?(_DEPRECATED)?\s*\(")
FUNC_RE = re.compile(r"^BUILDIN_FUNC\s*\(\s*(\w+)\s*\)", re.M)
# Accessors that resolve the attached (or a named) player inside a buildin.
PLAYER_RE = re.compile(r"\bscript_(rid2sd|charid2sd|accid2sd|nick2sd|mapid2sd)\s*\(")


@dataclass(frozen=True)
class Buildin:
    name: str
    function: str
    args: str
    deprecated: str
    condition: str
    player: bool
    effective: bool = True


class GenError(Exception):
    pass


def strip_comments(text: str) -> str:
    """Removes // and /* */ comments, keeping string literals and line breaks."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif c == "'":
            # character literal ('"', '\'', 'x'); keeps a quote from opening a string
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise GenError("unterminated /* comment")
            out.append("\n" * text.count("\n", i, j))
            i = j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_args(inner: str) -> list[str]:
    args, cur, i = [], [], 0
    while i < len(inner):
        c = inner[i]
        if c == '"':
            j = i + 1
            while inner[j] != '"':
                j += 2 if inner[j] == "\\" else 1
            cur.append(inner[i : j + 1])
            i = j + 1
            continue
        if c == ",":
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    args.append("".join(cur).strip())
    return args


def unquote(token: str, where: str) -> str:
    if len(token) < 2 or token[0] != '"' or token[-1] != '"':
        raise GenError(f"{where}: expected a string literal, got {token!r}")
    return token[1:-1]


def table_lines(path: Path, lines: list[str], conditions: list[str]) -> list[tuple[str, str]]:
    """Returns (logical line, condition) pairs for the table body, following #include <custom/...>."""
    out = []
    for raw in lines:
        s = raw.strip()
        if s.startswith("#"):
            directive = s[1:].strip()
            if directive.startswith("include"):
                m = re.match(r"include\s*[<\"]([^>\"]+)[>\"]", directive)
                if not m:
                    raise GenError(f"{path}: cannot parse {s!r}")
                inc = SRC_DIR / m.group(1)
                inc_lines = strip_comments(inc.read_text(encoding="utf-8", errors="replace")).splitlines()
                out += table_lines(inc, inc_lines, conditions)
            elif directive.startswith(("ifdef", "ifndef", "if ")):
                conditions.append(directive)
            elif directive.startswith("else"):
                if not conditions:
                    raise GenError(f"{path}: #else without #if")
                last = conditions.pop()
                conditions.append(f"!({last})")
            elif directive.startswith("endif"):
                if not conditions:
                    raise GenError(f"{path}: #endif without #if")
                conditions.pop()
            else:
                raise GenError(f"{path}: unexpected directive {s!r} inside buildin_func[]")
            continue
        out.append((raw, " && ".join(conditions) if conditions else "-"))
    return out


def parse(script_cpp: Path = SCRIPT_CPP) -> list[Buildin]:
    text = strip_comments(script_cpp.read_text(encoding="utf-8", errors="replace"))
    lines = text.splitlines()
    start = next((i for i, l in enumerate(lines) if TABLE_START.match(l)), None)
    if start is None:
        raise GenError(f"{script_cpp}: buildin_func[] not found")
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^\s*\{\s*nullptr", lines[i])), None)
    if end is None:
        raise GenError(f"{script_cpp}: end of buildin_func[] (the nullptr sentinel) not found")

    body = table_lines(script_cpp, lines[start + 1 : end], [])
    # Join the table into one stream, remembering the condition at each entry's start.
    entries: list[Buildin] = []
    stream = ""
    cond_at: list[tuple[int, str]] = []
    for raw, cond in body:
        cond_at.append((len(stream), cond))
        stream += raw + "\n"

    def condition_for(pos: int) -> str:
        cond = "-"
        for off, c in cond_at:
            if off > pos:
                break
            cond = c
        return cond

    pos = 0
    while True:
        m = DEF_RE.search(stream, pos)
        if not m:
            rest = stream[pos:].strip().strip(",").strip()
            if rest:
                raise GenError(f"{script_cpp}: unrecognised text in buildin_func[]: {rest[:80]!r}")
            break
        between = stream[pos : m.start()].strip().strip(",").strip()
        if between:
            raise GenError(f"{script_cpp}: unrecognised text in buildin_func[]: {between[:80]!r}")
        # Find the matching parenthesis (strings may hold parentheses).
        depth, i, in_str = 1, m.end(), False
        while depth:
            c = stream[i]
            if in_str:
                if c == "\\":
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        args = split_args(stream[m.end() : i - 1])
        two, deprecated = bool(m.group(1)), bool(m.group(2))
        want = 2 + two + deprecated
        where = f"buildin_func[] entry {stream[m.start():i]!r}"
        if len(args) != want:
            raise GenError(f"{where}: expected {want} arguments, got {len(args)}")
        function = args[0]
        if not re.fullmatch(r"\w+", function):
            raise GenError(f"{where}: bad function name")
        name = unquote(args[1], where) if two else function
        sig = unquote(args[2 if two else 1], where)
        date = unquote(args[-1], where) if deprecated else "-"
        entries.append(Buildin(name, f"buildin_{function}", sig, date, condition_for(m.start()), False))
        pos = i

    # Which C functions use a player.
    full = strip_comments(script_cpp.read_text(encoding="utf-8", errors="replace"))
    for inc in (SRC_DIR / "custom/script.inc",):
        if inc.exists():
            full += "\n" + strip_comments(inc.read_text(encoding="utf-8", errors="replace"))
    bodies: dict[str, str] = {}
    matches = list(FUNC_RE.finditer(full))
    for k, fm in enumerate(matches):
        stop = matches[k + 1].start() if k + 1 < len(matches) else len(full)
        bodies[f"buildin_{fm.group(1)}"] = full[fm.end() : stop]
    result = []
    for e in entries:
        if e.function not in bodies:
            raise GenError(f"{e.name}: no BUILDIN_FUNC({e.function[len('buildin_'):]}) found")
        result.append(Buildin(e.name, e.function, e.args, e.deprecated, e.condition, bool(PLAYER_RE.search(bodies[e.function]))))
    # add_buildin_func() registers the table in order, so a later entry with the same name
    # replaces an earlier one (e.g. the deprecated strmobinfo alias "getmonsterinfo").
    last = {e.name: k for k, e in enumerate(result)}
    return [Buildin(e.name, e.function, e.args, e.deprecated, e.condition, e.player, last[e.name] == k)
            for k, e in enumerate(result)]


def render(entries: list[Buildin]) -> str:
    live = [e for e in entries if e.effective]
    functions = {e.function for e in live}
    lines = [
        f"# Generated by {GENERATOR} from src/map/script.cpp buildin_func[]. Do not edit.",
        f"# {len(live)} script commands, {len(functions)} C functions"
        f" ({sum(e.deprecated != '-' for e in live)} deprecated,"
        f" {sum(e.condition != '-' for e in live)} conditional,"
        f" {sum(e.player for e in live)} use a player; {len(entries) - len(live)} table entries shadowed by a later one).",
        "# args: i=int s=string v=value r=reference l=label ?=optional *=repeat; player: the C function resolves a player"
        " (script_rid2sd & co.); effective: no = a later entry with the same name replaces this one.",
        "name\tfunction\targs\tdeprecated\tcondition\tplayer\teffective",
    ]
    for e in entries:
        lines.append(f"{e.name}\t{e.function}\t{e.args}\t{e.deprecated}\t{e.condition}"
                     f"\t{'yes' if e.player else 'no'}\t{'yes' if e.effective else 'no'}")
    return "\n".join(lines) + "\n"


def read_manifest(path: Path = OUTPUT) -> list[dict[str, str]]:
    """Reads the manifest back (used by the corpus coverage report)."""
    rows = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    header = rows[0].split("\t")
    return [d for d in (dict(zip(header, r.split("\t"))) for r in rows[1:]) if d["effective"] == "yes"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 when the manifest is out of date")
    ap.add_argument("--summary", action="store_true", help="print the counts")
    ap.add_argument("--output", type=Path, default=OUTPUT)
    opts = ap.parse_args()
    try:
        entries = parse()
    except GenError as e:
        print(f"script_buildins.py: {e}", file=sys.stderr)
        return 2
    names = [e.name for e in entries]
    text = render(entries)
    if opts.summary:
        print(text.splitlines()[1].lstrip("# "))
    if opts.check:
        current = opts.output.read_text(encoding="utf-8") if opts.output.exists() else ""
        if current != text:
            old = {l.split("\t")[0] for l in current.splitlines()[4:]}
            new = set(names)
            print(f"{opts.output.relative_to(ROOT) if opts.output.is_relative_to(ROOT) else opts.output} is out of date; "
                  f"run python3 {GENERATOR}", file=sys.stderr)
            if new - old:
                print(f"  added: {', '.join(sorted(new - old))}", file=sys.stderr)
            if old - new:
                print(f"  removed: {', '.join(sorted(old - new))}", file=sys.stderr)
            return 1
        return 0
    opts.output.write_text(text, encoding="utf-8")
    if not opts.summary:
        print(f"wrote {opts.output} ({len(entries)} commands)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
