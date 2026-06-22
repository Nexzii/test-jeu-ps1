#include "player.h"
#include <string.h>

PlayerState player;

static const int32_t xp_table[] = {
    0, 20, 50, 100, 180, 300, 480, 720, 1050, 1500,
    2100, 2900, 3900, 5200, 6800, 8800, 11500, 15000, 19500, 25000
};

void player_init(void) {
    memset(&player, 0, sizeof(PlayerState));

    strcpy(player.party[0].name, "ZION");
    player.party[0].hp     = 120;
    player.party[0].hp_max = 120;
    player.party[0].mp     = 30;
    player.party[0].mp_max = 30;
    player.party[0].atk    = 12;
    player.party[0].def    = 8;
    player.party[0].mag    = 6;
    player.party[0].spd    = 10;
    player.party[0].level  = 1;
    player.party[0].xp     = 0;
    player.party[0].xp_next = xp_table[1];

    strcpy(player.party[1].name, "LYRA");
    player.party[1].hp     = 90;
    player.party[1].hp_max = 90;
    player.party[1].mp     = 60;
    player.party[1].mp_max = 60;
    player.party[1].atk    = 6;
    player.party[1].def    = 6;
    player.party[1].mag    = 14;
    player.party[1].spd    = 8;
    player.party[1].level  = 1;
    player.party[1].xp     = 0;
    player.party[1].xp_next = xp_table[1];

    player.party_size = 2;
    player.gold       = 100;
    player.map_x      = 9;
    player.map_y      = 7;
    player.current_map = 0;

    player_add_item(0, 3); // Potion x3
}

static void level_up(PartyMember *m) {
    if (m->level >= 20) return;

    m->level++;
    m->hp_max += 8 + (m->level * 2);
    m->mp_max += 3 + m->level;
    m->atk    += 2;
    m->def    += 1;
    m->mag    += 2;
    m->spd    += 1;
    m->hp      = m->hp_max;
    m->mp      = m->mp_max;

    if (m->level < 20)
        m->xp_next = xp_table[m->level];
}

void player_add_xp(int32_t xp) {
    for (int i = 0; i < player.party_size; i++) {
        if (!player_is_alive(i)) continue;
        player.party[i].xp += xp;
        while (player.party[i].xp >= player.party[i].xp_next && player.party[i].level < 20) {
            level_up(&player.party[i]);
        }
    }
}

void player_add_gold(int32_t g) {
    player.gold += g;
    if (player.gold > 999999) player.gold = 999999;
}

void player_heal(int member, int16_t amount) {
    PartyMember *m = &player.party[member];
    m->hp += amount;
    if (m->hp > m->hp_max) m->hp = m->hp_max;
}

void player_damage(int member, int16_t amount) {
    PartyMember *m = &player.party[member];
    m->hp -= amount;
    if (m->hp < 0) m->hp = 0;
}

int player_is_alive(int member) {
    return player.party[member].hp > 0;
}

int player_party_alive(void) {
    for (int i = 0; i < player.party_size; i++) {
        if (player_is_alive(i)) return 1;
    }
    return 0;
}

void player_add_item(uint8_t id, uint8_t qty) {
    for (int i = 0; i < player.item_count; i++) {
        if (player.items[i].id == id) {
            player.items[i].qty += qty;
            return;
        }
    }
    if (player.item_count < MAX_ITEMS) {
        player.items[player.item_count].id  = id;
        player.items[player.item_count].qty = qty;
        player.item_count++;
    }
}
