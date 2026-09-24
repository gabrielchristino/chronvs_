Import("env")

# The pre-script registers vendor sources before PlatformIO configures Xtensa
# and ESP-IDF includes. Synchronize the isolated environment only afterwards.
lvgl_env = env.get("CHRONVS_LVGL_ENV")
if lvgl_env is not None:
    lvgl_env.Replace(**{
        key: value for key, value in env.Dictionary().items()
        if key != "CHRONVS_LVGL_ENV"
    })
    lvgl_env.ProcessUnFlags("-O0 -Os -O1 -O2 -O3 -Og -Ofast")
    lvgl_env.Append(CCFLAGS=["-O2"])
