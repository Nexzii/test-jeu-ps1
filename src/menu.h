#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <psxgpu.h>

void menu_open(void);
void menu_update(uint16_t pad, int (*pressed)(uint16_t));
void menu_draw(uint32_t *ot, char **pri);
int  menu_closed(void);

#endif
