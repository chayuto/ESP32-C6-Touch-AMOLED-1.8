#pragma once

#include "lvgl.h"

/* Which metric the chart plots. Humidity is the default: it is the reading
 * this build exists to watch. */
typedef enum {
    CHART_METRIC_HUMIDITY = 0,
    CHART_METRIC_TEMP,
} chart_metric_t;

/* Build the chart page under `parent`. Returns the page root so the caller can
 * show/hide it; it starts hidden. */
lv_obj_t *chart_view_create(lv_obj_t *parent);

void chart_view_set_metric(chart_metric_t metric);

/* Repaint from the current history snapshot. Cheap enough to call at 1 Hz. */
void chart_view_refresh(void);
