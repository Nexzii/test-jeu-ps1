#include "battle.h"
#include "player.h"
#include "text.h"
#include "engine3d.h"
#include "input.h"
#include "model_cloud.h"
#include "textures.h"
#include "audio.h"
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENEMIES   3
/* 2D UI sorts at a low OT index so it always draws in front of the 3D scene. */
#define BATTLE_OT_BASE 16

/* Battlefield 3D layout. */
#define BT          350        /* battlefield tile size */
#define BCAM_DIST   980
#define BCAM_HEIGHT 380
#define ENEMY_Z     250
#define FULL_CIRCLE 131072

typedef enum {
    BSTATE_START,
    BSTATE_SELECT_ACTION,
    BSTATE_SELECT_TARGET,
    BSTATE_ANIMATE,
    BSTATE_ENEMY_TURN,
    BSTATE_WIN,
    BSTATE_LOSE,
    BSTATE_DONE
} BattleState;

typedef enum {
    ACT_ATTACK,
    ACT_MAGIC,
    ACT_ITEM,
    ACT_FLEE,
    ACT_COUNT
} Action;

static const char *action_names[] = { "ATTACK", "MAGIC", "ITEM", "FLEE" };

static const Enemy enemy_db[] = {
    { "GOBLIN",   25,  25,  8,  4,  6,  5,  10 },
    { "SLIME",    15,  15,  5,  2,  3,  3,   5 },
    { "WOLF",     35,  35, 12,  5,  9,  8,  15 },
    { "SKELETON", 40,  40, 10,  8,  5, 10,  20 },
    { "BAT",      12,  12,  6,  3, 12,  4,   8 },
    { "ORC",      60,  60, 15, 10,  4, 15,  30 },
    { "MAGE",     30,  30, 18,  3,  7, 20,  40 },
    { "DRAGON",  200, 200, 30, 20, 10, 100, 200 },
};

static Enemy      enemies[MAX_ENEMIES];
static int        enemy_type[MAX_ENEMIES];   /* index into enemy_db, for color */
static int        enemy_count;
static BattleState bstate;
static int        cursor;
static int        current_member;
static int        target;
static int        anim_timer;
static int32_t    total_xp;
static int32_t    total_gold;
static char       msg_buf[40];
static int        msg_timer;
static int        bcam_yaw;     /* battle camera orbit */
static int        gfx_timer;    /* animation counter for bobbing/flashing */

/* 3D model color per enemy_db entry. */
static const uint8_t enemy_colors[8][3] = {
    {  80, 170,  60 },   /* goblin   */
    { 120, 210, 140 },   /* slime    */
    { 140, 130, 140 },   /* wolf     */
    { 220, 215, 190 },   /* skeleton */
    { 130,  70, 170 },   /* bat      */
    {  90, 140,  70 },   /* orc      */
    { 210,  80,  80 },   /* mage     */
    { 210,  60,  50 },   /* dragon   */
};

static int calc_damage(int16_t atk, int16_t def) {
    int dmg = atk - (def / 2) + (rand() % 5);
    if (dmg < 1) dmg = 1;
    return dmg;
}

void battle_init(int zone) {
    enemy_count = 1 + (rand() % 3);
    int pool_start = zone * 2;
    if (pool_start > 6) pool_start = 6;

    for (int i = 0; i < enemy_count; i++) {
        int idx = pool_start + (rand() % 2);
        if (idx >= 8) idx = 7;
        memcpy(&enemies[i], &enemy_db[idx], sizeof(Enemy));
        enemy_type[i] = idx;
    }

    bstate = BSTATE_START;
    cursor = 0;
    current_member = 0;
    total_xp = 0;
    total_gold = 0;
    anim_timer = 60;
    msg_timer = 0;
    bcam_yaw = 0;
    strcpy(msg_buf, "ENEMIES APPEAR!");
}

static void next_alive_member(void) {
    for (int i = current_member + 1; i < player.party_size; i++) {
        if (player_is_alive(i)) {
            current_member = i;
            cursor = 0;
            bstate = BSTATE_SELECT_ACTION;
            return;
        }
    }
    bstate = BSTATE_ENEMY_TURN;
}

static int all_enemies_dead(void) {
    for (int i = 0; i < enemy_count; i++)
        if (enemies[i].hp > 0) return 0;
    return 1;
}

