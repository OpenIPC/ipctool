/* SPDX-License-Identifier: MIT */
#include "nt98513_sensor.h"

enum nt98513_control_status nt98513_sensor_active_parse(
    const void *reply, size_t reply_size, struct nt98513_sensor_active *out)
{
    static const struct canonical {
        const char *bytes;
        size_t size;
        enum nt98513_sensor_selection selection;
    } accepted[] = {
        { "0 CMOS_OFF success", sizeof("0 CMOS_OFF success"), NT98513_SENSOR_OFF },
        { "0 CMOS_OS05A10M success", sizeof("0 CMOS_OS05A10M success"), NT98513_SENSOR_OS05A10M },
        { "0 CMOS_GC2053M success", sizeof("0 CMOS_GC2053M success"), NT98513_SENSOR_GC2053M },
        { "0 CMOS_JXK02M success", sizeof("0 CMOS_JXK02M success"), NT98513_SENSOR_JXK02M },
        { "0 CMOS_JXK03M success", sizeof("0 CMOS_JXK03M success"), NT98513_SENSOR_JXK03M }
    };
    const unsigned char *bytes = reply;
    size_t candidate, i;

    if (reply == NULL || out == NULL) return NT98513_CONTROL_ARGUMENT;
    if (reply_size == 0u || reply_size > 256u) return NT98513_CONTROL_SIZE_ERROR;
    for (candidate = 0; candidate < sizeof accepted / sizeof accepted[0]; ++candidate) {
        if (reply_size != accepted[candidate].size) continue;
        for (i = 0; i < reply_size; ++i)
            if (bytes[i] != (unsigned char)accepted[candidate].bytes[i]) break;
        if (i == reply_size) {
            out->logical_index = 0;
            out->selection = accepted[candidate].selection;
            return NT98513_CONTROL_OK;
        }
    }
    return NT98513_CONTROL_RANGE;
}
