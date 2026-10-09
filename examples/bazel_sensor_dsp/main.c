#include <stdio.h>
#include <stdbool.h>

#include "sensor_dsp.h"

int main(void) {
    SensorDSP_init();

    double rawSensor = 10.0;
    double threshold = 5.0;
    double filtered = 0.0;
    bool alert = false;

    printf("Starting Stride DSP Pipeline in Bazel...\n");

    for (int i = 0; i < 5; ++i) {
        SensorDSP_process(&rawSensor, &threshold, &filtered, &alert);
        printf("Sample %d: Raw=%.1f -> Filtered=%.3f, Alert=%s\n",
               i + 1, rawSensor, filtered, alert ? "TRIGGERED" : "NORMAL");
    }

    return 0;
}
