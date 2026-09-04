#include "services/battery_service.h"

#include <stddef.h>

#include "BAT_Driver.h"

typedef struct {
    float voltage;
    uint8_t percent;
} battery_level_t;

static uint8_t voltage_to_percent(float voltage) {
    static const battery_level_t levels[] = {
        {3.30f, 0}, {3.45f, 3}, {3.60f, 10}, {3.70f, 25},
        {3.80f, 45}, {3.90f, 65}, {4.00f, 80}, {4.10f, 90},
        {4.20f, 100},
    };
    const size_t count = sizeof(levels) / sizeof(levels[0]);

    if (voltage <= levels[0].voltage) return levels[0].percent;
    if (voltage >= levels[count - 1].voltage) return levels[count - 1].percent;

    for (size_t index = 1; index < count; ++index) {
        if (voltage <= levels[index].voltage) {
            const battery_level_t low = levels[index - 1];
            const battery_level_t high = levels[index];
            const float position = (voltage - low.voltage) /
                                   (high.voltage - low.voltage);
            return (uint8_t)(low.percent +
                position * (float)(high.percent - low.percent) + 0.5f);
        }
    }
    return 0;
}

void chronvs_battery_init(void) {
    BAT_Init();
}

chronvs_battery_status_t chronvs_battery_read(void) {
    chronvs_battery_status_t status = {0};
    float total_voltage = 0.0f;
    unsigned valid_samples = 0;

    for (unsigned sample = 0; sample < 8; ++sample) {
        const float voltage = BAT_Get_Volts();
        if (voltage >= 2.5f && voltage <= 5.0f) {
            total_voltage += voltage;
            ++valid_samples;
        }
    }
    if (valid_samples == 0) return status;

    status.voltage = total_voltage / valid_samples;
    status.percent = voltage_to_percent(status.voltage);
    status.valid = true;
    return status;
}
