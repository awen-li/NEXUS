"""B3: a Python-called extension dynamically loading a C++ plugin."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

from . import _nexus_bench_ext as _native

_COMPONENT = "nexus_bench.b3_extension_plugin"
_INPUT = "hello from b3 extension plugin.\n"


def run(work_dir: Path, plugin_path: Path) -> Dict[str, str]:
    work_dir.resolve().mkdir(parents=True, exist_ok=True)
    plugin_path = plugin_path.resolve()
    result = _native.transform_via_plugin(
        _INPUT,
        str(plugin_path),
        _COMPONENT,
    )
    if result != _INPUT.upper():
        raise RuntimeError("B3 plugin returned unexpected content")
    return {"plugin": str(plugin_path), "result": result}