static void award_and_win(void) {
    total_xp = 0;
    total_gold = 0;
    for (int i = 0; i < enemy_count; i++) {
        total_xp   += enemies[i].xp_reward;
        total_gold += enemies[i].gold_reward;
    }
    bstate = BSTATE_WIN;
    anim_timer = 90;
}

void battle_update(uint16_t pad, int (*pressed)(uint16_t)) {
    /* Orbit the battle camera with the right stick (L1/R1 fallback). */
    bcam_yaw += (pad_rstick_x() * 1200) / 128;
    if (pad & PAD_L1) bcam_yaw -= 1200;
    if (pad & PAD_R1) bcam_yaw += 1200;
    bcam_yaw &= (FULL_CIRCLE - 1);

    /* Victory can be reached from any action (e.g. AOE magic killing the
     * whole group), so detect it centrally. This also prevents the target
     * selection loop below from spinning forever on an all-dead party. */
    if (bstate != BSTATE_WIN && bstate != BSTATE_LOSE && bstate != BSTATE_DONE
            && all_enemies_dead()) {
        award_and_win();
    }

    switch (bstate) {
        case BSTATE_START:
            anim_timer--;
            if (anim_timer <= 0) {
                current_member = 0;
                cursor = 0;
                bstate = BSTATE_SELECT_ACTION;
            }
            break;

        case BSTATE_SELECT_ACTION:
            if (pressed(PAD_UP))   { cursor--; if (cursor < 0) cursor = ACT_COUNT - 1; }
            if (pressed(PAD_DOWN)) { cursor++; if (cursor >= ACT_COUNT) cursor = 0; }

            if (pressed(PAD_CROSS)) { // X button
                if (cursor == ACT_ATTACK) {
                    target = 0;
                    bstate = BSTATE_SELECT_TARGET;
                } else if (cursor == ACT_FLEE) {
                    if (rand() % 100 < 50) {
                        bstate = BSTATE_DONE;
                    } else {
                        strcpy(msg_buf, "CAN'T ESCAPE!");
                        msg_timer = 40;
                        bstate = BSTATE_ENEMY_TURN;
                    }
                } else if (cursor == ACT_ITEM) {
                    if (player.item_count > 0) {
                        player.items[0].qty--;
                        player_heal(current_member, 50);
                        if (player.items[0].qty == 0) {
                            for (int i = 0; i < player.item_count - 1; i++)
                                player.items[i] = player.items[i+1];
                            player.item_count--;
                        }
                        sprintf(msg_buf, "%s HEALED!", player.party[current_member].name);
                        msg_timer = 30;
                        next_alive_member();
                    }
                } else if (cursor == ACT_MAGIC) {
                    PartyMember *m = &player.party[current_member];
                    if (m->mp >= 5) {
                        m->mp -= 5;
                        for (int i = 0; i < enemy_count; i++) {
                            if (enemies[i].hp > 0) {
                                int dmg = calc_damage(m->mag * 2, enemies[i].def);
                                enemies[i].hp -= dmg;
                                if (enemies[i].hp < 0) enemies[i].hp = 0;
                            }
                        }
                        strcpy(msg_buf, "FIRE!");
                        msg_timer = 30;
                        audio_sfx(AUDIO_SFX_MAGIC);
                        next_alive_member();
                    }
                }
            }
            break;

        case BSTATE_SELECT_TARGET:
            if (pressed(PAD_UP))   { target--; if (target < 0) target = enemy_count - 1; }
            if (pressed(PAD_DOWN)) { target++; if (target >= enemy_count) target = 0; }

            // Skip dead enemies (safe: victory check above guarantees one is alive)
            while (enemies[target].hp <= 0) {
                target = (target + 1) % enemy_count;
            }

            if (pressed(PAD_CROSS)) { // X
                PartyMember *m = &player.party[current_member];
                int dmg = calc_damage(m->atk, enemies[target].def);
                enemies[target].hp -= dmg;
                if (enemies[target].hp < 0) enemies[target].hp = 0;

                sprintf(msg_buf, "%d DMG!", dmg);
                msg_timer = 25;
                anim_timer = 20;
                audio_sfx(AUDIO_SFX_HIT);
                bstate = BSTATE_ANIMATE;
            }

            if (pressed(PAD_CIRCLE)) { // O = cancel
                bstate = BSTATE_SELECT_ACTION;
            }
            break;

        case BSTATE_ANIMATE:
            anim_timer--;
            if (anim_timer <= 0) {
                // Check if all enemies dead
                int all_dead = 1;
                for (int i = 0; i < enemy_count; i++) {
                    if (enemies[i].hp > 0) { all_dead = 0; break; }
                }
                if (all_dead) {
                    award_and_win();
                } else {
                    next_alive_member();
                }
            }
            break;

        case BSTATE_ENEMY_TURN: {
            for (int i = 0; i < enemy_count; i++) {
                if (enemies[i].hp <= 0) continue;
                int t = rand() % player.party_size;
                int tries = 0;
                while (!player_is_alive(t) && tries < player.party_size) {
                    t = (t + 1) % player.party_size;
                    tries++;
                }
                if (player_is_alive(t)) {
                    int dmg = calc_damage(enemies[i].atk, player.party[t].def);
                    player_damage(t, dmg);
                }
            }

            if (!player_party_alive()) {
                bstate = BSTATE_LOSE;
                anim_timer = 120;
            } else {
                current_member = 0;
                while (!player_is_alive(current_member) && current_member < player.party_size)
                    current_member++;
                cursor = 0;
                bstate = BSTATE_SELECT_ACTION;
            }
            break;
        }

        case BSTATE_WIN:
            anim_timer--;
            if (anim_timer <= 0) bstate = BSTATE_DONE;
            break;

        case BSTATE_LOSE:
            anim_timer--;
            if (anim_timer <= 0) {
                player_init();
                bstate = BSTATE_DONE;
            }
            break;

        case BSTATE_DONE:
            break;
    }

    if (msg_timer > 0) msg_timer--;
}

