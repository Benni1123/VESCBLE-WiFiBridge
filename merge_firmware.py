Import("env")
import os
import subprocess

def merge_firmware(source, target, env):
    build_dir   = env.subst("$BUILD_DIR")
    pkg_dir     = env.subst("$PROJECT_PACKAGES_DIR")

    bootloader  = os.path.join(build_dir, "bootloader.bin")
    partitions  = os.path.join(build_dir, "partitions.bin")
    boot_app0   = os.path.join(pkg_dir, "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
    firmware    = os.path.join(build_dir, "firmware.bin")
    merged      = os.path.join(build_dir, "firmware_full.bin")

    if not os.path.exists(bootloader):
        print(">>> merge_firmware: bootloader.bin not found, skipping merge")
        return
    if not os.path.exists(boot_app0):
        print(f">>> merge_firmware: boot_app0.bin not found at {boot_app0}, skipping merge")
        return

    import sys

    # Try PlatformIO's bundled esptool first
    esptool_path = os.path.join(
        env.subst("$PROJECT_PACKAGES_DIR"),
        "tool-esptoolpy", "esptool.py"
    )
    if os.path.exists(esptool_path):
        esptool_cmd = [sys.executable, esptool_path]
    else:
        esptool_cmd = [sys.executable, "-m", "esptool"]

    cmd = esptool_cmd + [
        "--chip", "esp32s3",
        "merge_bin",
        "-o", merged,
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "4MB",
        "0x0",     bootloader,
        "0x8000",  partitions,
        "0xe000",  boot_app0,
        "0x10000", firmware,
    ]

    print(f"\n>>> Merging firmware to {merged}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            size = os.path.getsize(merged) / 1024
            print(f">>> firmware_full.bin created ({size:.1f} KB)\n")
        else:
            print(f">>> merge failed: {result.stderr}")
    except Exception as e:
        print(f">>> merge failed: {e}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
