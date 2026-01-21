#!/usr/bin/env python3
"""
Unified Build Script for Owl Browser

Handles building for:
- macOS headless (MCP server mode)
- macOS UI (visual browser)
- Ubuntu headless (server deployment)
- HTTP Server (REST API server)
- E2E IPC Tests (test client)

Usage:
    python3 scripts/build.py              # Default: headless for current platform
    python3 scripts/build.py --ui         # Build UI version (macOS only)
    python3 scripts/build.py --headless   # Build headless version
    python3 scripts/build.py --http       # Build HTTP server only
    python3 scripts/build.py --all        # Build browser + HTTP server
    python3 scripts/build.py --clean      # Clean build directory first
    python3 scripts/build.py --skip-cef   # Skip CEF download (use existing)
    python3 scripts/build.py --build-e2e  # Build e2e-ipc-tests client

Testing:
    python3 scripts/build.py --test                           # Run smoke tests after build
    python3 scripts/build.py --test --test-mode full          # Run full test suite
    python3 scripts/build.py --test --test-mode parallel      # Run parallel tests
    python3 scripts/build.py --test --connection-mode socket  # Force socket mode
    python3 scripts/build.py --test --connection-mode pipe    # Force pipe mode
    python3 scripts/build.py --test --test-mode parallel --concurrency 8  # 8 parallel threads
"""

import os
import sys
import subprocess
import platform
import shutil
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)


def get_cpu_count():
    """Get number of CPUs for parallel build."""
    try:
        import multiprocessing
        return multiprocessing.cpu_count()
    except:
        return 4


def run_cmd(cmd, cwd=None, check=True):
    """Run command and print output."""
    print(f"  → {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    result = subprocess.run(
        cmd,
        shell=isinstance(cmd, str),
        cwd=cwd or PROJECT_ROOT,
        capture_output=False
    )
    if check and result.returncode != 0:
        print(f"  ✗ Command failed with code {result.returncode}")
        sys.exit(1)
    return result


def detect_platform():
    """Detect current platform."""
    system = platform.system()
    if system == "Darwin":
        return "macos"
    elif system == "Linux":
        return "ubuntu"
    else:
        print(f"Unsupported platform: {system}")
        sys.exit(1)


def download_cef(plat):
    """Download CEF if not present."""
    cef_dir = os.path.join(PROJECT_ROOT, "third_party", f"cef_{plat}")

    if os.path.exists(cef_dir):
        print("  CEF already downloaded")
        return True

    print("  Downloading CEF...")
    run_cmd("node scripts/download-cef.cjs")
    return True


def patch_angle():
    """Patch ANGLE with GPU spoofing wrapper."""
    print("  Patching ANGLE...")
    run_cmd("python3 scripts/patch_angle.py")


def build_cmake(build_type, jobs):
    """Run CMake build."""
    build_dir = os.path.join(PROJECT_ROOT, "build")
    os.makedirs(build_dir, exist_ok=True)

    # Determine CMake options and target
    if build_type == "ui":
        cmake_opts = "-DBUILD_HEADLESS=OFF -DBUILD_UI=ON"
        target = "owl_browser_ui"
    elif build_type == "http":
        cmake_opts = "-DBUILD_HEADLESS=ON -DBUILD_UI=OFF"
        target = "owl_http_server"
    else:
        cmake_opts = "-DBUILD_HEADLESS=ON -DBUILD_UI=OFF"
        target = "owl_browser"

    # Configure
    print(f"  Configuring CMake ({build_type})...")
    run_cmd(f"cmake .. {cmake_opts}", cwd=build_dir)

    # Build
    print(f"  Building {target}...")
    run_cmd(f"make {target} -j{jobs}", cwd=build_dir)


def build_http_server(jobs):
    """Build HTTP server separately."""
    build_dir = os.path.join(PROJECT_ROOT, "build")
    os.makedirs(build_dir, exist_ok=True)

    # Configure if not already configured
    if not os.path.exists(os.path.join(build_dir, "Makefile")):
        print("  Configuring CMake...")
        run_cmd("cmake .. -DBUILD_HEADLESS=ON -DBUILD_UI=OFF", cwd=build_dir)

    # Build HTTP server
    print("  Building owl_http_server...")
    run_cmd(f"make owl_http_server -j{jobs}", cwd=build_dir)


def copy_angle_wrapper(build_type):
    """Copy ANGLE wrapper to app bundle using patch_angle.py."""
    print("  Copying ANGLE wrapper to app bundle...")
    if build_type == "ui":
        run_cmd(["python3", os.path.join(SCRIPT_DIR, "patch_angle.py"), "--copy-to-build", "--ui"])
    else:
        run_cmd(["python3", os.path.join(SCRIPT_DIR, "patch_angle.py"), "--copy-to-build"])


