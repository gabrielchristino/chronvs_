/* Optional host reference: download upstream miniz v1.14 into the ignored
 * research directory. Production uses the ESP32-S3 ROM implementation. */
#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS
#include "../../.pio/host-tests/agnss-research/miniz-host.c"
