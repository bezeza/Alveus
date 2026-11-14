#!/usr/bin/env python3
"""Check translation parity, Qt placeholders, markup, and QML source coverage."""

from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLACEHOLDER = re.compile(r"%(?:[1-9][0-9]*|n)")
TAG = re.compile(r"</?([A-Za-z][A-Za-z0-9]*)\b[^>]*>")


def parse_po(path: Path) -> dict[str, dict[str, str]]:
    entries: dict[str, dict[str, str]] = {}
    current: dict[str, str] = {}
    active: str | None = None

    def flush() -> None:
        nonlocal current, active
        if "msgid" in current:
            entries[current["msgid"]] = current
        current = {}
        active = None

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            flush()
            continue
        if line.startswith("#"):
            continue
        match = re.match(r"^(msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+(.*)$", line)
        if match:
            active = match.group(1)
            current[active] = ast.literal_eval(match.group(2))
        elif line.startswith('"') and active:
            current[active] += ast.literal_eval(line)
    flush()
    return entries


def fail(message: str) -> None:
    print(f"translation check: {message}", file=sys.stderr)
    raise SystemExit(1)


source = parse_po(ROOT / "po/uk.po")
russian = parse_po(ROOT / "po/ru.po")
source.pop("", None)
russian.pop("", None)

if source.keys() != russian.keys():
    missing = sorted(source.keys() - russian.keys())
    extra = sorted(russian.keys() - source.keys())
    fail(f"catalog mismatch; missing={missing!r}, extra={extra!r}")

for msgid, src in source.items():
    dst = russian[msgid]
    if src.get("msgid_plural", "") != dst.get("msgid_plural", ""):
        fail(f"plural source differs for {msgid!r}")
    forms = [dst.get(f"msgstr[{i}]", "") for i in range(3)] if "msgid_plural" in src else [dst.get("msgstr", "")]
    if any(not value for value in forms):
        fail(f"empty Russian translation for {msgid!r}")
    expected = sorted(PLACEHOLDER.findall(msgid))
    if "msgid_plural" in src:
        expected = sorted(set(expected + PLACEHOLDER.findall(src["msgid_plural"])))
    for form in forms:
        if sorted(PLACEHOLDER.findall(form)) != expected:
            fail(f"Qt placeholders differ for {msgid!r}: {form!r}")
        if TAG.findall(form) != TAG.findall(msgid):
            fail(f"HTML markup differs for {msgid!r}")

# Every literal passed to i18n()/i18nc()/i18np() in the QML UI must be translated.
source_messages = set(source)
russian_messages = set(russian)
for entry in source.values():
    if "msgid_plural" in entry:
        source_messages.add(entry["msgid_plural"])
for entry in russian.values():
    if "msgid_plural" in entry:
        russian_messages.add(entry["msgid_plural"])

qml_root = ROOT / "package/contents/ui"
single = re.compile(r'\bi18n\s*\(\s*("(?:\\.|[^"\\])*")', re.S)
contextual = re.compile(r'\bi18nc\s*\(\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")', re.S)
plural = re.compile(r'\bi18np\s*\(\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")', re.S)
for qml in qml_root.rglob("*.qml"):
    text = qml.read_text(encoding="utf-8")
    messages = [ast.literal_eval(m.group(1)) for m in single.finditer(text)]
    for match in contextual.finditer(text):
        messages.append(ast.literal_eval(match.group(2)))
    for match in plural.finditer(text):
        messages.extend((ast.literal_eval(match.group(1)), ast.literal_eval(match.group(2))))
    for message in messages:
        if message not in source_messages or message not in russian_messages:
            fail(f"{qml.relative_to(ROOT)} uses an untranslated source string: {message!r}")

print(f"OK: {len(source)} source messages and all QML strings have complete Russian translations.")
