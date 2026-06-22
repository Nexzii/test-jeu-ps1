#include "map.h"
#include "player.h"
#include "text.h"
#include "engine3d.h"
#include "input.h"
#include "dialogue.h"
#include "model_player.h"
#include "model_tree.h"
#include "model_cloud.h"
#include "textures.h"
#include "audio.h"
#include <psxpad.h>
#include <psxgte.h>
#include <stdlib.h>
#include <stdio.h>

#define TILE_GRASS   0
#define TILE_WALL    1
#define TILE_WATER   2
#define TILE_PATH    3
#define TILE_DOOR    4
#define TILE_TOWER   5
#define TILE_MOUNTAIN 6

/* World scale: one tile is TSZ units across. */
#define TSZ        400
#define HALF_TSZ   (TSZ / 2)
#define WALL_H     420
#define TOWER_H    760
#define PLY_H      300     /* player height */
#define PLY_HW  90      /* player half-width */
#define VIEW_TILES 5       /* how many tiles around the player to render */
#define NPC_VIEW   4       /* NPCs/props only render within this radius */

#define MOVE_SPEED 14      /* world units per frame */

/* Orbit camera. */
#define CAM_DIST     700
#define CAM_HEIGHT_MIN  200
#define CAM_HEIGHT_MAX  950
#define FULL_CIRCLE  131072        /* isin/icos: 131072 = 360 deg */
#define YAW_SPEED    420           /* per frame at full deflection (~70 deg/s) */

/* Decor props (visual only, no collision). */
#define PROP_TREE     0
#define PROP_ROCK     1
#define PROP_CRYSTAL  2
#define PROP_FOUNTAIN 3
#define PROP_THRONE   4
#define PROP_HOUSE    5

typedef struct { uint8_t tx, tz, type; } Prop;

static const Prop props_town[] = {
    { 9, 7, PROP_FOUNTAIN },
    { 3, 2, PROP_THRONE },  { 16, 2, PROP_CRYSTAL },
    { 3, 11, PROP_HOUSE },  { 16, 11, PROP_HOUSE },
    { 7, 4, PROP_TREE },    { 12, 4, PROP_TREE },
};

static const Prop props_field[] = {
    /* forest clusters on the plains (avoid the road at col 9) */
    { 3, 5, PROP_TREE }, { 5, 6, PROP_TREE }, { 4, 7, PROP_TREE },
    { 6, 8, PROP_TREE }, { 3, 11, PROP_TREE },
    { 13, 6, PROP_TREE }, { 15, 7, PROP_TREE }, { 14, 9, PROP_TREE },
    { 16, 11, PROP_TREE }, { 13, 12, PROP_TREE },
    { 5, 2, PROP_ROCK }, { 17, 4, PROP_ROCK }, { 7, 12, PROP_ROCK },
};

static const Prop *map_props[]   = { props_town, props_field };
static const int   map_propcnt[] = {
    sizeof(props_town)  / sizeof(Prop),
    sizeof(props_field) / sizeof(Prop),
};

/* Camera orbit state. */
static int cam_yaw;
static int cam_height = 560;

/* Walk animation state. */
static int walk_phase;
static int is_moving;

/* ---- NPCs ---- */
#define NPC_VILLAGER 0
#define NPC_SHOP     1

typedef struct {
    uint8_t tx, tz, kind;
    uint8_t r, g, b;
    const char *const *lines;
    uint8_t line_count;
} Npc;

static const char *const shop_greet[] = {
    "WELCOME, TRAVELER!",
    "TAKE A LOOK AT MY WARES.",
};
static const char *const villager1_lines[] = {
    "OUR TOWN LIVES IN FEAR.",
    "MONSTERS ROAM THE FIELDS.",
    "FIND THE LOST CRYSTAL!",
};
static const char *const villager2_lines[] = {
    "THE FOUNTAIN IS ANCIENT.",
    "THEY SAY IT GRANTS LUCK.",
};
static const char *const guard_lines[] = {
    "BEYOND THE DOOR LIES",
    "THE FIELD. STAY SHARP!",
};

