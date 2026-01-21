#!/usr/bin/env python3
"""
ANGLE Wrapper Patcher

This script builds and installs the ANGLE wrapper library for GPU virtualization.
It works on both macOS and Linux (Ubuntu).

The wrapper intercepts GL calls to:
1. Spoof GPU vendor/renderer/version strings
2. Filter Apple-specific extensions that reveal hardware

Usage:
    python3 scripts/patch_angle.py              # Build wrapper in third_party
    python3 scripts/patch_angle.py --unpatch    # Restore original ANGLE
    python3 scripts/patch_angle.py --copy-to-build        # Copy wrapper to headless app bundle
    python3 scripts/patch_angle.py --copy-to-build --ui   # Copy wrapper to UI app bundle

This script is automatically run after CEF download via npm run download:cef
"""

import os
import sys
import shutil
import subprocess
import platform
import tempfile

# Paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

# Use the proper wrapper from src/gpu/angle_wrapper/owl_angle_wrapper.c
# This wrapper reads GPU values from environment variables:
# - OWL_GPU_VENDOR
# - OWL_GPU_RENDERER
# - OWL_GPU_VERSION
# - OWL_GPU_GLSL_VERSION
WRAPPER_SOURCE_FILE = os.path.join(PROJECT_ROOT, "src", "gpu", "angle_wrapper", "owl_angle_wrapper.c")


def get_cef_paths():
    """Get the paths to CEF ANGLE libraries based on platform."""
    system = platform.system()

    if system == "Darwin":
        cef_base = os.path.join(PROJECT_ROOT, "third_party", "cef_macos")
        release_lib_dir = os.path.join(cef_base, "Release", "Chromium Embedded Framework.framework", "Libraries")
        debug_lib_dir = os.path.join(cef_base, "Debug", "Chromium Embedded Framework.framework", "Libraries")

        return {
            "system": "Darwin",
            "lib_name": "libGLESv2.dylib",
            "lib_name_original": "libGLESv2_original.dylib",
            "lib_dirs": [release_lib_dir, debug_lib_dir],
        }
    elif system == "Linux":
        cef_base = os.path.join(PROJECT_ROOT, "third_party", "cef_linux")
        release_lib_dir = os.path.join(cef_base, "Release")

        return {
            "system": "Linux",
            "lib_name": "libGLESv2.so",
            "lib_name_original": "libGLESv2_original.so",
            "lib_dirs": [release_lib_dir],
        }
    else:
        raise RuntimeError(f"Unsupported platform: {system}")


def backup_original_angle(lib_dir, config):
    """Backup the original ANGLE library."""
    original_path = os.path.join(lib_dir, config["lib_name"])
    backup_path = os.path.join(lib_dir, config["lib_name_original"])

    if os.path.exists(backup_path):
        print(f"  Original already backed up: {backup_path}")
        return True

    if not os.path.exists(original_path):
        print(f"  ERROR: Original ANGLE not found: {original_path}")
        return False

    print(f"  Backing up original ANGLE library...")
    shutil.copy2(original_path, backup_path)
    return True


def fix_original_self_reference(lib_dir, config):
    """Fix the original library's self-reference on macOS."""
    if config["system"] != "Darwin":
        return True

    backup_path = os.path.join(lib_dir, config["lib_name_original"])

    # Check current dependencies
    result = subprocess.run(["otool", "-L", backup_path], capture_output=True, text=True)

    # Fix self-reference to use @loader_path for proper resolution
    # The original might have ./libGLESv2.dylib or ./libGLESv2_original.dylib
    print(f"  Fixing original library's self-reference...")

    # Change install name (the library's own ID)
    subprocess.run([
        "install_name_tool",
        "-id", f"@loader_path/{config['lib_name_original']}",
        backup_path
    ], capture_output=True)

    # Also fix any reference to the old name
    if "./libGLESv2.dylib" in result.stdout:
        subprocess.run([
            "install_name_tool",
            "-change", "./libGLESv2.dylib", f"@loader_path/{config['lib_name_original']}",
            backup_path
        ], capture_output=True)

    return True


