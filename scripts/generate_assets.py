#!/usr/bin/env python3
"""Decode repository-safe textual assets into the runtime asset tree."""
import argparse
import base64
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()

try:
    encoded = args.source.read_text(encoding="ascii")
    decoded = base64.b64decode("".join(encoded.split()), validate=True)
except (OSError, ValueError) as error:
    raise SystemExit(f"cannot generate {args.destination}: {error}") from error

if not decoded.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit(f"{args.source} does not contain a PNG")
args.destination.parent.mkdir(parents=True, exist_ok=True)
temporary = args.destination.with_suffix(args.destination.suffix + ".tmp")
temporary.write_bytes(decoded)
temporary.replace(args.destination)