static const char *const king_lines[] = {
    "WELCOME TO MY CASTLE.",
    "DEFEND THE REALM,",
    "BRAVE HERO!",
};

static const Npc npcs_town[] = {
    { 2,  3, NPC_SHOP,     220, 180,  60, shop_greet,      2 },  /* shop NPC top-left room */
    { 17, 3, NPC_VILLAGER,  60, 160,  90, villager1_lines, 3 },  /* top-right room */
    { 2, 11, NPC_VILLAGER, 180,  80, 160, villager2_lines, 2 },  /* bottom-left room */
    { 9, 13, NPC_VILLAGER, 150, 150, 170, guard_lines,     2 },  /* near the gate */
    { 17,11, NPC_VILLAGER, 230, 200,  80, king_lines,      3 },  /* bottom-right room */
};

static const Npc *map_npcs[]    = { npcs_town, 0 };
static const int  map_npc_cnt[] = { (int)(sizeof(npcs_town) / sizeof(Npc)), 0 };

static int pending_action;   /* 0 none, 1 dialogue, 2 shop */

// TEST MAP: all features in one compact area.
// 0=grass 1=wall 2=water 3=path 4=door 5=tower 6=mountain
// Layout: walled courtyard with a central plaza, pond, towers,
// paths to all NPCs, and an exit gate at the bottom.
static const uint8_t map_town[MAP_ROWS][MAP_W] = {
    {5,1,1,1,1,1,5,0,0,0,0,0,0,5,1,1,1,1,1,5},
    {1,3,3,3,3,3,1,0,0,2,2,0,0,1,3,3,3,3,3,1},
    {1,3,0,0,0,3,1,0,0,2,2,0,0,1,3,0,0,0,3,1},
    {1,3,0,0,0,3,3,3,3,3,3,3,3,3,3,0,0,0,3,1},
    {1,3,0,0,0,3,0,0,0,0,0,0,0,3,0,0,0,0,3,1},
    {1,3,3,3,3,3,0,0,0,0,0,0,0,3,3,3,3,3,3,1},
    {5,1,1,3,1,1,0,0,0,0,0,0,0,1,1,3,1,1,1,5},
    {0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0},
    {0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0},
    {5,1,1,3,1,1,0,0,0,0,0,0,0,1,1,3,1,1,1,5},
    {1,3,3,3,3,3,0,0,0,0,0,0,0,3,3,3,3,3,3,1},
    {1,3,0,0,0,3,0,0,0,3,0,0,0,3,0,0,0,0,3,1},
    {1,3,0,0,0,3,3,3,3,3,3,3,3,3,0,0,0,0,3,1},
    {1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1},
    {5,1,1,1,1,1,1,1,4,4,4,4,1,1,1,1,1,1,1,5},
};

// Overworld: mountains (6) frame it, a lake (2), a road (3), plains (0).
static const uint8_t map_field[MAP_ROWS][MAP_W] = {
    {6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6},
    {6,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,6},
    {6,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,0,0,0,6},
    {6,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,6},
    {0,0,0,0,0,0,0,0,0,3,3,3,0,2,2,2,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0},
    {0,0,6,6,0,0,0,0,0,3,0,0,0,0,0,6,6,0,0,0},
    {0,0,6,0,0,0,0,0,0,3,0,0,0,0,0,0,6,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0},
    {6,6,6,6,6,6,6,6,6,4,4,6,6,6,6,6,6,6,6,6},
};

static const uint8_t *maps[] = {
    (const uint8_t *)map_town,
    (const uint8_t *)map_field,
};

static int current_map;
static int encounter_flag;
static int step_count;

/* Player position in world units (continuous). */
static int32_t world_x, world_z;

static uint8_t get_tile(int x, int y) {
    return maps[current_map][y * MAP_W + x];
}

