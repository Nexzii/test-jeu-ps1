#ifndef TEXTURES_H
#define TEXTURES_H

#include <stdint.h>

/* tpage handles, valid after textures_load(). */
extern uint16_t tp_stone, tp_brick, tp_wood, tp_roof, tp_grass, tp_tree, tp_cloud, tp_dialog, tp_bg;

void textures_load(void);   /* upload all textures to VRAM (call once) */

#endif
