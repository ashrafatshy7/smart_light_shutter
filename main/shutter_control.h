#ifndef SHUTTER_CONTROL_H
#define SHUTTER_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#define SHUTTER_TOTAL_TIME_US (17 * 1000000ULL)

typedef enum {
    SHUTTER_IDLE,
    SHUTTER_OPENING,
    SHUTTER_CLOSING
} shutter_state_t;

void shutter_init(void);
void shutter_update(void);

// Logic controls
void shutter_open(void);
void shutter_close(void);
void shutter_stop(void);
void shutter_set_position(uint8_t percentage);

// Getters
shutter_state_t shutter_get_state(void);
uint8_t shutter_get_position(void);

#endif // SHUTTER_CONTROL_H