static void draw_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                     uint32_t *ot, char **pri) {
    TILE *tile = (TILE *)*pri;
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);
    addPrim(ot + BATTLE_OT_BASE, tile);
    *pri += sizeof(TILE);
}

/* Compute an enemy's 3D footprint. */
static void enemy_geom(int i, int32_t *x, int32_t *z, int32_t *hw, int32_t *hh) {
    int hp = enemies[i].hp_max;
    int h = 70 + hp;       if (h  > 230) h  = 230;
    int w = 55 + hp / 3;   if (w  > 140) w  = 140;
    *x  = (i * 2 - (enemy_count - 1)) * 190;
    *z  = ENEMY_Z;
    *hw = w;
    *hh = h;
}

/* Full-screen 2D battle background (pre-rendered art), drawn behind the 3D. */
static void battle_draw_bg(uint32_t *ot, char **pri) {
    POLY_FT4 *bg = (POLY_FT4 *)*pri;
    setPolyFT4(bg);
    setXY4(bg, 0, 0,  320, 0,  0, 240,  320, 240);
    setUVWH(bg, 0, 0, 255, 239);
    bg->tpage = tp_bg;
    bg->clut = 0;
    setRGB0(bg, 128, 128, 128);
    addPrim(ot + (OT_LEN - 2), bg);   /* backmost */
    *pri += sizeof(POLY_FT4);
}

