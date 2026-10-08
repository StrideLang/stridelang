#include <stdio.h>
#include <stdbool.h>

#include "dsp.h"

int main(void) {
    SensorDSP_init();

    double rawSensorValue = 10.0;
    double alertThreshold = 5.0;
    double filteredValue = 0.0;
    bool alertTriggered = false;

    SensorDSP_process(&rawSensorValue, &alertThreshold, &filteredValue, &alertTriggered);

    printf("Filtered: %f, Alert: %d\n", filteredValue, (int)alertTriggered);
    return 0;
}
