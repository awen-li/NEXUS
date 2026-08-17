"""Compatibility wrapper for the B4 extension-service benchmark."""

from __future__ import annotations

from pathlib import Path
from typing import Dict

from .b4_extension_service import run


def run_pipeline(work_dir: Path, socket_path: Path) -> Dict[str, str]:
    """Run B4 using the original public entry point."""
    return run(work_dir, socket_path)