def build_wrapper_macos(lib_dir, config, source_file):
    """Build wrapper on macOS that intercepts GL calls and forwards to original."""
    original_path = os.path.join(lib_dir, config["lib_name_original"])
    wrapper_path = os.path.join(lib_dir, config["lib_name"])
    obj_path = os.path.join(lib_dir, "owl_wrapper.o")

    print(f"  Building wrapper with re-export (two-phase)...")

    # Two-phase build to ensure our symbols take precedence over re-exported ones:
    # Phase 1: Compile our wrapper to object file
    # Phase 2: Link with -force_load for our object, then re-export original

    # Phase 1: Compile to object file
    compile_cmd = [
        "clang",
        "-c",
        "-o", obj_path,
        source_file,
        "-fvisibility=default",
        "-fPIC",
    ]

    result = subprocess.run(compile_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR: Compilation failed: {result.stderr}")
        return False

    # Phase 2: Link with force_load for our symbols, then re-export original
    # -force_load ensures our symbols are loaded first and take precedence
    link_cmd = [
        "clang",
        "-dynamiclib",
        "-o", wrapper_path,
        # Force load our object file first - this makes our symbols primary
        f"-Wl,-force_load,{obj_path}",
        # Then re-export the original for all other symbols
        f"-Wl,-reexport_library,{original_path}",
        "-install_name", "@loader_path/libGLESv2.dylib",
        "-framework", "CoreFoundation",
        "-fvisibility=default",
    ]

    result = subprocess.run(link_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR: Linking failed: {result.stderr}")
        # Try fallback without force_load
        print(f"  Trying fallback link...")
        fallback_cmd = [
            "clang",
            "-dynamiclib",
            "-o", wrapper_path,
            obj_path,
            f"-Wl,-reexport_library,{original_path}",
            "-install_name", "@loader_path/libGLESv2.dylib",
            "-framework", "CoreFoundation",
            "-fvisibility=default",
        ]
        result = subprocess.run(fallback_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  ERROR: Fallback linking failed: {result.stderr}")
            return False

    # Clean up object file
    if os.path.exists(obj_path):
        os.remove(obj_path)

    # Fix dependency path to use @loader_path
    subprocess.run([
        "install_name_tool",
        "-change", f"./{config['lib_name_original']}", f"@loader_path/{config['lib_name_original']}",
        wrapper_path
    ], capture_output=True)

    return True


def build_wrapper_linux(lib_dir, config, source_file):
    """Build wrapper on Linux."""
    original_path = os.path.join(lib_dir, config["lib_name_original"])
    wrapper_path = os.path.join(lib_dir, config["lib_name"])

    print(f"  Building wrapper...")

    cmd = [
        "gcc",
        "-shared",
        "-fPIC",
        "-o", wrapper_path,
        source_file,
        "-ldl",
        "-O2",
        f"-Wl,-soname,{config['lib_name']}",
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR: Compilation failed: {result.stderr}")
        return False

    return True


def sign_framework(lib_dir, config):
    """Sign the CEF framework on macOS."""
    if config["system"] != "Darwin":
        return True

    # Find the framework root
    framework_path = lib_dir
    while framework_path and not framework_path.endswith(".framework"):
        framework_path = os.path.dirname(framework_path)

    if not framework_path:
        print(f"  WARNING: Could not find framework path for signing")
        return True

    print(f"  Signing framework: {framework_path}")
    result = subprocess.run([
        "codesign",
        "--force",
        "--deep",
        "--sign", "-",
        framework_path
    ], capture_output=True, text=True)

    if result.returncode != 0:
        print(f"  WARNING: Signing failed: {result.stderr}")
    else:
        print(f"  Framework signed successfully")

    return True


def patch_angle():
    """Main function to patch ANGLE with our wrapper."""
    print("=" * 60)
    print("ANGLE Wrapper Patcher - GPU Virtualization")
    print("=" * 60)

    config = get_cef_paths()
    print(f"\nPlatform: {config['system']}")

    # Use the proper wrapper source file
    source_file = WRAPPER_SOURCE_FILE
    if not os.path.exists(source_file):
        print(f"ERROR: Wrapper source not found: {source_file}")
        return False

    print(f"Using wrapper source: {source_file}")

    try:
        success = True
        for lib_dir in config["lib_dirs"]:
            print(f"\nProcessing: {lib_dir}")

            if not os.path.exists(lib_dir):
                print(f"  Skipping (directory not found)")
                continue

            original_path = os.path.join(lib_dir, config["lib_name"])
            if not os.path.exists(original_path):
                print(f"  Skipping (ANGLE library not found)")
                continue

            # Backup original
            if not backup_original_angle(lib_dir, config):
                success = False
                continue

            # Fix original's self-reference (macOS only)
            if not fix_original_self_reference(lib_dir, config):
                success = False
                continue

            # Build wrapper
            if config["system"] == "Darwin":
                if not build_wrapper_macos(lib_dir, config, source_file):
                    success = False
                    continue
            else:
                if not build_wrapper_linux(lib_dir, config, source_file):
                    success = False
                    continue

            # Sign framework (macOS only)
            sign_framework(lib_dir, config)

            print(f"  ✓ Patched successfully!")

    except Exception as e:
        print(f"ERROR: {e}")
        return False

    if success:
        print("\n" + "=" * 60)
        print("ANGLE patching complete!")
        print("=" * 60)
        print("\nGPU virtualization wrapper installed.")
        print("GPU values will be read from environment variables:")
        print("  OWL_GPU_VENDOR    - GPU vendor string")
        print("  OWL_GPU_RENDERER  - GPU renderer string")
        print("  OWL_GPU_VERSION   - OpenGL version string")
        print("\nThese are set automatically from the VM profile.")
    else:
        print("\nSome patches failed. Check errors above.")

    return success


def unpatch_angle():
    """Restore original ANGLE libraries."""
    print("=" * 60)
    print("Restoring Original ANGLE")
    print("=" * 60)

    config = get_cef_paths()

    for lib_dir in config["lib_dirs"]:
        print(f"\nProcessing: {lib_dir}")

        backup_path = os.path.join(lib_dir, config["lib_name_original"])
        target_path = os.path.join(lib_dir, config["lib_name"])

        if not os.path.exists(backup_path):
            print(f"  No backup found, skipping")
            continue

        print(f"  Restoring from backup...")
        shutil.copy2(backup_path, target_path)

        if config["system"] == "Darwin":
            sign_framework(lib_dir, config)

        print(f"  ✓ Restored successfully!")

    print("\nOriginal ANGLE libraries restored.")
    return True


def copy_to_build(ui_mode=False):
    """Copy patched ANGLE wrapper to the built app bundle and sign."""
    system = platform.system()

    if system != "Darwin":
        print("copy-to-build is only needed on macOS")
        return True

    # Determine app name
    if ui_mode:
        app_name = "Owl Browser.app"
    else:
        app_name = "owl_browser.app"

    print(f"Copying ANGLE wrapper to {app_name}...")

    # Source paths (from third_party where patch_angle puts them)
    # Try Release first, then Debug
    src_base_release = os.path.join(
        PROJECT_ROOT, "third_party", "cef_macos", "Release",
        "Chromium Embedded Framework.framework", "Libraries"
    )
    src_base_debug = os.path.join(
        PROJECT_ROOT, "third_party", "cef_macos", "Debug",
        "Chromium Embedded Framework.framework", "Libraries"
    )

    # Use Release source if available, otherwise Debug
    if os.path.exists(os.path.join(src_base_release, "libGLESv2.dylib")):
        src_wrapper = os.path.join(src_base_release, "libGLESv2.dylib")
        src_original = os.path.join(src_base_release, "libGLESv2_original.dylib")
    else:
        src_wrapper = os.path.join(src_base_debug, "libGLESv2.dylib")
        src_original = os.path.join(src_base_debug, "libGLESv2_original.dylib")

    # Destination paths (in app bundle) - check Release first, then Debug
    dest_libs_release = os.path.join(
        PROJECT_ROOT, "build", "Release", app_name, "Contents", "Frameworks",
        "Chromium Embedded Framework.framework", "Versions", "A", "Libraries"
    )
    dest_libs_debug = os.path.join(
        PROJECT_ROOT, "build", "Debug", app_name, "Contents", "Frameworks",
        "Chromium Embedded Framework.framework", "Libraries"
    )

    # Determine which build folder exists
    if os.path.exists(dest_libs_release):
        dest_libs = dest_libs_release
        dest_framework = os.path.join(
            PROJECT_ROOT, "build", "Release", app_name, "Contents", "Frameworks",
            "Chromium Embedded Framework.framework"
        )
    elif os.path.exists(dest_libs_debug):
        dest_libs = dest_libs_debug
        dest_framework = os.path.join(
            PROJECT_ROOT, "build", "Debug", app_name, "Contents", "Frameworks",
            "Chromium Embedded Framework.framework"
        )
    else:
        dest_libs = None
        dest_framework = None

    if not os.path.exists(src_wrapper):
        print(f"  ERROR: ANGLE wrapper not found at {src_wrapper}")
        print("  Run 'npm run patch:angle' first to build the wrapper")
        return False

    if dest_libs is None or not os.path.exists(dest_libs):
        print(f"  ERROR: Destination not found at {dest_libs_release}")
        print(f"         or at {dest_libs_debug}")
        print("  Build the app first with 'npm run build' or 'npm run rebuild'")
        return False

    # Copy wrapper
    print(f"  Copying libGLESv2.dylib...")
    shutil.copy2(src_wrapper, os.path.join(dest_libs, "libGLESv2.dylib"))

    # Copy original if exists
    if os.path.exists(src_original):
        print(f"  Copying libGLESv2_original.dylib...")
        shutil.copy2(src_original, os.path.join(dest_libs, "libGLESv2_original.dylib"))

    # Sign the CEF framework
    print(f"  Signing CEF framework...")
    result = subprocess.run(
        ["codesign", "--force", "--sign", "-", dest_framework],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        print(f"  WARNING: Signing failed: {result.stderr}")
    else:
        print(f"  ✓ ANGLE wrapper installed and CEF framework signed")

    return True


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="ANGLE Wrapper Patcher")
    parser.add_argument("--unpatch", action="store_true", help="Restore original ANGLE libraries")
    parser.add_argument("--copy-to-build", action="store_true", help="Copy wrapper to built app bundle")
    parser.add_argument("--ui", action="store_true", help="Target UI app bundle (with --copy-to-build)")
    args = parser.parse_args()

    if args.unpatch:
        sys.exit(0 if unpatch_angle() else 1)
    elif args.copy_to_build:
        sys.exit(0 if copy_to_build(ui_mode=args.ui) else 1)
    else:
        sys.exit(0 if patch_angle() else 1)
