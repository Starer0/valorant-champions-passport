#pragma once

#include "lvgl.h"
#include <stdbool.h>

bool champions_assets_init(void);
const lv_image_dsc_t *champions_assets_logo(int team_index, bool small);
const lv_image_dsc_t *champions_assets_mark(bool small);
