#!/usr/bin/env python3
"""
RetroVue gRPC stub generator
--------------------------------
Generates both C++ (via CMake build) and Python stubs for playout.proto.
Run this script from the root of the Retrovue-playout repo.

Example:
    python scripts/generate_stubs.py
"""

import os
import subprocess
import sys
from pathlib import Path

# Paths
repo_root = Path(__file__).resolve().parent.parent
proto_dir = repo_root / "proto"
proto_file = proto_dir / "retrovue" / "playout.proto"

# Path to the main RetroVue (Python) repo — adjust if needed
python_repo = repo_root.parent / "Retrovue"
python_out = python_repo / "core" / "proto"

def run(cmd, cwd=None):
    print(f"\n> {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd or repo_root, check=True)

def find_vcpkg_toolchain():
    """Find vcpkg toolchain file in common locations."""
    # Check VCPKG_ROOT environment variable
    vcpkg_root = os.environ.get("VCPKG_ROOT")
    if vcpkg_root:
        toolchain = Path(vcpkg_root) / "scripts" / "buildsystems" / "vcpkg.cmake"
        if toolchain.exists():
            return toolchain
    
    # Check common locations
    common_paths = [
        Path.home() / "source" / "vcpkg",
        Path.home() / "vcpkg",
        repo_root.parent / "vcpkg",
        Path("/usr/local/vcpkg"),
        Path("/opt/vcpkg"),
    ]
    # Windows-specific paths (only check on Windows)
    if os.name == "nt":
        common_paths.extend([
            Path("C:/vcpkg"),
            Path("C:/tools/vcpkg"),
        ])
    
    for base_path in common_paths:
        toolchain = base_path / "scripts" / "buildsystems" / "vcpkg.cmake"
        if toolchain.exists():
            return toolchain
    
    return None

def main():
    if not proto_file.exists():
        print(f"[ERROR] Missing proto file: {proto_file}")
        sys.exit(1)

    print("[INFO] Generating Python gRPC stubs...")
    run([
        sys.executable, "-m", "grpc_tools.protoc",
        "-I", str(proto_dir),
        f"--python_out={python_out}",
        f"--grpc_python_out={python_out}",
        str(proto_file)
    ])

    print("\n[INFO] Building C++ proto targets...")
    build_dir = repo_root / "build"
    build_dir.mkdir(exist_ok=True)
    
    # Find vcpkg toolchain file
    vcpkg_toolchain = find_vcpkg_toolchain()
    cmake_cmd = ["cmake", "-S", ".", "-B", "build"]
    if vcpkg_toolchain:
        print(f"[INFO] Found vcpkg toolchain: {vcpkg_toolchain}")
        cmake_cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={vcpkg_toolchain}")
    else:
        print("[WARN] vcpkg toolchain not found, proceeding without it")
    
    run(cmake_cmd)
    run(["cmake", "--build", "build"])

    print("\n[SUCCESS] All proto stubs generated successfully.")
    print(f"Python stubs: {python_out}/retrovue/playout_pb2*.py")
    print(f"C++ artifacts: {build_dir}/generated/")

if __name__ == "__main__":
    main()