static int tile_at_world(int32_t wx, int32_t wz) {
    int tx = wx / TSZ;
    int tz = wz / TSZ;
    if (tx < 0 || tx >= MAP_W || tz < 0 || tz >= MAP_ROWS) return TILE_WALL;
    return get_tile(tx, tz);
}

static int tile_blocks(int t) {
    return t == TILE_WALL || t == TILE_WATER || t == TILE_TOWER ||
           t == TILE_MOUNTAIN;
}

/* Can the player's bounding box occupy world position (wx, wz)? */
static int can_stand(int32_t wx, int32_t wz) {
    /* Sample the four corners of the player's footprint. */
    int32_t off = PLY_HW;
    if (tile_blocks(tile_at_world(wx - off, wz - off))) return 0;
    if (tile_blocks(tile_at_world(wx + off, wz - off))) return 0;
    if (tile_blocks(tile_at_world(wx - off, wz + off))) return 0;
    if (tile_blocks(tile_at_world(wx + off, wz + off))) return 0;
    return 1;
}

static void set_world_from_tile(int tx, int tz) {
    world_x = tx * TSZ + HALF_TSZ;
    world_z = tz * TSZ + HALF_TSZ;
}

void map_init(int map_id) {
    current_map = map_id;
    encounter_flag = 0;
    step_count = 0;
    set_world_from_tile(player.map_x, player.map_y);
}

/* Check for an NPC within reach when CROSS is pressed. */
static void check_npc_interaction(int (*pressed)(uint16_t)) {
    if (!pressed(PAD_CROSS)) return;

    const Npc *list = map_npcs[current_map];
    int n = map_npc_cnt[current_map];
    for (int i = 0; i < n; i++) {
        int32_t nx = list[i].tx * TSZ + HALF_TSZ;
        int32_t nz = list[i].tz * TSZ + HALF_TSZ;
        int32_t dx = world_x - nx;
        int32_t dz = world_z - nz;
        if (dx < 0) dx = -dx;
        if (dz < 0) dz = -dz;
        /* Reach a bit beyond one tile so talking from an adjacent tile works. */
        if (dx < TSZ + HALF_TSZ && dz < TSZ + HALF_TSZ) {
            if (list[i].kind == NPC_SHOP) {
                pending_action = 2;
            } else {
                dialogue_open(list[i].lines, list[i].line_count);
                pending_action = 1;
            }
            return;
        }
    }
}

int map_take_interaction(void) {
    int a = pending_action;
    pending_action = 0;
    return a;
}

