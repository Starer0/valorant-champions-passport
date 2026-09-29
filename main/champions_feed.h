#pragma once

#include "champions_model.h"
#include "esp_err.h"

// Public VLR event calendar and match listing. Network I/O belongs on a worker task.
esp_err_t champions_feed_refresh(champions_snapshot_t *snapshot);
// Refresh only the current live match, keeping the snapshot unchanged on failure.
esp_err_t champions_feed_refresh_rounds(champions_snapshot_t *snapshot);

