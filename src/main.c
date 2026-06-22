#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxetc.h>

#include "engine3d.h"
#include "input.h"
#include "player.h"
#include "battle.h"
#include "map.h"
#include "text.h"
#include "menu.h"
#include "dialogue.h"
#include "shop.h"
#include "audio.h"
#include "textures.h"

/* SCREEN_W/H and OT_LEN come from engine3d.h */

typedef enum {
    STATE_TITLE,
    STATE_INTRO,
    STATE_MAP,
    STATE_BATTLE,
    STATE_MENU,
    STATE_DIALOGUE,
    STATE_SHOP
} GameState;

/* Full-screen subtractive fade rectangle (darkens everything behind it). */
typedef struct {
    uint32_t tag;
    uint32_t tpage;
    uint8_t  r0, g0, b0, code;
    int16_t  x0, y0;
    int16_t  w, h;
} FADERECT;

/* Transition / fade state machine. */
static int        fade = 256;       /* 0 = clear, 256 = full black */
static int        fade_phase = 2;   /* 0 idle, 1 fading out, 2 fading in */
static GameState  pending_state;
#define FADE_SPEED 18

// Double buffer
typedef struct {
    DISPENV disp;
    DRAWENV draw;
    uint32_t ot[OT_LEN];
    char     pri[200000];
} FrameBuffer;

static FrameBuffer fb[2];
static int         db = 0;
static char       *next_pri;
static GameState   state;
static uint16_t    pad_data;
static uint16_t    pad_old;
static uint8_t     pad_buff[2][34];
static int         stick_rx, stick_ry, stick_lx, stick_ly;

#define STICK_DEADZONE 24

void init_gpu(void) {
    ResetGraph(0);

    SetDefDispEnv(&fb[0].disp, 0,   0, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&fb[0].draw, 0, 240, SCREEN_W, SCREEN_H);
    SetDefDispEnv(&fb[1].disp, 0, 240, SCREEN_W, SCREEN_H);
    SetDefDrawEnv(&fb[1].draw, 0,   0, SCREEN_W, SCREEN_H);

    /* Sky-blue background clear, with dithering for smooth 3D shading. */
    fb[0].draw.isbg = 1;
    fb[1].draw.isbg = 1;
    fb[0].draw.dtd  = 1;
    fb[1].draw.dtd  = 1;
    setRGB0(&fb[0].draw, 30, 60, 110);
    setRGB0(&fb[1].draw, 30, 60, 110);

    SetDispMask(1);

    /* Initialize the 3D geometry engine (GTE). */
    eng3d_init();
}

void flip_buffers(void) {
    DrawSync(0);
    VSync(0);

    PutDispEnv(&fb[db].disp);
    PutDrawEnv(&fb[db].draw);

    ClearOTagR(fb[db].ot, OT_LEN);
    next_pri = fb[db].pri;

    db ^= 1;
}

/* Draw the full-screen fade overlay. Must be added to ot[0] BEFORE anything
 * else in the frame so it ends up frontmost and darkens the whole scene. */
static void draw_fade(void) {
    int level = fade;
    if (level <= 0) return;
    if (level > 255) level = 255;

    FADERECT *f = (FADERECT *)next_pri;
    f->tag   = 0x04000000;       /* 4-word primitive */
    f->tpage = 0xe1000040;       /* draw mode: subtractive (B - F) blending */
    f->code  = 0x62;             /* semi-transparent flat rectangle */
    f->r0 = f->g0 = f->b0 = (uint8_t)level;
    f->x0 = 0; f->y0 = 0;
    f->w = SCREEN_W; f->h = SCREEN_H;
    addPrim(fb[1-db].ot, f);
    next_pri += sizeof(FADERECT);
}

/* Begin a fade-out; the actual state switch happens at full black. */
static void transition_to(GameState s) {
    if (fade_phase != 0) return;
    pending_state = s;
    fade_phase = 1;
}

static int apply_deadzone(int v) {
    if (v > -STICK_DEADZONE && v < STICK_DEADZONE) return 0;
    return v;
}

static int pad_kind;   /* last seen controller type nibble, for debugging */

void read_pad(void) {
    pad_old = pad_data;
    PADTYPE *pad = (PADTYPE *)pad_buff[0];

    stick_rx = stick_ry = stick_lx = stick_ly = 0;
    pad_kind = pad->stat ? -1 : pad->type;

    if (pad->stat == 0) {
        pad_data = ~pad->btn;
        /* Read sticks whenever the payload actually carries analog data
         * (len >= 3 half-words) or the pad reports an analog type. This is
         * more robust than checking the type alone. */
        if (pad->len >= 3 ||
            pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK) {
            stick_rx = apply_deadzone(pad->rs_x - 128);
            stick_ry = apply_deadzone(pad->rs_y - 128);
            stick_lx = apply_deadzone(pad->ls_x - 128);
            stick_ly = apply_deadzone(pad->ls_y - 128);
        }
    } else {
        pad_data = 0;
    }
}

