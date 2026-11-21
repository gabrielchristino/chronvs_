"""TailTiny network/crypto settings, scoped to the dedicated environment."""
Import("env")
from pathlib import Path
import re

if env.subst("$PIOENV") != "tailtiny":
    raise RuntimeError("TailTiny configuration is restricted to env:tailtiny")
config = Path(env["PROJECT_DIR"]) / "sdkconfig.tailtiny"
if not config.exists():
    reference = config.with_name("sdkconfig.waveshare_esp32_s3_touch_lcd_146")
    if not reference.is_file():
        raise RuntimeError("Build the default environment before TailTiny")
    config.write_text(reference.read_text(encoding="utf-8"), encoding="utf-8")
settings = {
    "LWIP_MAX_SOCKETS": "16",
    "LWIP_SO_RCVBUF": "y",
    "MBEDTLS_CHACHA20_C": "y",
    "MBEDTLS_POLY1305_C": "y",
    "MBEDTLS_CHACHAPOLY_C": "y",
    "MBEDTLS_HKDF_C": "y",
    "MBEDTLS_DYNAMIC_BUFFER": "y",
    "MBEDTLS_CERTIFICATE_BUNDLE": "y",
    "ML_DERP_VERIFY_CERT": "y",
    "ML_MAX_PEERS": "8",
    "ML_H2_BUFFER_SIZE_KB": "128",
    "ML_JSON_BUFFER_SIZE_KB": "128",
    "ML_ENABLE_CONFIG_HTTPD": None,
    "ML_ENABLE_CELLULAR": None,
    "ML_TASK_DERP_CORE": "1",
}
original = config.read_text(encoding="utf-8")
lines = []
seen = set()
for line in original.splitlines():
    match = re.match(r"(?:# )?CONFIG_(\w+)(?:=| is not set)", line)
    if match and match[1] in settings:
        key = match[1]
        seen.add(key)
        value = settings[key]
        line = f"CONFIG_{key}={value}" if value is not None else f"# CONFIG_{key} is not set"
    lines.append(line)
for key, value in settings.items():
    if key not in seen:
        lines.append(f"CONFIG_{key}={value}" if value is not None else f"# CONFIG_{key} is not set")
updated = "\n".join(lines) + "\n"
if updated != original:
    config.write_text(updated, encoding="utf-8")
