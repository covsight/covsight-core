#!/usr/bin/env python3
"""Amalgamate a split module tree into a single-header deliverable.

Decision D1 of docs/ucis-writer-impl-plan.md, and D-9 of
docs/ucis-xml-cpp-impl-plan.md: a library is developed as separate modules so it
can be reviewed and unit-tested per module, and shipped as one file so it can be
vendored by dropping it into a tree.

Two output styles:

  stb          C, STB-style. Including the header gives declarations; including
               it with the implementation macro defined, in exactly one
               translation unit, also gives definitions. In that mode every
               internal helper becomes `static`, so a consumer gains no symbols
               beyond the API it asked for.
  header-only  C++. Everything is `#pragma once` and `inline`; there is no
               implementation macro and no second inclusion mode.

Projects are described by a manifest (TOML) so this script carries no
project-specific knowledge:

    python3 tools/amalgamate.py --manifest cpp/ucis-xml/amalgam.toml --output PATH
    python3 tools/amalgamate.py --manifest cpp/ucis-xml/amalgam.toml --check  PATH

With no --manifest, the manifest is inferred from the output path so the
long-standing invocations keep working unchanged:

    python3 tools/amalgamate.py --output c/ucis-writer/include/ucis_writer.h
    python3 tools/amalgamate.py --check  c/ucis-writer/include/ucis_writer.h
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    import tomli as tomllib  # type: ignore

ROOT = Path(__file__).resolve().parent.parent

# Output path -> manifest, so the pre-manifest command lines still resolve.
KNOWN = {
    "c/ucis-writer/include/ucis_writer.h": "c/ucis-writer/amalgam.toml",
    "cpp/ucis-xml/include/ucis_xml.hpp": "cpp/ucis-xml/amalgam.toml",
}

LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s+"[^"]+"\s*$')
PRAGMA_ONCE = re.compile(r"^\s*#\s*pragma\s+once\s*$")


def strip_guard(text: str, path: Path) -> str:
    """Remove a file's include guard, whether #pragma once or #ifndef/#define."""
    lines = text.splitlines()

    if any(PRAGMA_ONCE.match(l) for l in lines):
        return "\n".join(l for l in lines
                         if not PRAGMA_ONCE.match(l)).strip("\n") + "\n"

    start = None
    guard = None
    for i, line in enumerate(lines):
        m = re.match(r"^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", line)
        if m and i + 1 < len(lines):
            m2 = re.match(r"^\s*#\s*define\s+" + re.escape(m.group(1)) + r"\s*$",
                          lines[i + 1])
            if m2:
                start, guard = i, m.group(1)
                break
    if start is None:
        raise SystemExit(f"{path}: no include guard found")

    end = None
    for i in range(len(lines) - 1, start, -1):
        if re.match(r"^\s*#\s*endif\b", lines[i]):
            end = i
            break
    if end is None:
        raise SystemExit(f"{path}: guard {guard} is never closed")

    del lines[end]
    del lines[start:start + 2]
    return "\n".join(lines).strip("\n") + "\n"


def strip_local_includes(text: str) -> str:
    return "\n".join(l for l in text.splitlines()
                     if not LOCAL_INCLUDE.match(l)) + "\n"


def hoist_system_includes(text: str) -> tuple[str, list[str]]:
    """Pull `#include <...>` lines out, so the amalgamation includes each once.

    Only matters for header-only, where every module is concatenated into one
    scope; in stb style the sources are already inside one implementation block
    and duplicate system includes are harmless (and were kept in place for
    years, so moving them would churn the committed header for no gain).
    """
    kept, includes = [], []
    for line in text.splitlines():
        if re.match(r"^\s*#\s*include\s+<[^>]+>\s*$", line):
            includes.append(line.strip())
        else:
            kept.append(line)
    return "\n".join(kept).strip("\n") + "\n", includes


