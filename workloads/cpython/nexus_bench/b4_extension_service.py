"""B4: Python -> extension <-> C++ service -> C++ plugin."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

from . import _nexus_bench_ext as _native

_COMPONENT = "nexus_bench.b4_extension_service"
_INPUT = "hello from b4 extension service.\n"


def _record_file(role: str, path: Path, context: str) -> None:
    _native.record_interaction(
        runtime="cpython",
        component=_COMPONENT,
        mechanism="file",
        role=role,
        object=str(path.resolve()),
        provenance="CPython package attribution + pathlib boundary",
        context=context,
        resolution="precise",
    )


def run(work_dir: Path, socket_path: Path) -> Dict[str, str]:
    work_dir = work_dir.resolve()
    socket_path = socket_path.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)

    input_path = work_dir / "input.txt"
    output_path = work_dir / "output.txt"
    input_path.write_text(_INPUT, encoding="utf-8")
    _record_file("write", input_path, "b4:write-input")

    extension_result = _native.run_via_service(
        str(input_path),
        str(output_path),
        str(socket_path),
        _COMPONENT,
    )
    result = output_path.read_text(encoding="utf-8")
    _record_file("read", output_path, "b4:read-output")

    if result != extension_result or result != _INPUT.upper():
        raise RuntimeError("B4 service pipeline returned unexpected content")
    return {
        "input": str(input_path),
        "output": str(output_path),
        "socket": str(socket_path),
        "result": result,
    }
