#include "remote_display.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const char *const system_names[] = {"INI", "STOP", "WDG", "ZERO", "HOLD", "WAIT", "RDY",
    "RUN", "DEC", "EST", "FLT", "DEG", "RO"};
static const char *const mode_names[] = {"NONE", "PUL", "SPD", "POS", "RC", "STOP", "ZERO", "HOLD", "WDG"};

static void line(remote_display_t *display, uint8_t row, const char *format, ...)
{
    char text[96];
    int count;
    va_list args;
    va_start(args, format);
    count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count > (int)REMOTE_DISPLAY_COLUMNS && count < (int)sizeof(text)) {
        size_t source, target = 0U;
        for (source = 0U; source < (size_t)count; source++)
            if (text[source] != ' ') text[target++] = text[source];
        text[target] = '\0';
        count = (int)target;
        while (count > (int)REMOTE_DISPLAY_COLUMNS) {
            size_t begin = 0U, longest = 0U;
            for (source = 0U; source < (size_t)count;) {
                size_t start = source;
                if ((text[source] >= '0' && text[source] <= '9') || text[source] == '-') {
                    source++;
                    while ((text[source] >= '0' && text[source] <= '9') || text[source] == '.') source++;
                    if (source - start > longest) { begin = start; longest = source - start; }
                } else source++;
            }
            if (longest <= 3U) break;
            memmove(text + begin + 3U, text + begin + longest, strlen(text + begin + longest) + 1U);
            memcpy(text + begin, "OVF", 3U);
            count = (int)strlen(text);
        }
    }
    if (count < 0 || count > (int)REMOTE_DISPLAY_COLUMNS) {
        display->truncated = 1U;
        strcpy(display->lines[row], "OVF");
    } else memcpy(display->lines[row], text, (size_t)count + 1U);
}

static void region(remote_display_t *display, uint8_t row, uint8_t x, uint8_t width)
{
    remote_display_region_t *area;
    if (display->region_count >= REMOTE_DISPLAY_REGIONS) return;
    area = &display->regions[display->region_count++];
    area->row = row; area->x = x; area->width = width;
}

static uint8_t selected(const remote_status_model_t *model, remote_history_field_t field)
{
    uint8_t axis = remote_history_axis(field);
    if (axis) return remote_status_axis(model, (uint8_t)(axis - 1U));
    if (field == RH_DIFF || field == RH_AVERAGE ||
        (field >= RH_DELTA && field <= RH_CORRECTION) || field == RH_PAIR)
        return (uint8_t)(!model->received || (model->latest.axis_flags & 3U) == 3U);
    return 1U;
}

static uint8_t use(remote_display_t *display, const remote_status_model_t *model,
                    remote_history_field_t field, uint32_t now)
{
    const remote_history_value_t *value = &model->history[field];
    if (!selected(model, field)) return 0U;
    if (value->received) display->quality_any = 1U;
    if (!remote_history_fresh(model, field, now)) display->quality_old = 1U;
    return value->received;
}

static void format_number(char text[16], int32_t value, uint8_t decimal)
{
    uint32_t magnitude;
    if (!decimal) {
        (void)snprintf(text, 16U, "%ld", (long)value);
        return;
    }
    magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    (void)snprintf(text, 16U, "%s%lu.%lu", value < 0 ? "-" : "",
        (unsigned long)(magnitude / 10U), (unsigned long)(magnitude % 10U));
}