void map_update(uint16_t pad, int (*pressed)(uint16_t)) {
    encounter_flag = 0;

    check_npc_interaction(pressed);

    /* ---- Camera control ---- */
    /* Right stick orbits the camera; L1/R1 are a digital fallback. */
    int rx = pad_rstick_x();
    int ry = pad_rstick_y();
    cam_yaw += (rx * YAW_SPEED) / 128;
    if (pad & PAD_L1) cam_yaw -= YAW_SPEED;
    if (pad & PAD_R1) cam_yaw += YAW_SPEED;
    cam_yaw &= (FULL_CIRCLE - 1);

    cam_height += ry / 24;
    if (pad & PAD_L2) cam_height += 6;
    if (pad & PAD_R2) cam_height -= 6;
    if (cam_height < CAM_HEIGHT_MIN) cam_height = CAM_HEIGHT_MIN;
    if (cam_height > CAM_HEIGHT_MAX) cam_height = CAM_HEIGHT_MAX;

    /* ---- Movement (relative to camera facing) ---- */
    int inx = 0, inz = 0;
    if (pad & PAD_UP)    inz += 1;
    if (pad & PAD_DOWN)  inz -= 1;
    if (pad & PAD_RIGHT) inx += 1;
    if (pad & PAD_LEFT)  inx -= 1;

    int s = isin(cam_yaw);
    int c = icos(cam_yaw);

    /* forward = (sin, cos), right = (cos, -sin) */
    int32_t dx = ((inx * c) + (inz * s)) * MOVE_SPEED >> 12;
    int32_t dz = ((-inx * s) + (inz * c)) * MOVE_SPEED >> 12;

    int moving = (inx != 0 || inz != 0);
    is_moving = moving;
    if (moving) {
        int prev = walk_phase;
        walk_phase = (walk_phase + 3200) & (FULL_CIRCLE - 1);
        /* footstep on each bob cycle (when the phase crosses a half-turn) */
        if ((prev & 0x8000) != (walk_phase & 0x8000)) audio_sfx(AUDIO_SFX_STEP);
    }

    /* Move per-axis so the player slides along walls. */
    if (dx != 0 && can_stand(world_x + dx, world_z)) world_x += dx;
    if (dz != 0 && can_stand(world_x, world_z + dz)) world_z += dz;

    /* Update tile coordinates for save / door logic. */
    player.map_x = world_x / TSZ;
    player.map_y = world_z / TSZ;
    player.current_map = current_map;

    /* Door transition: edge doors change maps. */
    int door_t = tile_at_world(world_x, world_z);
    int on_edge = (player.map_x == 0 || player.map_x == MAP_W - 1 ||
                   player.map_y == 0 || player.map_y == MAP_ROWS - 1);
    if (door_t == TILE_DOOR && on_edge) {
        if (current_map == 0) {
            current_map = 1;
            player.map_x = 9; player.map_y = 7;
        } else {
            current_map = 0;
            player.map_x = 9; player.map_y = 12;
        }
        player.current_map = current_map;
        set_world_from_tile(player.map_x, player.map_y);
        step_count = 0;
        return;
    }

    /* Random encounters on the field map while walking on grass. */
    int enc_t = tile_at_world(world_x, world_z);
    if (moving && current_map == 1 && enc_t == TILE_GRASS) {
        step_count++;
        if (step_count > 40 && (rand() % 100) < 3) {
            encounter_flag = 1;
            step_count = 0;
        }
    }
}

static void tile_color(uint8_t t, uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (t) {
        case TILE_GRASS: *r = 50;  *g = 115; *b = 55;  break;  /* courtyard lawn */
        case TILE_PATH:  *r = 150; *g = 130; *b = 80;  break;
        case TILE_WATER: *r = 30;  *g = 60;  *b = 150; break;
        case TILE_DOOR:  *r = 110; *g = 80;  *b = 50;  break;  /* gateway */
        case TILE_WALL:
        case TILE_TOWER: *r = 100; *g = 100; *b = 108; break;  /* stone floor */
        case TILE_MOUNTAIN: *r = 80; *g = 72; *b = 60; break;  /* rocky ground */
        default:         *r = 50;  *g = 115; *b = 55;  break;
    }
}

static int terrain_y(int32_t wx, int32_t wz);   /* forward decl */

/* Draw a decor prop centered on tile (tx, tz), sitting on the terrain. */
static void draw_prop(const Prop *p, uint32_t *ot, char **pri) {
    int32_t cx = p->tx * TSZ + HALF_TSZ;
    int32_t cz = p->tz * TSZ + HALF_TSZ;
    int32_t by = terrain_y(cx, cz);

    switch (p->type) {
        case PROP_TREE:
            /* textured wooden trunk + leafy green foliage (reliable) */
            eng3d_draw_box_tex(cx, by-90, cz, 35, 90, 35, tp_wood, ot, pri);
            eng3d_draw_box_tex(cx, by-270, cz, 135, 120, 135, tp_grass, ot, pri);
            break;
        case PROP_HOUSE:
            eng3d_draw_box_tex(cx, by-150, cz, 180, 150, 180, tp_brick, ot, pri);
            eng3d_draw_box_tex(cx, by-340, cz, 200, 60, 200, tp_roof, ot, pri);
            break;
        case PROP_ROCK:
            eng3d_draw_box(cx, by-70, cz, 110, 70, 90, 120, 120, 130, ot, pri);
            break;
        case PROP_CRYSTAL:
            eng3d_draw_box(cx, by-260, cz, 70, 260, 70, 90, 220, 230, ot, pri);
            break;
        case PROP_FOUNTAIN:
            eng3d_draw_box(cx, by-40, cz, 180, 40, 180, 150, 150, 160, ot, pri);
            eng3d_draw_box(cx, by-120, cz, 60, 80, 60, 40, 90, 200, ot, pri);
            break;
        case PROP_THRONE:
            eng3d_draw_box(cx, by-70, cz, 90, 70, 90, 180, 150, 40, ot, pri);
            eng3d_draw_box(cx, by-240, cz - 70, 90, 170, 25, 200, 170, 50, ot, pri);
            break;
    }
}

