#ifndef BATTLE_H
#define BATTLE_H

#include <stdint.h>
#include <psxgpu.h>

typedef struct {
    char    name[12];
    int16_t hp, hp_max;
    int16_t atk, def, spd;
    int16_t xp_reward;
    int16_t gold_reward;
} Enemy;

void battle_init(int zone);
void battle_update(uint16_t pad, int (*pressed)(uint16_t));
void battle_draw(uint32_t *ot, char **pri);
int  battle_is_over(void);
int32_t battle_get_xp(void);
int32_t battle_get_gold(void);

#endif