static uint8_t not_applicable(remote_display_t *display, const remote_status_model_t *model,
                              remote_history_field_t field, uint32_t now)
{
    if (field == RH_DIFF && remote_history_fresh(model, RH_AUTH, now) &&
        !(model->history[RH_AUTH].value & 2U)) { (void)use(display, model, RH_AUTH, now); return 1U; }
    if (field >= RH_DELTA && field <= RH_SYNC_AGE2 &&
        remote_history_fresh(model, RH_SYNC_FLAGS, now) &&
        !(model->history[RH_SYNC_FLAGS].value & 2U)) {
        (void)use(display, model, RH_SYNC_FLAGS, now); return 1U;
    }
    return 0U;
}
static void mark_old(const remote_status_model_t *model, remote_history_field_t field,
                     uint32_t now, char text[16])
{
    size_t length = strlen(text);
    if (length < 14U && (!remote_history_fresh(model, field, now) ||
        remote_status_link(model, now) != REMOTE_RETURN_ONLINE)) {
        text[length] = '~'; text[length + 1U] = '\0';
    }
}
static void number(remote_display_t *display, const remote_status_model_t *model,
                    remote_history_field_t field, uint32_t now, uint8_t decimal,
                    char text[16])
{
    if (!selected(model, field)) strcpy(text, "NC");
    else if (not_applicable(display, model, field, now)) strcpy(text, "N/A");
    else if (!use(display, model, field, now)) strcpy(text, "--");
    else { format_number(text, model->history[field].value, decimal); mark_old(model, field, now, text); }
}

static void velocity(remote_display_t *display, const remote_status_model_t *model,
                      remote_history_field_t field, uint32_t now, char text[16])
{
    int32_t value, tenths;
    if (!selected(model, field)) { strcpy(text, "NC"); return; }
    if (!use(display, model, field, now)) { strcpy(text, "--"); return; }
    value = model->history[field].value * 9;
    tenths = value < 0 ? -((-value + 125) / 250) : (value + 125) / 250;
    format_number(text, tenths, 1U);
    mark_old(model, field, now, text);
}

static void sample_age(remote_display_t *display, const remote_status_model_t *model,
                         remote_history_field_t field, uint32_t now, char text[16])
{
    uint32_t elapsed, age;
    number(display, model, field, now, 0U, text);
    if (!selected(model, field) || not_applicable(display, model, field, now) || !model->history[field].received) return;
    elapsed = now - model->history[field].updated_ms;
    age = (uint32_t)model->history[field].value;
    if (elapsed > UINT32_MAX - age) strcpy(text, "OVF");
    else (void)snprintf(text, 16U, "%lu", (unsigned long)(elapsed + age));
    mark_old(model, field, now, text);
}

static const char *flag(remote_display_t *display, const remote_status_model_t *model,
                         remote_history_field_t field, uint8_t mask, uint32_t now)
{
    if (!selected(model, field)) return "NC";
    if (!use(display, model, field, now)) return "--";
    return model->history[field].value & mask ? "1" : "0";
}

static const char *name(remote_display_t *display, const remote_status_model_t *model,
                         remote_history_field_t field, const char *const *names,
                         uint8_t count, uint32_t now)
{
    int32_t value = model->history[field].value;
    if (!use(display, model, field, now)) return "UNK";
    return value >= 0 && value < count ? names[value] : "UNK";
}

static void pair(remote_display_t *display, uint8_t row, const char *label,
                  char first[16], char second[16], const char *unit)
{
    size_t length = strlen(label) + strlen(first) + strlen(second) + strlen(unit) + 3U;
    if (strlen(first) <= 6U && strlen(second) <= 6U && strlen(unit) <= 4U)
        line(display, row, "%-2s %6s/%6s %s", label, first, second, unit);
    else if (length <= REMOTE_DISPLAY_COLUMNS)
        line(display, row, "%s %s/%s %s", label, first, second, unit);
    else {
        length -= 2U;
        if (length > REMOTE_DISPLAY_COLUMNS) {
            if (strlen(first) >= strlen(second)) strcpy(first, "OVF");
            else strcpy(second, "OVF");
            if (strlen(label) + strlen(first) + strlen(second) + strlen(unit) + 1U >
                REMOTE_DISPLAY_COLUMNS) { strcpy(first, "OVF"); strcpy(second, "OVF"); }
        }
        line(display, row, "%s%s/%s%s", label, first, second, unit);
    }
}

