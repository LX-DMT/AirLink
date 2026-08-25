#ifndef AIRLINK_LV_PORT_H
#define AIRLINK_LV_PORT_H
#include "display.h"
#include "touch.h"
void airlink_lv_port_init(struct airlink_display_status *display,
                          struct airlink_touch_status *touch);
uint32_t airlink_lv_port_take_full_redraw_request(void);
#endif