int pad_rstick_x(void) { return stick_rx; }
int pad_rstick_y(void) { return stick_ry; }
int pad_lstick_x(void) { return stick_lx; }
int pad_lstick_y(void) { return stick_ly; }
int pad_type_dbg(void) { return pad_kind; }

int pad_pressed(uint16_t btn) {
    return (pad_data & btn) && !(pad_old & btn);
}

int pad_held(uint16_t btn) {
    return (pad_data & btn);
}

// ---- TITLE SCREEN ----

static int title_angle;   /* auto-orbiting camera */
static int title_cursor;  /* 0 = NEW GAME, 1 = CONTINUE */
static int title_blink;

static void title_panel(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    TILE *t = (TILE *)next_pri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, r, g, b);
    addPrim(fb[1-db].ot + 8, t);
    next_pri += sizeof(TILE);
}

/* A small castle that the camera slowly orbits behind the menu. */
static void title_castle_3d(void) {
    VECTOR eye, at;
    int s = isin(title_angle), c = icos(title_angle);
    at.vx = 0; at.vy = -360; at.vz = 0;
    eye.vx = -((1500 * s) >> 12);
    eye.vy = -780;
    eye.vz = -((1500 * c) >> 12);
    eng3d_set_camera(&eye, &at);

    uint32_t *ot = fb[1-db].ot;

    /* ground platform */
    for (int gz = -2; gz <= 2; gz++)
        for (int gx = -2; gx <= 2; gx++)
            eng3d_draw_floor(gx * 420, gz * 420, 216,
                             ((gx + gz) & 1) ? 60 : 80, 90, ((gx + gz) & 1) ? 60 : 80,
                             ot, &next_pri);

    /* central keep */
    eng3d_draw_box(0, -300, 0, 230, 300, 230, 120, 118, 128, ot, &next_pri);
    eng3d_draw_box(0, -640, 0, 250, 40, 250, 150, 50, 45, ot, &next_pri);

    /* four corner towers with red roofs */
    const int tp[4][2] = { {-560,-560}, {560,-560}, {-560,560}, {560,560} };
    for (int i = 0; i < 4; i++) {
        int x = tp[i][0], z = tp[i][1];
        eng3d_draw_box(x, -430, z, 150, 430, 150, 135, 132, 142, ot, &next_pri);
        eng3d_draw_box(x, -930, z, 168, 90, 168, 170, 50, 45, ot, &next_pri);
    }
}

void title_update(int can_input) {
    title_angle = (title_angle + 70) & (131072 - 1);
    title_blink++;
    if (!can_input) return;

    if (pad_pressed(PAD_UP) || pad_pressed(PAD_DOWN))
        title_cursor ^= 1;

    if (pad_pressed(PAD_START) || pad_pressed(PAD_CROSS))
        transition_to(STATE_INTRO);
}

void title_draw(void) {
    title_castle_3d();

    title_panel(54, 18, 212, 34, 20, 20, 70);
    text_draw("CRYSTAL QUEST", 92, 28, fb[1-db].ot, &next_pri);

    title_panel(108, 150, 104, 52, 0, 0, 60);
    text_draw("NEW GAME", 124, 158, fb[1-db].ot, &next_pri);
    text_draw("CONTINUE", 124, 178, fb[1-db].ot, &next_pri);
    text_draw(">", 116, 158 + title_cursor * 20, fb[1-db].ot, &next_pri);

    if (title_blink & 16)
        text_draw("PRESS START", 116, 216, fb[1-db].ot, &next_pri);
}

// ---- INTRO CINEMATIC ----

static int intro_page;
static int intro_blink;

/* Story pages — each is up to 4 centered lines. */
static const char *const intro_text[][4] = {
    { "FOR A THOUSAND YEARS,",   "THE CRYSTAL OF DAWN",     "SHIELDED THE KINGDOM",   "OF AETHELGARD." },
    { "BUT ON THE BLOOD MOON",   "THE CRYSTAL WAS STOLEN",  "AND DARKNESS SWALLOWED", "THE LAND." },
    { "MONSTERS ROAM THE FIELDS","THE PEOPLE HIDE BEHIND",  "THE CASTLE WALLS,",      "PRAYING FOR A HERO." },
    { "YOU ARE ZION,",           "A YOUNG KNIGHT.",         "THE KING SUMMONS YOU",   "TO RECLAIM THE DAWN." },
    { "D-PAD: MOVE",             "X: TALK / CONFIRM",       "SELECT: MENU",           "L1 R1: CAMERA" },
};
#define INTRO_PAGES (int)(sizeof(intro_text) / sizeof(intro_text[0]))