static void pair_fields(remote_display_t *display, const remote_status_model_t *model,
                         uint32_t now, uint8_t row, const char *label,
                         remote_history_field_t first_field, uint8_t decimal,
                         const char *unit)
{
    char first[16], second[16];
    number(display, model, first_field, now, decimal, first);
    number(display, model, (remote_history_field_t)(first_field + 1), now, decimal, second);
    pair(display, row, label, first, second, unit);
}

static void home(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char first[16], second[16], distance[16];
    uint8_t axis;
    line(display, 1U, "S:%s M:%s", name(display, model, RH_SYSTEM, system_names, 13U, now),
        name(display, model, RH_MODE, mode_names, 9U, now));
    if (remote_status_fresh(model, 0x8AU, now) && (now / 1000U) % 2U == 0U) {
        static const char *const owners[] = {"--", "HMI", "RC", "AUTO", "CLI", "CAN"};
        const remote_snapshot_metadata_t *metadata = &model->slot[9].frame.data.metadata;
        if (metadata->recovery_state != 0U && metadata->recovery_state != 6U)
            line(display, 1U, "REC:%s %u%%", metadata->recovery_eligible ? "READY" : "WAIT",
                (unsigned)(metadata->recovery_healthy_ms > 1000U ? 100U : metadata->recovery_healthy_ms / 10U));
        else
            line(display, 1U, "S:%s SRC:%s", name(display, model, RH_SYSTEM, system_names, 13U, now),
                owners[metadata->owner <= 5U ? metadata->owner : 0U]);
    }
    {
        const remote_status_slot_t *slot = &model->slot[7];
        const remote_vehicle_speed_t *v = &slot->frame.data.vehicle_speed;
        static const char *const source[] = {"JOY", "KEY", "HMI", "CAN", "CLI"};
        uint8_t old = !remote_status_fresh(model, 0x88U, now) || !display->radio_live;
        if (!slot->received || !v->control_extended) line(display, 2U, "T:--km/h UNK");
        else {
            int32_t magnitude = v->common_centi_kmh;
            char target[12];
            if (magnitude == 32767) strcpy(target, "--");
            else {
                if (magnitude < 0) magnitude = -magnitude;
                (void)snprintf(target, sizeof(target), "%s%ld.%02ld",
                    v->common_centi_kmh < 0 ? "-" : "", (long)(magnitude / 100), (long)(magnitude % 100));
            }
            (void)use(display, model, RH_VTARGET, now);
            line(display, 2U, "T:%skm/h %s%s%s", target,
                source[v->control_source <= 4U ? v->control_source : 4U],
                v->exit_ready ? " R" : v->exit_pending ? " W" : "", old ? "~" : "");
        }
    }
    velocity(display, model, RH_FRONT, now, first);
    velocity(display, model, RH_REAR, now, second);
    pair(display, 3U, "VA", first, second, "km/h");
    pair_fields(display, model, now, 4U, "RA", RH_RPM1, 0U, "rpm");
    pair_fields(display, model, now, 5U, "U", RH_VOLTAGE1, 1U, "V");
    pair_fields(display, model, now, 6U, "I", RH_CURRENT1, 1U, "A");
    for (axis = 0U; axis < 2U; axis++) {
        remote_history_field_t field = (remote_history_field_t)(RH_LOAD1 + axis);
        char *text = axis ? second : first;
        number(display, model, field, now, 0U, text);
        if (selected(model, field) && model->history[field].received) {
            format_number(text, (model->history[field].value + 5) / 10, 0U);
            mark_old(model, field, now, text);
        }
    }
    number(display, model, RH_BOOT, now, 1U, distance);
    (void)use(display, model, RH_INCOMPLETE, now);
    line(display, 7U, "L%s/%s%% D%sm%s", first, second, distance,
        model->history[RH_INCOMPLETE].received && model->history[RH_INCOMPLETE].value ? "!" : "");
}

