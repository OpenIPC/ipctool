#include <math.h>

#include "chipid.h"
#include "hal/common.h"
#include "hwinfo.h"

float gethwtemp() {
    getchipname();
    if (!hal_temperature)
        return NAN;
    return hal_temperature();
}
