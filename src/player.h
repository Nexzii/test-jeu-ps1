#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#define MAX_ITEMS     20
#define MAX_PARTY      4
#define MAX_NAME_LEN  12

typedef struct {
    char     name[MAX_NAME_LEN];
    int16_t  hp, hp_max;
    int16_t  mp, mp_max;
    int16_t  atk, def, mag, spd;
    int16_t  level;
    int32_t  xp;
    int32_t  xp_next;
} PartyMember;

typedef struct {
    uint8_t  id;
    uint8_t  qty;
} Item;

typedef struct {
    PartyMember party[MAX_PARTY];
    int         party_size;
    Item        items[MAX_ITEMS];
    int         item_count;
    int32_t     gold;
    int16_t     map_x, map_y;
    int16_t     current_map;
} PlayerState;

extern PlayerState player;

void player_init(void);
void player_add_xp(int32_t xp);
void player_add_gold(int32_t g);
void player_heal(int member, int16_t amount);
void player_damage(int member, int16_t amount);
int  player_is_alive(int member);
int  player_party_alive(void);
void player_add_item(uint8_t id, uint8_t qty);

#endif