static void intro_enter(void) {
    intro_page = 0;
    intro_blink = 0;
}

static void intro_update(int can_input) {
    intro_blink++;
    if (!can_input) return;

    if (pad_pressed(PAD_CROSS) || pad_pressed(PAD_START)) {
        intro_page++;
        if (intro_page >= INTRO_PAGES) {
            player_init();
            map_init(0);
            transition_to(STATE_MAP);
        }
    }
}

static void intro_draw(void) {
    uint32_t *ot = fb[1-db].ot;

    /* dark backdrop */
    TILE *bg = (TILE *)next_pri;
    setTile(bg); setXY0(bg, 0, 0); setWH(bg, SCREEN_W, SCREEN_H);
    setRGB0(bg, 4, 6, 24);
    addPrim(ot + 200, bg);
    next_pri += sizeof(TILE);

    if (intro_page < INTRO_PAGES) {
        const char *const *p = intro_text[intro_page];
        for (int i = 0; i < 4; i++)
            if (p[i] && p[i][0])
                text_draw(p[i], 160 - (int)(strlen(p[i]) * 4), 88 + i * 18,
                          ot, &next_pri);
    }

    if (intro_blink & 16)
        text_draw("PRESS X", 130, 200, ot, &next_pri);
}

// ---- MAIN LOOP ----

int main(void) {
    init_gpu();
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
    StartPAD();
    ChangeClearPAD(1);

    FntLoad(960, 0);

    text_init();
    audio_init();
    textures_load();
    state = STATE_TITLE;

    while (1) {
        flip_buffers();
        read_pad();
        audio_tick();

        /* ---- Fade / transition machine ---- */
        if (fade_phase == 1) {                 /* fading out */
            fade += FADE_SPEED;
            if (fade >= 256) {
                fade = 256;
                state = pending_state;         /* switch at full black */
                if (state == STATE_INTRO) intro_enter();
                fade_phase = 2;
            }
        } else if (fade_phase == 2) {           /* fading in */
            fade -= FADE_SPEED;
            if (fade <= 0) { fade = 0; fade_phase = 0; }
        }
        int can_input = (fade_phase == 0);

        /* Fade overlay first so it stays frontmost. */
        draw_fade();

        switch (state) {
            case STATE_TITLE:
                title_update(can_input);
                title_draw();
                break;

            case STATE_INTRO:
                intro_update(can_input);
                intro_draw();
                break;

            case STATE_MAP: {
                if (can_input) map_update(pad_data, pad_pressed);
                map_draw(fb[1-db].ot, &next_pri);

                if (can_input) {
                    int act = map_take_interaction();
                    if (act == 1) {
                        state = STATE_DIALOGUE;
                    } else if (act == 2) {
                        shop_open();
                        state = STATE_SHOP;
                    } else if (pad_pressed(PAD_SELECT)) {
                        menu_open();
                        state = STATE_MENU;
                    } else if (map_encounter()) {
                        battle_init(map_get_zone());
                        transition_to(STATE_BATTLE);
                    }
                }
                break;
            }

            case STATE_DIALOGUE:
                map_draw(fb[1-db].ot, &next_pri);   /* frozen world behind */
                dialogue_update(pad_pressed);
                dialogue_draw(fb[1-db].ot, &next_pri);
                if (!dialogue_active()) state = STATE_MAP;
                break;

            case STATE_SHOP:
                shop_update(pad_pressed);
                shop_draw(fb[1-db].ot, &next_pri);
                if (!shop_active()) state = STATE_MAP;
                break;

            case STATE_BATTLE:
                if (can_input) battle_update(pad_data, pad_pressed);
                battle_draw(fb[1-db].ot, &next_pri);

                if (can_input && battle_is_over()) {
                    player_add_xp(battle_get_xp());
                    player_add_gold(battle_get_gold());
                    transition_to(STATE_MAP);
                }
                break;

            case STATE_MENU:
                menu_update(pad_data, pad_pressed);
                menu_draw(fb[1-db].ot, &next_pri);
                if (menu_closed()) state = STATE_MAP;
                break;
        }

        DrawOTag(fb[1-db].ot + (OT_LEN - 1));
    }

    return 0;
}
