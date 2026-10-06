#!/usr/bin/env python3
"""Build every target listed in build.yaml, the same way ZMK's GitHub Actions does.

Runs inside the ZMK Docker image; use ./build.sh rather than calling this directly.
"""
import argparse
import itertools
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parent
WS = REPO / ".west-build"
OUT = REPO / "firmware"


def run(*cmd, **kwargs):
    print("$", shlex.join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, cwd=WS, **kwargs)


def prepare_workspace(force_update):
    manifest = REPO / "config" / "west.yml"
    ws_manifest = WS / "config" / "west.yml"
    ws_manifest.parent.mkdir(parents=True, exist_ok=True)
    (WS / ".home").mkdir(exist_ok=True)  # $HOME inside the container (see build.sh)

    update = force_update
    if not (WS / ".west").is_dir():
        shutil.copy(manifest, ws_manifest)
        run("west", "init", "-l", ws_manifest.parent, check=True)
        update = True
    elif manifest.read_bytes() != ws_manifest.read_bytes():
        shutil.copy(manifest, ws_manifest)
        update = True

    if update:
        run("west", "update", "--narrow", "--fetch-opt=--filter=tree:0", check=True)
        run("west", "zephyr-export", check=True)


def as_list(value):
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def load_targets():
    """Expand build.yaml into a list of targets (board x shield matrix + include)."""
    data = yaml.safe_load((REPO / "build.yaml").read_text()) or {}
    entries = [
        {"board": board, "shield": shield}
        for board, shield in itertools.product(
            as_list(data.get("board")), as_list(data.get("shield")) or [None]
        )
    ]
    entries += as_list(data.get("include"))

    targets = []
    for entry in entries:
        board = entry["board"]
        shield = entry.get("shield")
        name = entry.get("artifact-name") or (
            f"{shield}-{board}-zmk" if shield else f"{board}-zmk"
        )
        targets.append(
            {
                "name": name.replace(" ", "-"),
                "board": board,
                "shield": shield,
                "snippet": entry.get("snippet"),
                "cmake_args": shlex.split(entry.get("cmake-args") or ""),
            }
        )
    return targets


def build(target, pristine):
    build_dir = WS / "build" / target["name"]
    cmd = ["west", "build", "-p", pristine, "-s", "zmk/app", "-d", build_dir, "-b", target["board"]]
    if target["snippet"]:
        cmd += ["-S", target["snippet"]]
    cmd += ["--", f"-DZMK_CONFIG={REPO / 'config'}", f"-DZMK_EXTRA_MODULES={REPO}"]
    if target["shield"]:
        cmd.append(f"-DSHIELD={target['shield']}")
    cmd += target["cmake_args"]

    if run(*cmd).returncode != 0:
        return False

    for ext in ("uf2", "bin"):
        image = build_dir / "zephyr" / f"zmk.{ext}"
        if image.exists():
            OUT.mkdir(exist_ok=True)
            shutil.copy(image, OUT / f"{target['name']}.{ext}")
            return True
    print(f"no firmware image found in {build_dir / 'zephyr'}", file=sys.stderr)
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-p", "--pristine", action="store_true", help="clean build")
    parser.add_argument("--update", action="store_true", help="re-run west update")
    parser.add_argument("filters", nargs="*", help="build only targets whose name contains one of these")
    args = parser.parse_args()

    targets = load_targets()
    if args.filters:
        targets = [t for t in targets if any(f in t["name"] for f in args.filters)]
        if not targets:
            names = "\n  ".join(t["name"] for t in load_targets())
            sys.exit(f"no target matches {args.filters}; available:\n  {names}")

    prepare_workspace(args.update)

    results = {t["name"]: build(t, "always" if args.pristine else "auto") for t in targets}

    print("\n==> summary")
    for name, ok in results.items():
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
    if not all(results.values()):
        sys.exit(1)


if __name__ == "__main__":
    main()
