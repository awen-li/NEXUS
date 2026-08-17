#!/usr/bin/env python3
"""Manifest-driven oracle for the named NEXUS benchmark suite."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Any


def run(command: list[str], *, environment: dict[str, str]) -> None:
    completed = subprocess.run(
        command,
        check=False,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    sys.stdout.write(completed.stdout)
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed with status {completed.returncode}: {command}"
        )


def wait_for_service(
    process: subprocess.Popen[str],
    socket_path: Path,
    timeout_seconds: float = 5.0,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if socket_path.exists():
            return
        if process.poll() is not None:
            output, _ = process.communicate()
            raise RuntimeError(
                f"service exited before creating its socket:\n{output}"
            )
        time.sleep(0.01)
    process.terminate()
    output, _ = process.communicate(timeout=2)
    raise RuntimeError(f"timed out waiting for service socket:\n{output}")


def execute_benchmark(
    case: dict[str, Any],
    arguments: argparse.Namespace,
    case_dir: Path,
    environment: dict[str, str],
) -> None:
    command = [
        arguments.python,
        "-m",
        "nexus_bench",
        "--case",
        case["id"],
        "--work-dir",
        str(case_dir / "work"),
    ]
    if case["id"] == "b3-extension-plugin":
        command.extend(["--plugin", str(arguments.plugin.resolve())])

    if case["id"] != "b4-extension-service":
        run(command, environment=environment)
        return

    service_socket = case_dir / "nexus-service.sock"
    service = subprocess.Popen(
        [
            str(arguments.service.resolve()),
            "--socket",
            str(service_socket),
            "--plugin",
            str(arguments.plugin.resolve()),
        ],
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_for_service(service, service_socket)
        command.extend(["--socket", str(service_socket)])
        run(command, environment=environment)
        service_output, _ = service.communicate(timeout=5)
        sys.stdout.write(service_output)
        if service.returncode != 0:
            raise RuntimeError(
                f"service failed with status {service.returncode}"
            )
    except BaseException:
        if service.poll() is None:
            service.terminate()
        service_output, _ = service.communicate(timeout=2)
        sys.stdout.write(service_output)
        raise


def dependency_key(dependency: dict[str, Any]) -> tuple[str, str, str]:
    return (
        dependency["source"],
        dependency["target"],
        dependency["mechanism"],
    )


def validate_graph(case: dict[str, Any], graph: dict[str, Any]) -> int:
    recovered = {dependency_key(item) for item in graph["dependencies"]}
    expected = {
        dependency_key(item) for item in case["expected_dependencies"]
    }
    if recovered != expected:
        raise AssertionError(
            f"{case['id']} dependency mismatch; "
            f"missing={sorted(expected - recovered)}, "
            f"unexpected={sorted(recovered - expected)}"
        )

    interactions = graph["interactions"]
    if len(interactions) != case["expected_interactions"]:
        raise AssertionError(
            f"{case['id']} expected {case['expected_interactions']} "
            f"interactions, got {len(interactions)}"
        )

    process_actors = {
        interaction["actor"].split("/tid:", maxsplit=1)[0]
        for interaction in interactions
    }
    if len(process_actors) != case["expected_processes"]:
        raise AssertionError(
            f"{case['id']} expected {case['expected_processes']} "
            f"processes, got {process_actors}"
        )

    for dependency in graph["dependencies"]:
        if not dependency["evidence"]:
            raise AssertionError(f"dependency lacks evidence: {dependency}")
        if not all("/seq:" in item for item in dependency["evidence"]):
            raise AssertionError(
                f"dependency evidence is not process-qualified: {dependency}"
            )
    return len(process_actors)


def write_expected_output(
    case: dict[str, Any],
    graph_dot: Path,
    graph_svg: Path,
    expected_output_dir: Path,
) -> None:
    case_dir = expected_output_dir / case["id"]
    case_dir.mkdir(parents=True, exist_ok=True)
    expected = {
        "schema": "nexus.expected-dependencies.v1",
        "benchmark": case["id"],
        "dependencies": case["expected_dependencies"],
    }
    (case_dir / "dependencies.json").write_text(
        json.dumps(expected, indent=2) + "\n",
        encoding="utf-8",
    )
    shutil.copyfile(graph_dot, case_dir / "graph.dot")
    shutil.copyfile(graph_svg, case_dir / "graph.svg")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", required=True)
    parser.add_argument("--package-dir", required=True, type=Path)
    parser.add_argument("--probe-lib-dir", required=True, type=Path)
    parser.add_argument("--plugin", required=True, type=Path)
    parser.add_argument("--service", required=True, type=Path)
    parser.add_argument("--analyzer", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--graphviz", required=True)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--expected-output-dir", type=Path)
    arguments = parser.parse_args()

    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    if manifest.get("schema") != "nexus.demo.benchmark-suite.v1":
        raise ValueError("unsupported benchmark manifest schema")

    work_dir = arguments.work_dir.resolve()
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)

    expected_output_dir = arguments.expected_output_dir
    if expected_output_dir is not None:
        expected_output_dir = expected_output_dir.resolve()
        expected_output_dir.mkdir(parents=True, exist_ok=True)

    base_environment = os.environ.copy()
    base_environment["PYTHONPATH"] = str(arguments.package_dir.resolve())
    current_library_path = base_environment.get("LD_LIBRARY_PATH", "")
    base_environment["LD_LIBRARY_PATH"] = os.pathsep.join(
        part
        for part in (
            str(arguments.probe_lib_dir.resolve()),
            current_library_path,
        )
        if part
    )

    total_interactions = 0
    total_dependencies = 0
    for case in manifest["cases"]:
        case_dir = work_dir / case["id"]
        case_dir.mkdir()
        trace = case_dir / "trace.jsonl"
        graph_json = case_dir / "graph.json"
        graph_dot = case_dir / "graph.dot"
        graph_svg = case_dir / "graph.svg"

        environment = base_environment.copy()
        environment["NEXUS_TRACE_FILE"] = str(trace)
        execute_benchmark(case, arguments, case_dir, environment)
        run(
            [
                str(arguments.analyzer.resolve()),
                "--trace",
                str(trace),
                "--json",
                str(graph_json),
                "--dot",
                str(graph_dot),
            ],
            environment=environment,
        )
        run(
            [
                arguments.graphviz,
                "-Tsvg",
                str(graph_dot),
                "-o",
                str(graph_svg),
            ],
            environment=environment,
        )

        graph = json.loads(graph_json.read_text(encoding="utf-8"))
        process_count = validate_graph(case, graph)
        total_interactions += len(graph["interactions"])
        total_dependencies += len(graph["dependencies"])
        if expected_output_dir is not None:
            write_expected_output(
                case,
                graph_dot,
                graph_svg,
                expected_output_dir,
            )

        print(
            f"PASS {case['id']}: {len(graph['interactions'])} interactions, "
            f"{len(graph['dependencies'])} dependencies, "
            f"{process_count} process(es); graph={graph_svg}"
        )

    print(
        f"NEXUS benchmark suite passed: {len(manifest['cases'])} cases, "
        f"{total_interactions} interactions, "
        f"{total_dependencies} dependencies."
    )


if __name__ == "__main__":
    main()
