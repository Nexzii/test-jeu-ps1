#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>
#include <psxgpu.h>

void text_init(void);
void text_draw(const char *str, int x, int y, uint32_t *ot, char **pri);
void text_flush(void);

#endif