static void overview(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char first[16], second[16];
    uint8_t axis;
    number(display, model, RH_CMD1, now, 0U, first);
    number(display, model, RH_CMD2, now, 0U, second);
    pair(display, 1U, "RT", first, second, "rpm");
    line(display, 2U, "RC:%s AU:%s GT:%s", flag(display, model, RH_AUTH, 1U, now),
        flag(display, model, RH_AUTH, 2U, now), flag(display, model, RH_AUTH, 4U, now));
    line(display, 3U, "ES:%s RD:%s EN:%s/%s", flag(display, model, RH_AUTH, 16U, now),
        flag(display, model, RH_AUTH, 8U, now), flag(display, model, RH_ENABLE1, 1U, now),
        flag(display, model, RH_ENABLE2, 1U, now));
    number(display, model, RH_TARGET, now, 0U, first);
    number(display, model, RH_BASE, now, 0U, second);
    line(display, 4U, "T:%s B:%s rpm", first, second);
    for (axis = 0U; axis < 2U; axis++) {
        number(display, model, (remote_history_field_t)(RH_CMD1 + axis), now, 0U, first);
        number(display, model, (remote_history_field_t)(RH_FAST1 + axis), now, 0U, second);
        line(display, (uint8_t)(5U + axis), "A%u C:%s V:%s", (unsigned)(axis + 1U), first, second);
    }
    number(display, model, RH_COMM, now, 0U, first);
    if (model->history[RH_COMM].received && model->history[RH_COMM].value > 4) strcpy(first, "UNK");
    if (model->history[RH_SYSTEM].received)
        (void)snprintf(second, sizeof(second), "%lu", (unsigned long)(now - model->history[RH_SYSTEM].updated_ms));
    else strcpy(second, "--");
    line(display, 7U, "COM:%s AGE:%sms", first, second);
    if (remote_status_fresh(model, 0x8AU, now) && (now / 2000U) % 2U == 0U)
        line(display, 7U, "SNAP:%lu", (unsigned long)model->slot[9].frame.data.metadata.sequence);
}

static void servo(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char first[16], second[16];
    uint8_t axis, row;
    for (axis = 0U; axis < 2U; axis++) {
        remote_history_field_t state = (remote_history_field_t)(RH_STATE1 + axis);
        remote_history_field_t fault = (remote_history_field_t)(RH_FAULT1 + axis);
        row = (uint8_t)(1U + axis * 3U);
        line(display, row, "A%u ON:%s EN:%s G:%s", (unsigned)(axis + 1U),
            flag(display, model, state, 1U, now),
            flag(display, model, (remote_history_field_t)(RH_ENABLE1 + axis), 1U, now),
            flag(display, model, state, 128U, now));
        number(display, model, (remote_history_field_t)(RH_RPM1 + axis), now, 0U, first);
        number(display, model, fault, now, 0U, second);
        if (selected(model, fault) && model->history[fault].received)
            (void)snprintf(second, sizeof(second), "%04X", (unsigned)model->history[fault].value);
        line(display, (uint8_t)(row + 1U), "V:%s F:%s", first, second);
        sample_age(display, model, (remote_history_field_t)(RH_AGE1 + axis), now, first);
        number(display, model, (remote_history_field_t)(RH_FAIL1 + axis), now, 0U, second);
        line(display, (uint8_t)(row + 2U), "AGE:%s FC:%s", first, second);
    }
    line(display, 7U, "Z:%s/%s P:%s/%s", flag(display, model, RH_ZERO1, 1U, now),
        flag(display, model, RH_ZERO2, 1U, now), flag(display, model, RH_STATE1, 64U, now),
        flag(display, model, RH_STATE2, 64U, now));
}

