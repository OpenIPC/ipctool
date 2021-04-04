#include <math.h>

#include "chipid.h"
#include "hal_common.h"
#include "hwinfo.h"

float gethwtemp() {
    getchipid();
    if (!hal_temperature)
        return NAN;
    return hal_temperature();
}
