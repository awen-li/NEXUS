"""B2: a Python component calling an in-process C++ extension."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

from . import _nexus_bench_ext as _native

_COMPONENT = "nexus_bench.b2_python_extension"
_INPUT = "hello from b2 python extension.\n"


def run(work_dir: Path) -> Dict[str, str]:
    work_dir.resolve().mkdir(parents=True, exist_ok=True)
    result = _native.transform_inline(_INPUT, _COMPONENT)
    if result != _INPUT.upper():
        raise RuntimeError("B2 extension returned unexpected content")
    return {"result": result}