static void drive(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char first[16], second[16];
    uint8_t axis, row;
    for (axis = 0U; axis < 2U; axis++) {
        row = (uint8_t)(1U + axis * 2U);
        number(display, model, (remote_history_field_t)(RH_TORQUE1 + axis), now, 1U, first);
        number(display, model, (remote_history_field_t)(RH_LOAD1 + axis), now, 1U, second);
        line(display, row, "A%u T:%s%% L:%s%%", (unsigned)(axis + 1U), first, second);
        number(display, model, (remote_history_field_t)(RH_CURRENT1 + axis), now, 1U, first);
        number(display, model, (remote_history_field_t)(RH_VOLTAGE1 + axis), now, 1U, second);
        line(display, (uint8_t)(row + 1U), strlen(first) + strlen(second) > 11U ?
            "A%u I:%sAU:%sV" : "A%u I:%sA U:%sV", (unsigned)(axis + 1U), first, second);
    }
    if (model->diagnostic_cache.received) line(display, 5U, "RXAGE:%lums",
        (unsigned long)(now - model->diagnostic_cache.received_ms));
    else line(display, 5U, "RXAGE:--ms");
    line(display, 6U, "S:%s %s", !model->received ? "--" :
        ((model->latest.axis_flags & 3U) == 3U ? "A1/A2" :
        (model->latest.axis_flags & 1U ? "A1" : (model->latest.axis_flags & 2U ? "A2" : "NC"))),
        !display->radio_live && model->received ? "LOST" :
        (!display->quality_any ? "WAIT" : (display->quality_old ? "OLD" : "LAST")));
    line(display, 7U, "T/L:%% I:A U:V");
}

static void vehicle(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char first[16], second[16], third[16];
    velocity(display, model, RH_VTARGET, now, first);
    velocity(display, model, RH_VCOMMAND, now, second);
    line(display, 1U, "T:%s C:%s km/h", first, second);
    velocity(display, model, RH_FRONT, now, first);
    velocity(display, model, RH_REAR, now, second);
    line(display, 2U, "F:%s R:%s km/h", first, second);
    velocity(display, model, RH_AVERAGE, now, first);
    line(display, 3U, "AVG:%s km/h", first);
    sample_age(display, model, RH_FRONT_AGE, now, first);
    sample_age(display, model, RH_REAR_AGE, now, second);
    if (strlen(first) + strlen(second) > 13U) {
        if (strlen(first) > 6U) strcpy(first, "OVF");
        if (strlen(second) > 6U) strcpy(second, "OVF");
    }
    line(display, 4U, "AGE:%s/%s ms", first, second);
    line(display, 5U, "F:%s R:%s", !selected(model, RH_FRONT) ? "NC" :
        (remote_history_fresh(model, RH_FRONT, now) && display->radio_live ? "OK" :
        (model->history[RH_FRONT].received ? "OLD" : "--")),
        !selected(model, RH_REAR) ? "NC" :
        (remote_history_fresh(model, RH_REAR, now) && display->radio_live ? "OK" :
        (model->history[RH_REAR].received ? "OLD" : "--")));
    if (model->history[RH_VTARGET].received) (void)snprintf(third, sizeof(third), "%lu",
        (unsigned long)(now - model->history[RH_VTARGET].updated_ms));
    else strcpy(third, "--");
    line(display, 6U, "V:%s AGE:%s", model->received ?
        ((model->latest.axis_flags & 3U) == 3U ? "DBL" :
        (model->latest.axis_flags & 1U ? "LAX" : (model->latest.axis_flags & 2U ? "RAX" : "INV"))) : "--", third);
    pair_fields(display, model, now, 7U, "VT", RH_TARGET_KMH1, 1U, "km/h");
}

static void position(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    char text[16];
    number(display, model, RH_RAW1, now, 0U, text); line(display, 1U, "A1:%s count", text);
    number(display, model, RH_RAW2, now, 0U, text); line(display, 2U, "A2:%s count", text);
    number(display, model, RH_DIFF, now, 0U, text); line(display, 3U, "DP:%s mm", text);
    number(display, model, RH_NET, now, 1U, text); line(display, 4U, "NET:%s m", text);
    number(display, model, RH_BOOT, now, 1U, text); line(display, 5U, "BOOT:%s m", text);
    number(display, model, RH_TRIP, now, 1U, text); line(display, 6U, "TRIP:%s m", text);
    (void)use(display, model, RH_INCOMPLETE, now);
    line(display, 7U, "Q:%s EST:WHEEL", model->history[RH_INCOMPLETE].received &&
        model->history[RH_INCOMPLETE].value ? "INC" :
        (!display->quality_any ? "UNK" : (display->quality_old ? "OLD" : "OK")));
}