/* Rolling-hills height for the overworld (negative Y = up). Castle is flat. */
static int terrain_y(int32_t wx, int32_t wz) {
    if (current_map != 1) return 0;
    int a = (wx * 110) & (FULL_CIRCLE - 1);
    int b = (wz * 150) & (FULL_CIRCLE - 1);
    int d = ((wx + wz) * 70) & (FULL_CIRCLE - 1);
    int h = (isin(a) * 130 + isin(b) * 100 + isin(d) * 70) >> 12;
    return -h;
}

void map_draw(uint32_t *ot, char **pri) {
    VECTOR eye, at;
    int pty = terrain_y(world_x, world_z);   /* terrain height under player */

    /* Orbit camera. Overworld pulls back; test map stays close. */
    int s = isin(cam_yaw);
    int c = icos(cam_yaw);
    int cdist = (current_map == 1) ? 1050 : CAM_DIST;
    int chgt  = (current_map == 1 && cam_height < 760) ? 760 : cam_height;
    eye.vx = world_x - ((cdist * s) >> 12);
    eye.vy = pty - chgt;
    eye.vz = world_z - ((cdist * c) >> 12);
    at.vx  = world_x;
    at.vy  = pty - PLY_H / 2;
    at.vz  = world_z;
    eng3d_set_camera(&eye, &at);

    /* A bit of distance fog for atmosphere. */
    eng3d_set_fog(150);

    int ptx = world_x / TSZ;
    int ptz = world_z / TSZ;

    for (int tz = ptz - VIEW_TILES; tz <= ptz + VIEW_TILES; tz++) {
        if (tz < 0 || tz >= MAP_ROWS) continue;
        for (int tx = ptx - VIEW_TILES; tx <= ptx + VIEW_TILES; tx++) {
            if (tx < 0 || tx >= MAP_W) continue;

            uint8_t t = get_tile(tx, tz);
            int32_t cx = tx * TSZ + HALF_TSZ;
            int32_t cz = tz * TSZ + HALF_TSZ;

            /* Textured ground. On the overworld, sample terrain height at the
             * four shared tile corners for seamless rolling hills. */
            uint16_t ftp = (t == TILE_GRASS) ? tp_grass
                         : (t == TILE_PATH)  ? tp_stone
                         : 0;
            if (ftp && current_map == 1) {
                int h = HALF_TSZ + 6;
                int ynw = terrain_y(cx - HALF_TSZ, cz - HALF_TSZ);
                int yne = terrain_y(cx + HALF_TSZ, cz - HALF_TSZ);
                int ysw = terrain_y(cx - HALF_TSZ, cz + HALF_TSZ);
                int yse = terrain_y(cx + HALF_TSZ, cz + HALF_TSZ);
                eng3d_draw_floor_tex_h(cx, cz, h, ynw, yne, ysw, yse, ftp, 150, ot, pri);
            } else if (ftp) {
                eng3d_draw_floor_tex(cx, cz, HALF_TSZ + 6, ftp, 150, ot, pri);
            } else {
                uint8_t r, g, b;
                tile_color(t, &r, &g, &b);
                eng3d_draw_floor(cx, cz, HALF_TSZ + 6, r, g, b, ot, pri);
            }

            if (t == TILE_WALL) {
                eng3d_draw_box_tex(cx, -WALL_H / 2, cz,
                                   HALF_TSZ, WALL_H / 2, HALF_TSZ,
                                   tp_brick, ot, pri);
            } else if (t == TILE_TOWER) {
                eng3d_draw_box_tex(cx, -TOWER_H / 2, cz,
                                   HALF_TSZ, TOWER_H / 2, HALF_TSZ,
                                   tp_stone, ot, pri);
                eng3d_draw_box_tex(cx, -TOWER_H - 80, cz,
                                   HALF_TSZ + 18, 80, HALF_TSZ + 18,
                                   tp_roof, ot, pri);
            } else if (t == TILE_MOUNTAIN) {
                int my = terrain_y(cx, cz);
                eng3d_draw_box(cx, my-190, cz, HALF_TSZ, 190, HALF_TSZ,
                               95, 82, 66, ot, pri);
                eng3d_draw_box(cx, my-470, cz, 130, 100, 130,
                               110, 105, 110, ot, pri);
                eng3d_draw_box(cx, my-610, cz, 60, 55, 60,
                               210, 212, 220, ot, pri);
            }
        }
    }

    /* Decor props. */
    {
        const Prop *plist = map_props[current_map];
        int n = map_propcnt[current_map];
        for (int i = 0; i < n; i++) {
            int dxp = plist[i].tx - ptx;
            int dzp = plist[i].tz - ptz;
            if (dxp < -NPC_VIEW || dxp > NPC_VIEW) continue;
            if (dzp < -NPC_VIEW || dzp > NPC_VIEW) continue;
            draw_prop(&plist[i], ot, pri);
        }
    }

    /* NPCs. */
    {
        const Npc *list = map_npcs[current_map];
        int n = map_npc_cnt[current_map];
        for (int i = 0; i < n; i++) {
            int dxn = list[i].tx - ptx;
            int dzn = list[i].tz - ptz;
            if (dxn < -NPC_VIEW || dxn > NPC_VIEW) continue;
            if (dzn < -NPC_VIEW || dzn > NPC_VIEW) continue;
            int32_t cx = list[i].tx * TSZ + HALF_TSZ;
            int32_t cz = list[i].tz * TSZ + HALF_TSZ;
            /* placeholder white cube (real NPC model TBD) */
            int32_t by = terrain_y(cx, cz);
            eng3d_draw_box(cx, by - 150, cz, 80, 150, 80, 235, 235, 235, ot, pri);
        }
    }

    /* Draw the player: the baked 3D model (feet at y=0).
     * Face away from the camera. cam_yaw is in isin units (131072 = 360),
     * RotMatrix wants 4096 = 360, so divide by 32.
     * While walking, add a vertical bob and a slight side-to-side waddle. */
    /* animation disabled for now — static model facing away from camera */
    int player_yaw = cam_yaw / 32;
    eng3d_draw_texmesh(&model_cloud, world_x, pty, world_z, player_yaw, tp_cloud, ot, pri);

    /* ---- HUD ---- */
    char buf[40];

    /* location name, top-left */
    sprintf(buf, "%s", current_map == 0 ? "CASTLE OF AETHELGARD" : "AETHELGARD FIELDS");
    text_draw(buf, 8, 6, ot, pri);

    /* party leader vitals + gold, bottom-left panel */
    {
        TILE *panel = (TILE *)*pri;
        setTile(panel);
        setXY0(panel, 4, 204);
        setWH(panel, 168, 32);
        setRGB0(panel, 0, 0, 64);
        addPrim(ot + 8, panel);
        *pri += sizeof(TILE);

        PartyMember *m = &player.party[0];
        sprintf(buf, "%s  HP %d/%d", m->name, m->hp, m->hp_max);
        text_draw(buf, 10, 208, ot, pri);
        sprintf(buf, "MP %d/%d  G %d", m->mp, m->mp_max, (int)player.gold);
        text_draw(buf, 10, 222, ot, pri);
    }
}

int map_encounter(void) {
    return encounter_flag;
}

int map_get_zone(void) {
    return current_map;
}