def sign_macos_app(build_type):
    """Sign macOS app bundle."""
    if build_type == "ui":
        app_path = os.path.join(PROJECT_ROOT, "build", "Release", "Owl Browser.app")
    else:
        app_path = os.path.join(PROJECT_ROOT, "build", "Release", "owl_browser.app")

    if not os.path.exists(app_path):
        print(f"  WARNING: App not found at {app_path}")
        return

    print(f"  Signing {os.path.basename(app_path)}...")
    run_cmd(["codesign", "--force", "--deep", "--sign", "-", app_path])
    print("  ✓ App signed successfully")


def set_linux_permissions():
    """Set proper permissions on Linux."""
    release_dir = os.path.join(PROJECT_ROOT, "build", "Release")

    # Make binaries executable
    for name in ["owl_browser", "owl_browser_helper"]:
        path = os.path.join(release_dir, name)
        if os.path.exists(path):
            os.chmod(path, 0o755)

    print("  ✓ Permissions set")


def build_mcp():
    """Build MCP server standalone bundle."""
    print("  Building MCP server...")
    run_cmd("node scripts/build-mcp-standalone.cjs")


def build_e2e_tests(jobs):
    """Build e2e-ipc-tests client."""
    e2e_dir = os.path.join(PROJECT_ROOT, "e2e-ipc-tests")
    build_dir = os.path.join(e2e_dir, "build")
    os.makedirs(build_dir, exist_ok=True)

    print("  Configuring e2e-ipc-tests...")
    run_cmd("cmake ..", cwd=build_dir)

    print("  Building ipc_test_client...")
    run_cmd(f"make -j{jobs}", cwd=build_dir)
    print("  ✓ e2e-ipc-tests built successfully")


def clean_build():
    """Clean build directory."""
    build_dir = os.path.join(PROJECT_ROOT, "build")
    if os.path.exists(build_dir):
        print(f"  Removing {build_dir}...")
        shutil.rmtree(build_dir)
    print("  ✓ Build directory cleaned")