static void sync_page(remote_display_t *display, const remote_status_model_t *model, uint32_t now)
{
    static const char *const states[] = {"OFF", "TRK", "LIM", "INV", "REV", "FLT", "ONE"};
    static const char *const rejects[] = {"NO", "DUP", "AGE", "SKW", "GAT", "WAIT"};
    char first[16], second[16];
    line(display, 1U, "ON:%s A:%s T:%s L:%s", flag(display, model, RH_SYNC_FLAGS, 1U, now),
        flag(display, model, RH_SYNC_FLAGS, 2U, now), flag(display, model, RH_SYNC_FLAGS, 4U, now),
        flag(display, model, RH_SYNC_FLAGS, 8U, now));
    line(display, 2U, "ST:%s P:%s R:%s", name(display, model, RH_SYNC_STATE, states, 7U, now),
        flag(display, model, RH_PAIR, 1U, now), name(display, model, RH_REJECT, rejects, 6U, now));
    number(display, model, RH_DELTA, now, 0U, first); line(display, 3U, "DV:%s rpm", first);
    number(display, model, RH_ERROR, now, 0U, first);
    number(display, model, RH_CORRECTION, now, 0U, second); line(display, 4U, "ER:%s CP:%s", first, second);
    number(display, model, RH_SKEW, now, 0U, first); line(display, 5U, "SK:%s ms", first);
    sample_age(display, model, RH_SYNC_AGE1, now, first);
    sample_age(display, model, RH_SYNC_AGE2, now, second); line(display, 6U, "AGE:%s/%sms", first, second);
    line(display, 7U, "Q:%s UNIT:rpm", !display->quality_any ? "UNK" :
        (display->quality_old ? "OLD" : (model->history[RH_SYNC_FLAGS].value & 2U ? "OK" : "IDLE")));
}

