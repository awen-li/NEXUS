from __future__ import annotations

import argparse
import json
from pathlib import Path

from .suite import CASE_IDS, run_cases


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run one or all named NEXUS CPython/native benchmarks"
    )
    parser.add_argument(
        "--case",
        choices=(*CASE_IDS, "all"),
        default="all",
        help="benchmark case to run (default: all)",
    )
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--plugin", type=Path)
    parser.add_argument("--socket", type=Path)
    arguments = parser.parse_args()

    selected = CASE_IDS if arguments.case == "all" else (arguments.case,)
    result = run_cases(
        selected,
        arguments.work_dir,
        plugin_path=arguments.plugin,
        socket_path=arguments.socket,
    )
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