def main():
    parser = argparse.ArgumentParser(description="Build Owl Browser")
    parser.add_argument("--ui", action="store_true", help="Build UI version (macOS only)")
    parser.add_argument("--headless", action="store_true", help="Build headless version")
    parser.add_argument("--http", action="store_true", help="Build HTTP server only")
    parser.add_argument("--all", action="store_true", help="Build browser + HTTP server")
    parser.add_argument("--clean", action="store_true", help="Clean build directory first")
    parser.add_argument("--skip-cef", action="store_true", help="Skip CEF download")
    parser.add_argument("--skip-mcp", action="store_true", help="Skip MCP server build")
    parser.add_argument("--skip-http", action="store_true", help="Skip HTTP server build (with --all)")
    parser.add_argument("--build-e2e", action="store_true", help="Build e2e-ipc-tests client")
    parser.add_argument("--test", action="store_true", help="Run E2E IPC tests after build")
    parser.add_argument("--test-mode", type=str, default="smoke", choices=["smoke", "full", "benchmark", "parallel"], help="E2E test mode (default: smoke)")
    parser.add_argument("--connection-mode", type=str, default="auto", choices=["auto", "socket", "pipe"], help="E2E test connection mode (default: auto)")
    parser.add_argument("--concurrency", type=int, default=4, help="Number of parallel threads for parallel test mode (default: 4)")
    parser.add_argument("-j", "--jobs", type=int, default=get_cpu_count(), help="Parallel jobs")
    args = parser.parse_args()

    plat = detect_platform()

    # Determine build type
    if args.http:
        build_type = "http"
    elif args.ui:
        build_type = "ui"
    else:
        build_type = "headless"

    # UI only supported on macOS
    if args.ui and plat != "macos":
        print("ERROR: UI build is only supported on macOS")
        sys.exit(1)

    # HTTP-only build is simpler
    if args.http and not args.all:
        print("=" * 60)
        print("Owl Browser HTTP Server Build")
        print("=" * 60)
        print(f"  Platform: {plat}")
        print(f"  Jobs:     {args.jobs}")
        print()

        if args.clean:
            print("[1/3] Cleaning build directory...")
            clean_build()
        else:
            print("[1/3] Skipping clean (use --clean to clean)")

        # Download CEF (required for cmake configuration)
        if not args.skip_cef:
            print("\n[2/3] Checking CEF...")
            download_cef(plat)
        else:
            print("\n[2/3] Skipping CEF download")

        print("\n[3/3] Building HTTP server...")
        build_http_server(args.jobs)

        print("\n" + "=" * 60)
        print("HTTP Server build complete!")
        print("=" * 60)
        print("\nTo run: ./build/Release/owl_http_server")
        print("   or:  npm run start:http")
        return

    print("=" * 60)
    print("Owl Browser Build")
    print("=" * 60)
    print(f"  Platform:   {plat}")
    print(f"  Build type: {build_type}")
    print(f"  Jobs:       {args.jobs}")
    if args.all:
        print(f"  HTTP Server: {'skip' if args.skip_http else 'include'}")
    print()

    # Determine step count
    total_steps = 6
    if args.all and not args.skip_http:
        total_steps += 1
    if args.build_e2e:
        total_steps += 1

    # Clean if requested
    if args.clean:
        print(f"[1/{total_steps}] Cleaning build directory...")
        clean_build()
    else:
        print(f"[1/{total_steps}] Skipping clean (use --clean to clean)")

    # Download CEF
    if not args.skip_cef:
        print(f"\n[2/{total_steps}] Checking CEF...")
        download_cef(plat)
    else:
        print(f"\n[2/{total_steps}] Skipping CEF download")

    # Patch ANGLE
    print(f"\n[3/{total_steps}] Patching ANGLE for GPU virtualization...")
    patch_angle()

    # Build
    print(f"\n[4/{total_steps}] Building browser...")
    build_cmake(build_type, args.jobs)

    # Platform-specific post-build
    print(f"\n[5/{total_steps}] Post-build tasks...")
    if plat == "macos":
        copy_angle_wrapper(build_type)
        sign_macos_app(build_type)
    else:
        set_linux_permissions()

    # Build MCP
    if not args.skip_mcp:
        print(f"\n[6/{total_steps}] Building MCP server...")
        build_mcp()
    else:
        print(f"\n[6/{total_steps}] Skipping MCP server build")

    # Build HTTP server (with --all flag)
    current_step = 7
    if args.all and not args.skip_http:
        print(f"\n[{current_step}/{total_steps}] Building HTTP server...")
        build_http_server(args.jobs)
        current_step += 1

    # Build e2e-ipc-tests (with --build-e2e flag)
    if args.build_e2e:
        print(f"\n[{current_step}/{total_steps}] Building e2e-ipc-tests...")
        build_e2e_tests(args.jobs)

    print("\n" + "=" * 60)
    print("Build complete!")
    print("=" * 60)

    if build_type == "ui":
        print("\nTo run: npm run start:ui")
        print("   or:  open build/Release/owl_browser_ui.app")
    else:
        print("\nTo run: npm start")
        print("   or:  node src/mcp-server.cjs")

    if args.all and not args.skip_http:
        print("\nHTTP server: ./build/Release/owl_http_server")
        print("        or:  npm run start:http")

    # Run E2E IPC tests if requested
    if args.test:
        print("\n" + "=" * 60)
        print(f"Running E2E IPC tests ({args.test_mode} mode, {args.connection_mode} connection)...")
        print("=" * 60)

        e2e_test_client = os.path.join(PROJECT_ROOT, "e2e-ipc-tests", "build", "ipc_test_client")
        if plat == "macos":
            browser_path = os.path.join(PROJECT_ROOT, "build", "Release", "owl_browser.app", "Contents", "MacOS", "owl_browser")
        else:
            browser_path = os.path.join(PROJECT_ROOT, "build", "Release", "owl_browser")

        if os.path.exists(e2e_test_client) and os.path.exists(browser_path):
            test_cmd = [
                e2e_test_client,
                "--browser-path", browser_path,
                "--mode", args.test_mode,
                "--connection-mode", args.connection_mode
            ]
            # Add concurrency for parallel mode
            if args.test_mode == "parallel":
                test_cmd.extend(["--concurrency", str(args.concurrency)])

            result = subprocess.run(test_cmd)
            if result.returncode != 0:
                print("\n" + "=" * 60)
                print("E2E IPC tests FAILED!")
                print("=" * 60)
                sys.exit(1)
            else:
                print("\n" + "=" * 60)
                print("E2E IPC tests PASSED!")
                print("=" * 60)
        else:
            print(f"WARNING: E2E test client not found at {e2e_test_client}")
            print("Build e2e-ipc-tests first: npm run build:e2e")
            print("  or: cd e2e-ipc-tests/build && cmake .. && make")


if __name__ == "__main__":
    main()
