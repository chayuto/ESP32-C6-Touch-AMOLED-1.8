#pragma once

#include "esp_err.h"
#include "pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load the saved pet from NVS. ESP_ERR_NVS_NOT_FOUND on first boot.
 *  The struct is overwritten on success. */
esp_err_t pet_save_load(pet_state_t *p);

/** Force-commit the current pet state to NVS now. */
esp_err_t pet_save_commit(const pet_state_t *p);

/** Mark the state as dirty; the periodic flusher will commit within
 *  ~5 seconds of the last request. Cheap to call from any handler. */
void pet_save_request(void);

/** Run the debounced flusher. Call from a 1 Hz LVGL timer. */
void pet_save_pump(const pet_state_t *p);

/** Erase the save (debug/death rebirth). */
esp_err_t pet_save_clear(void);

#ifdef __cplusplus
}
#endif
