#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Button edge / level tests (PAD_* constants from psxpad.h). */
int pad_pressed(uint16_t btn);
int pad_held(uint16_t btn);

/* Analog stick axes, range -128..127, 0 at center.
 * Return 0 when the controller is not in analog mode. A small dead zone
 * is already applied. */
int pad_rstick_x(void);
int pad_rstick_y(void);
int pad_lstick_x(void);
int pad_lstick_y(void);

/* Controller type nibble last seen (-1 if disconnected). 4=digital,
 * 5=analog stick, 7=DualShock analog. For diagnosing analog issues. */
int pad_type_dbg(void);

#endif
