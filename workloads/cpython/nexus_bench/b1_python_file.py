"""B1: two attributed Python components communicating through a file."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

from . import _nexus_bench_ext as _native

_PRODUCER = "nexus_bench.b1_python_file.producer"
_CONSUMER = "nexus_bench.b1_python_file.consumer"
_INPUT = "hello from b1 python shared file.\n"


def _record(component: str, role: str, path: Path, context: str) -> None:
    _native.record_interaction(
        runtime="cpython",
        component=component,
        mechanism="file",
        role=role,
        object=str(path.resolve()),
        provenance="CPython package attribution + pathlib boundary",
        context=context,
        resolution="precise",
    )


def run(work_dir: Path) -> Dict[str, str]:
    work_dir = work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    exchange = work_dir / "exchange.txt"

    exchange.write_text(_INPUT, encoding="utf-8")
    _record(_PRODUCER, "write", exchange, "b1:producer-write")

    result = exchange.read_text(encoding="utf-8")
    _record(_CONSUMER, "read", exchange, "b1:consumer-read")
    if result != _INPUT:
        raise RuntimeError("B1 file consumer observed unexpected content")

    return {"exchange": str(exchange), "result": result}
