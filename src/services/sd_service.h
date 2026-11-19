#pragma once
#include <stdbool.h>

#ifndef CHRONVS_RUN_DIRECTORY
#define CHRONVS_RUN_DIRECTORY "/sdcard/chronvs/runs"
#endif
/* UI task only. Mount existing FAT; never format or delete existing files. */
bool chronvs_sd_mount(void);