static void diagnostic(remote_display_t *display, const remote_status_model_t *model,
                         const remote_debug_view_t *debug, uint32_t now)
{
    static const char *const batteries[] = {"UNK", "OK", "LOW", "CHG", "FLT"};
    static const char *const locations[] = {"UNK", "OK", "OLD", "LIM"};
    static const char *const arms[] = {"UNK", "STOW", "OPEN", "MOVE", "FLT"};
    static const char *const lights[] = {"UNK", "OFF", "ON", "FLT"};
    static const char *const acquisitions[] = {"UNK", "STOP", "RUN", "PAUS", "FLT"};
    char first[16], second[16], third[16];
    uint8_t disconnected = (uint8_t)(remote_status_fresh(model, 0x8AU, now) &&
        model->slot[9].frame.data.metadata.version >= 2U &&
        !(model->slot[9].frame.data.metadata.capabilities & 1U));
    if (disconnected) {
        (void)use(display, model, RH_COMM, now);
        line(display, 1U, "BAT:N/C SOC:N/C");
        line(display, 2U, "LOC:N/C ACC:N/C");
        line(display, 3U, "ARM:N/C LGT:N/C");
        line(display, 4U, "DAQ:N/C EXT:N/C");
    } else {
    number(display, model, RH_CHARGE, now, 0U, first);
    line(display, 1U, "BAT:%s SOC:%s%%", name(display, model, RH_BATTERY, batteries, 5U, now), first);
    number(display, model, RH_ACCURACY, now, 0U, first);
    line(display, 2U, "LOC:%s ACC:%s", name(display, model, RH_LOCATION, locations, 4U, now), first);
    line(display, 3U, "ARM:%s LGT:%s", name(display, model, RH_ARM, arms, 5U, now),
        name(display, model, RH_LIGHT, lights, 4U, now));
    number(display, model, RH_EXT_FAULTS, now, 0U, first);
    if (model->history[RH_EXT_FAULTS].received)
        (void)snprintf(first, sizeof(first), "%02X", (unsigned)model->history[RH_EXT_FAULTS].value);
    line(display, 4U, "DAQ:%s EXT:%s", name(display, model, RH_ACQUISITION, acquisitions, 5U, now), first);
    }
    number(display, model, RH_COMM, now, 0U, first);
    if (model->history[RH_COMM].received && model->history[RH_COMM].value > 4) strcpy(first, "UNK");
    line(display, 5U, "MC:%s RF:%.2s", first, debug->radio);
    number(display, model, RH_ALARM_CODE, now, 0U, first);
    if (model->history[RH_ALARM_CODE].received) {
        if (model->history[RH_ALARM_CODE].value <= 12)
            (void)snprintf(first, sizeof(first), "%02X", (unsigned)model->history[RH_ALARM_CODE].value);
        else strcpy(first, "UNK");
    }
    number(display, model, RH_ALARM_SOURCE, now, 0U, second);
    if (model->history[RH_ALARM_SOURCE].received && model->history[RH_ALARM_SOURCE].value > 5) strcpy(second, "UNK");
    number(display, model, RH_ALARM_SEQUENCE, now, 0U, third);
    line(display, 6U, "EV:%s S:%s N:%s", first, second, third);
    number(display, model, RH_SPEED_UNIT, now, 0U, first);
    number(display, model, RH_POSITION_UNIT, now, 0U, second);
    line(display, 7U, "AX:%s UV:%s/%s RO:%u", !model->received ? "?" :
        ((model->latest.axis_flags & 3U) == 3U ? "B" :
        (model->latest.axis_flags & 1U ? "1" : (model->latest.axis_flags & 2U ? "2" : "NC"))),
        first, second, (unsigned)((model->latest.axis_flags & 8U) != 0U));
}

void remote_display_init(remote_display_t *display)
{
    memset(display, 0, sizeof(*display));
}

void remote_display_keys(remote_display_t *display, uint16_t keys,
                         uint16_t *short_upload, uint16_t *long_upload,
                         uint16_t *double_upload)
{
    uint16_t local = keys & REMOTE_LOCAL_KEY_MASK;
    if (local == REMOTE_PREVIOUS_PAGE_KEY_MASK)
        display->requested_page = (uint8_t)((display->requested_page + REMOTE_DISPLAY_PAGE_COUNT - 1U) % REMOTE_DISPLAY_PAGE_COUNT);
    else if (local == REMOTE_NEXT_PAGE_KEY_MASK)
        display->requested_page = (uint8_t)((display->requested_page + 1U) % REMOTE_DISPLAY_PAGE_COUNT);
    *short_upload &= (uint16_t)~REMOTE_LOCAL_KEY_MASK;
    *long_upload &= (uint16_t)~REMOTE_LOCAL_KEY_MASK;
    *double_upload &= (uint16_t)~REMOTE_LOCAL_KEY_MASK;
}