static void battle_draw_3d(uint32_t *ot, char **pri) {
    VECTOR eye, at;
    int s = isin(bcam_yaw);
    int c = icos(bcam_yaw);

    eng3d_set_fog(0);                 /* no fog in battle */
    battle_draw_bg(ot, pri);

    at.vx = 0;  at.vy = -120; at.vz = ENEMY_Z;
    eye.vx = at.vx - ((BCAM_DIST * s) >> 12);
    eye.vy = -BCAM_HEIGHT;
    eye.vz = at.vz - ((BCAM_DIST * c) >> 12);
    eng3d_set_camera(&eye, &at);

    /* Checkerboard battlefield ground. */
    for (int gz = 0; gz < 5; gz++) {
        for (int gx = -2; gx <= 2; gx++) {
            int dark = (gx + gz) & 1;
            eng3d_draw_floor(gx * BT, ENEMY_Z + (gz - 1) * BT, BT / 2 + 6,
                             dark ? 50 : 70, dark ? 75 : 95, dark ? 50 : 70,
                             ot, pri);
        }
    }

    /* Enemies (moi2 model as placeholder visual), facing the camera. */
    int enemy_yaw = (bcam_yaw / 32) + 2048;
    for (int i = 0; i < enemy_count; i++) {
        if (enemies[i].hp <= 0) continue;
        int32_t x, z, hw, hh;
        enemy_geom(i, &x, &z, &hw, &hh);
        (void)hw; (void)hh;

        /* placeholder white cube enemy (real model TBD) */
        eng3d_draw_box(x, -hh, z, hw, hh, hw, 235, 235, 235, ot, pri);

        /* Bobbing target marker above the model. */
        if (bstate == BSTATE_SELECT_TARGET && i == target) {
            int bob = ((gfx_timer >> 1) & 7) * 5;
            eng3d_draw_box(x, -380 - bob, z, 28, 28, 28,
                           240, 220, 40, ot, pri);
        }
    }

    /* Party members (the hero model), facing the enemies. */
    int party_yaw = (bcam_yaw / 32);     /* face away from camera = toward foes */
    for (int i = 0; i < player.party_size; i++) {
        if (!player_is_alive(i)) continue;
        int32_t px = (i * 2 - (player.party_size - 1)) * 180;
        eng3d_draw_texmesh(&model_cloud, px, 0, ENEMY_Z - 560, party_yaw,
                           tp_cloud, ot, pri);
    }
}

void battle_draw(uint32_t *ot, char **pri) {
    gfx_timer++;

    /* 3D battlefield first (it sorts behind the 2D UI). */
    battle_draw_3d(ot, pri);

    // Enemy list (compact, top-left)
    draw_box(6, 6, 150, enemy_count * 12 + 8, 0, 0, 70, ot, pri);
    for (int i = 0; i < enemy_count; i++) {
        char buf[32];
        if (enemies[i].hp > 0)
            sprintf(buf, "%s %d", enemies[i].name, enemies[i].hp);
        else
            sprintf(buf, "%s DEAD", enemies[i].name);
        text_draw(buf, 18, 11 + i * 12, ot, pri);

        if (bstate == BSTATE_SELECT_TARGET && i == target)
            text_draw(">", 10, 11 + i * 12, ot, pri);
    }

    // Party status bar (bottom)
    draw_box(0, 196, 320, 44, 0, 0, 70, ot, pri);
    for (int i = 0; i < player.party_size; i++) {
        char buf[40];
        PartyMember *m = &player.party[i];
        sprintf(buf, "%s HP:%d/%d MP:%d/%d", m->name, m->hp, m->hp_max, m->mp, m->mp_max);
        text_draw(buf, 18, 202 + i * 18, ot, pri);

        if (bstate == BSTATE_SELECT_ACTION && i == current_member)
            text_draw(">", 8, 202 + i * 18, ot, pri);
    }

    // Action menu
    if (bstate == BSTATE_SELECT_ACTION) {
        draw_box(206, 100, 108, 54, 0, 0, 100, ot, pri);
        for (int i = 0; i < ACT_COUNT; i++)
            text_draw(action_names[i], 226, 104 + i * 12, ot, pri);
        text_draw(">", 218, 104 + cursor * 12, ot, pri);
    }

    // Messages
    if (msg_timer > 0 || bstate == BSTATE_START) {
        draw_box(40, 86, 240, 26, 20, 20, 20, ot, pri);
        text_draw(msg_buf, 50, 92, ot, pri);
    }

    // Win/Lose
    if (bstate == BSTATE_WIN) {
        char buf[40];
        draw_box(40, 80, 240, 50, 20, 20, 60, ot, pri);
        text_draw("VICTORY!", 120, 86, ot, pri);
        sprintf(buf, "XP: %d  GOLD: %d", (int)total_xp, (int)total_gold);
        text_draw(buf, 80, 106, ot, pri);
    }

    if (bstate == BSTATE_LOSE) {
        draw_box(60, 100, 200, 26, 80, 0, 0, ot, pri);
        text_draw("GAME OVER...", 100, 106, ot, pri);
    }
}

int battle_is_over(void) {
    return bstate == BSTATE_DONE;
}

int32_t battle_get_xp(void) {
    return total_xp;
}

int32_t battle_get_gold(void) {
    return total_gold;
}
