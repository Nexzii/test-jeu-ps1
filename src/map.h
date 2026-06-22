#ifndef MAP_H_GUARD
#define MAP_H_GUARD

#include <stdint.h>
#include <psxgpu.h>

#define MAP_W 20
#define MAP_ROWS 15
#define TILE_SIZE 16

void map_init(int map_id);
void map_update(uint16_t pad, int (*pressed)(uint16_t));
void map_draw(uint32_t *ot, char **pri);
int  map_encounter(void);
int  map_get_zone(void);

/* Returns a pending NPC interaction triggered this frame and clears it:
 *   0 = none, 1 = dialogue opened, 2 = open the shop. */
int  map_take_interaction(void);

#endif /* MAP_H_GUARD */