void remote_display_build(remote_display_t *display, const remote_status_model_t *model,
                          const remote_debug_view_t *debug, uint32_t now,
                          uint8_t ready, uint8_t capable)
{
    static const char *const pages[] = {"HOME", "OVR", "SRV", "DRV", "VEL", "POS", "SYN", "DIA", "DBG"};
    const char *link = !ready ? "INIT" : (!capable ? "N/A" :
        (remote_status_link(model, now) == REMOTE_RETURN_WAIT ? "WAIT" :
        (remote_status_link(model, now) == REMOTE_RETURN_LOST ? "LOST" : "RX")));
    const char *quality;
    char alarm[16];
    display->page = display->requested_page < REMOTE_DISPLAY_PAGE_COUNT ? display->requested_page : 0U;
    display->truncated = 0U; display->invert = 0U; display->region_count = 0U;
    display->quality_any = 0U; display->quality_old = 0U;
    display->radio_live = (uint8_t)(ready && capable && remote_status_link(model, now) == REMOTE_RETURN_ONLINE);
    memset(display->lines, 0, sizeof(display->lines));
    switch (display->page) {
    case REMOTE_PAGE_HOME: home(display, model, now); break;
    case REMOTE_PAGE_OVERVIEW: overview(display, model, now); break;
    case REMOTE_PAGE_SERVO: servo(display, model, now); break;
    case REMOTE_PAGE_DRIVE: drive(display, model, now); break;
    case REMOTE_PAGE_VELOCITY: vehicle(display, model, now); break;
    case REMOTE_PAGE_POSITION: position(display, model, now); break;
    case REMOTE_PAGE_SYNC: sync_page(display, model, now); break;
    case REMOTE_PAGE_DIAGNOSTIC: diagnostic(display, model, debug, now); break;
    case REMOTE_PAGE_DEBUG:
        line(display, 1U, "ADC LH:%04u LV:%04u", (unsigned)debug->joystick[0], (unsigned)debug->joystick[1]);
        line(display, 2U, "ADC RH:%04u RV:%04u", (unsigned)debug->joystick[2], (unsigned)debug->joystick[3]);
        line(display, 3U, "S:%.12s", debug->short_keys);
        line(display, 4U, "L:%.12s", debug->long_keys);
        line(display, 5U, "D:%.12s", debug->double_keys);
        line(display, 6U, "SEQ:%03u EVT:%05u", (unsigned)debug->frame_sequence, (unsigned)debug->event_sequence);
        line(display, 7U, "RF:%.2s 10ms K1-C", debug->radio);
        region(display, 3U, 0U, 6U); region(display, 4U, 0U, 6U); region(display, 5U, 0U, 6U);
        display->quality_any = 1U;
        break;
    default: break;
    }
    if (!display->radio_live && display->page != REMOTE_PAGE_DEBUG) display->quality_old = 1U;
    quality = !display->quality_any ? "WAIT" : (display->quality_old ? "OLD" : "OK");
    if (!display->radio_live) strcpy(alarm, "UNK");
    else if (model->latest.alarm_level > 4U) strcpy(alarm, "AUNK");
    else if (!model->latest.alarm_level) strcpy(alarm, "A0/00");
    else if (remote_status_alarm_detail(model, now) && model->slot[6].frame.data.alarm.code <= 12U)
        (void)snprintf(alarm, sizeof(alarm), "A%u/%02X", (unsigned)model->latest.alarm_level,
            (unsigned)model->slot[6].frame.data.alarm.code);
    else (void)snprintf(alarm, sizeof(alarm), "A%u/UNK", (unsigned)model->latest.alarm_level);
    if (display->page == REMOTE_PAGE_HOME) line(display, 0U, "1/9 %s %s %s", link, quality, alarm);
    else {
        line(display, 0U, "%s %u/9 %s %s", pages[display->page], (unsigned)(display->page + 1U), link, quality);
        if (strlen(display->lines[0]) + strlen(alarm) + 1U <= REMOTE_DISPLAY_COLUMNS) {
            size_t length = strlen(display->lines[0]);
            (void)snprintf(display->lines[0] + length, sizeof(display->lines[0]) - length, " %s", alarm);
        } else line(display, 0U, "%u/9 %s %s %s", (unsigned)(display->page + 1U), link, quality, alarm);
    }
    display->invert_row = 0U;
    display->invert_width = (uint8_t)(strlen(alarm) * 6U);
    display->invert_x = (uint8_t)((strlen(display->lines[0]) - strlen(alarm)) * 6U);
    display->invert = (uint8_t)(display->radio_live && model->latest.alarm_level >= 3U &&
        model->latest.alarm_level <= 4U && ((now / 500U) & 1U));
    if (display->invert) region(display, 0U, display->invert_x, display->invert_width);
}
