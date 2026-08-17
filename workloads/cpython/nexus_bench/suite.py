"""Dispatch the named NEXUS benchmark cases."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable

from . import b1_python_file
from . import b2_python_extension
from . import b3_extension_plugin
from . import b4_extension_service

CASE_IDS = (
    "b1-python-file",
    "b2-python-extension",
    "b3-extension-plugin",
    "b4-extension-service",
)


def run_case(
    case_id: str,
    work_dir: Path,
    *,
    plugin_path: Path | None = None,
    socket_path: Path | None = None,
) -> Dict[str, str]:
    if case_id == "b1-python-file":
        return b1_python_file.run(work_dir)
    if case_id == "b2-python-extension":
        return b2_python_extension.run(work_dir)
    if case_id == "b3-extension-plugin":
        if plugin_path is None:
            raise ValueError("b3-extension-plugin requires --plugin")
        return b3_extension_plugin.run(work_dir, plugin_path)
    if case_id == "b4-extension-service":
        if socket_path is None:
            raise ValueError("b4-extension-service requires --socket")
        return b4_extension_service.run(work_dir, socket_path)
    raise ValueError(f"unknown benchmark case: {case_id}")


def run_cases(
    case_ids: Iterable[str],
    work_dir: Path,
    *,
    plugin_path: Path | None = None,
    socket_path: Path | None = None,
) -> Dict[str, Dict[str, str]]:
    return {
        case_id: run_case(
            case_id,
            work_dir / case_id,
            plugin_path=plugin_path,
            socket_path=socket_path,
        )
        for case_id in case_ids
    }
