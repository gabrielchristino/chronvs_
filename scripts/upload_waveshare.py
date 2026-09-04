Import("env")

from os.path import join

# PlatformIO's ESP-IDF uploader assumes the usual partition-table offset 0x8000.
# This board needs 0x10000 because its ESP-IDF bootloader is larger. Make the
# regular Upload button use the same known-good map used during bring-up.
esptool = join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
build_dir = env.subst("$BUILD_DIR")

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
    ],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
)
