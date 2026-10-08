Import("env")

from os.path import join
import os
import runpy

# PlatformIO's ESP-IDF uploader assumes the usual partition-table offset 0x8000.
# This board needs 0x10000 because its ESP-IDF bootloader is larger. Make the
# regular Upload button use the same known-good map used during bring-up.
esptool = join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
build_dir = env.subst("$BUILD_DIR")
reference = os.environ.get("CHRONVS_FIRMWARE_REFERENCE")
if reference:
    # Verify every archived artifact before replacing the upload paths.
    helper = runpy.run_path(join(env.subst("$PROJECT_DIR"), "scripts", "firmware_reference.py"))
    build_dir = str(helper["verify"](reference))

env.Replace(
    UPLOADER=esptool,
    UPLOADERFLAGS=[
        "--chip", "esp32s3",
        "--port", '"$UPLOAD_PORT"',
        "--baud", "$UPLOAD_SPEED",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash", "-z",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0", join(build_dir, "bootloader.bin"),
        "0x10000", join(build_dir, "partitions.bin"),
        "0x20000", join(build_dir, "firmware.bin"),
        "0x420000", join(build_dir, "srmodels", "srmodels.bin"),
    ],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
)
