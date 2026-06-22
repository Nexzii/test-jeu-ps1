#ifndef DIALOGUE_H
#define DIALOGUE_H

#include <stdint.h>

/* Open a dialogue with an array of lines (shown one at a time). */
void dialogue_open(const char *const *lines, int count);

/* Advance with CROSS. */
void dialogue_update(int (*pressed)(uint16_t));

void dialogue_draw(uint32_t *ot, char **pri);

int dialogue_active(void);

#endif