def section(title: str, comment: str) -> str:
    rule = "=" * max(4, 72 - len(title))
    if comment == "//":
        return f"\n// ==== {title} {rule}\n\n"
    return f"\n/* ==== {title} {rule} */\n\n"


def build_stb(m: dict, src: Path) -> str:
    parts = [m["banner"]]
    for decl in m.get("decls", []):
        text = (src / decl).read_text()
        # Each declares the next; in one file that include cannot resolve.
        text = "\n".join(l for l in text.splitlines()
                         if not LOCAL_INCLUDE.match(l))
        parts.append(text)

    macro = m["implementation_macro"]
    done = m.get("implemented_macro", macro.replace("IMPLEMENTATION",
                                                    "IMPLEMENTED"))
    internal = m.get("amalgamated_macro")
    parts.append(f"\n#ifdef {macro}\n#ifndef {done}\n#define {done}\n")
    if internal:
        parts.append("\n/* One translation unit: give every internal helper "
                     "internal linkage. */\n"
                     f"#define {internal} 1\n")

    for name in m.get("impl_headers", []) + m.get("impl_sources", []):
        path = src / name
        text = path.read_text()
        if name.endswith(".h"):
            text = strip_guard(text, path)
        text = strip_local_includes(text)
        parts.append(section(name, "/*"))
        parts.append(text.strip("\n") + "\n")

    parts.append(f"\n#endif /* {done} */\n#endif /* {macro} */\n")
    return "".join(parts)


def build_header_only(m: dict, src: Path) -> str:
    bodies, system = [], []
    for name in m["parts"]:
        path = src / name
        text = strip_local_includes(strip_guard(path.read_text(), path))
        text, incs = hoist_system_includes(text)
        for inc in incs:
            if inc not in system:
                system.append(inc)
        bodies.append(section(name, "//"))
        bodies.append(text.strip("\n") + "\n")

    return "".join([m["banner"], "\n#pragma once\n\n",
                    "\n".join(system) + "\n", *bodies])


def load_manifest(path: Path) -> dict:
    m = tomllib.loads(path.read_text())
    missing = {"style", "src", "banner"} - m.keys()
    if missing:
        raise SystemExit(f"{path}: manifest is missing {sorted(missing)}")
    return m


def build(manifest_path: Path) -> str:
    m = load_manifest(manifest_path)
    src = ROOT / m["src"]
    if m["style"] == "stb":
        return build_stb(m, src)
    if m["style"] == "header-only":
        return build_header_only(m, src)
    raise SystemExit(f"{manifest_path}: unknown style {m['style']!r}")


def resolve_manifest(args, target: Path) -> Path:
    if args.manifest:
        return Path(args.manifest)
    try:
        rel = target.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        rel = target.as_posix()
    if rel in KNOWN:
        return ROOT / KNOWN[rel]
    raise SystemExit(
        f"{target}: no manifest known for this output; pass --manifest. "
        f"Known outputs: {', '.join(sorted(KNOWN))}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", metavar="PATH",
                    help="TOML manifest describing the project to amalgamate")
    ap.add_argument("--output", metavar="PATH",
                    help="write the amalgamation to PATH")
    ap.add_argument("--check", metavar="PATH",
                    help="fail if PATH differs from the amalgamation")
    args = ap.parse_args(argv)

    if not args.output and not args.check:
        ap.error("one of --output or --check is required")

    target = Path(args.output or args.check)
    manifest = resolve_manifest(args, target)
    text = build(manifest)

    if args.check:
        if not target.exists():
            print(f"{target}: missing; run "
                  f"python3 tools/amalgamate.py --manifest {manifest} "
                  f"--output {target}", file=sys.stderr)
            return 1
        if target.read_text() != text:
            print(f"{target}: stale. The committed single-header deliverable "
                  f"no longer matches {manifest.parent.as_posix()}'s sources.\n"
                  f"Run: python3 tools/amalgamate.py --manifest {manifest} "
                  f"--output {target}", file=sys.stderr)
            return 1
        return 0

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
