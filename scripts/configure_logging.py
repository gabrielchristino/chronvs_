Import("env")
from pathlib import Path
import re

# sdkconfig.defaults does not override an existing menuconfig. Apply only the
# firmware policy, preserving the local hardware and memory choices.
diagnostics = env.GetProjectOption("custom_serial_diagnostics", "no")
data_cache_line = env.GetProjectOption("custom_data_cache_line", "reference")
if data_cache_line not in ("reference", "32", "64"):
    raise RuntimeError("custom_data_cache_line must be reference, 32 or 64")
instruction_cache = env.GetProjectOption("custom_instruction_cache", "reference")
if instruction_cache not in ("reference", "16", "32"):
    raise RuntimeError("custom_instruction_cache must be reference, 16 or 32")
if diagnostics not in ("yes", "no", "capture", "usb"):
    raise RuntimeError("custom_serial_diagnostics must be yes, no, capture or usb")
# Capture keeps stdout but suppresses unrelated task logs inside PCM records.
enabled = diagnostics != "no"
logs_enabled = diagnostics in ("yes", "usb")
config = Path(env["PROJECT_DIR"]) / ("sdkconfig." + env.subst("$PIOENV"))
reference = env.GetProjectOption("custom_sdkconfig_reference", "")
source = Path(env["PROJECT_DIR"]) / reference if reference else config
if reference and not source.is_file():
    raise RuntimeError("Build the default environment before display diagnostics")
if source.exists():
    settings = {}
    # Enable the BLE stack only in the explicit headless radio bench.
    if env.GetProjectOption("custom_bluetooth_diagnostic", "no") == "yes":
        settings.update(BT_ENABLED="y", BT_CONTROLLER_ENABLED="y",
                        BT_BLUEDROID_ENABLED="y", BT_NIMBLE_ENABLED=None,
                        BT_BLE_ENABLED="y", BT_BLE_42_FEATURES_SUPPORTED="y",
                        BT_CLASSIC_ENABLED=None)
    # Explicit A/B only: change line width without reserving more internal RAM
    # for the cache or changing PSRAM/flash clocks, display buffers or DMA.
    if data_cache_line != "reference":
        for choice in ("16", "32", "64"):
            settings["ESP32S3_DATA_CACHE_LINE_" + choice + "B"] = (
                "y" if choice == data_cache_line else None)
        settings["ESP32S3_DATA_CACHE_LINE_SIZE"] = data_cache_line
    if instruction_cache != "reference":
        for choice in ("16", "32"):
            settings["ESP32S3_INSTRUCTION_CACHE_" + choice + "KB"] = (
                "y" if choice == instruction_cache else None)
        settings["ESP32S3_INSTRUCTION_CACHE_SIZE"] = hex(int(instruction_cache) * 1024)
    for prefix in ("LOG_DEFAULT_LEVEL", "BOOTLOADER_LOG_LEVEL", "LOG_BOOTLOADER_LEVEL"):
        for level in ("NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"):
            settings[prefix + "_" + level] = "y" if level == ("INFO" if logs_enabled else "NONE") else None
        settings[prefix] = "3" if logs_enabled else "0"
    settings["LOG_MAXIMUM_EQUALS_DEFAULT"] = "y"
    for level in ("ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"):
        settings["LOG_MAXIMUM_LEVEL_" + level] = None
    settings["LOG_MAXIMUM_LEVEL"] = "3" if logs_enabled else "0"
    console = "USB_SERIAL_JTAG" if diagnostics == "usb" else "UART_DEFAULT" if enabled else "NONE"
    for choice in ("UART_DEFAULT", "USB_CDC", "USB_SERIAL_JTAG", "UART_CUSTOM", "NONE"):
        settings["ESP_CONSOLE_" + choice] = "y" if choice == console else None
    uart_console = enabled and diagnostics != "usb"
    settings["ESP_CONSOLE_UART_NONE"] = None if uart_console else "y"
    settings["ESP_CONSOLE_SECONDARY_NONE"] = None if uart_console else "y"
    settings["ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG"] = "y" if uart_console else None

    # Select IDF's own optimization too: build_flags alone do not replace
    # the -Og selected by an existing sdkconfig for framework components.
    release = env.GetProjectOption("build_type", "release") == "release"
    for choice in ("DEBUG", "SIZE", "PERF", "NONE"):
        settings["COMPILER_OPTIMIZATION_" + choice] = (
            "y" if choice == ("PERF" if release else "DEBUG") else None)
    for choice in ("ENABLE", "DISABLE", "SILENT"):
        settings["COMPILER_OPTIMIZATION_ASSERTIONS_" + choice] = (
            "y" if choice == ("SILENT" if release else "ENABLE") else None)
    settings["ESP_DEBUG_OCDAWARE"] = None if release else "y"
    settings["FREERTOS_DEBUG_OCDAWARE"] = None if release else "y"
    for choice in ("PRINT_REBOOT", "PRINT_HALT", "GDBSTUB", "SILENT_REBOOT"):
        settings["ESP_SYSTEM_PANIC_" + choice] = (
            "y" if choice == ("SILENT_REBOOT" if release else "PRINT_REBOOT") else None)
    for choice in ("ALWAYS_ON", "ALWAYS_OFF", "ON_GPIO_HIGH", "ON_GPIO_LOW"):
        settings["BOOT_ROM_LOG_" + choice] = (
            "y" if choice == ("ALWAYS_OFF" if release else "ALWAYS_ON") else None)

    original = config.read_text(encoding="utf-8") if config.exists() else ""
    source_text = source.read_text(encoding="utf-8")
    lines = []
    seen = set()
    for line in source_text.splitlines():
        match = re.match(r"(?:# )?CONFIG_(\w+)(?:=| is not set)", line)
        if match and match.group(1) in settings:
            key = match.group(1)
            seen.add(key)
            value = settings[key]
            lines.append("CONFIG_" + key + "=" + value if value is not None else
                         "# CONFIG_" + key + " is not set")
        else:
            lines.append(line)
    lines.extend("CONFIG_" + key + "=" + value if value is not None else
                 "# CONFIG_" + key + " is not set"
                 for key, value in settings.items() if key not in seen)
    updated = "\n".join(lines) + "\n"
    if updated != original:
        config.write_text(updated, encoding="utf-8")
