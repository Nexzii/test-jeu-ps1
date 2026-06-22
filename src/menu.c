#include "menu.h"
#include "player.h"
#include "text.h"
#include "audio.h"
#include <psxpad.h>
#include <stdio.h>

#define MENU_OT_BASE 128

typedef enum {
    MPAGE_MAIN,
    MPAGE_ITEMS,
    MPAGE_STATUS,
    MPAGE_OPTIONS
} MenuPage;

static MenuPage page;
static int      cursor;
static int      is_closed;

static void draw_box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                     uint32_t *ot, char **pri) {
    TILE *tile = (TILE *)*pri;
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);
    addPrim(ot + MENU_OT_BASE, tile);
    *pri += sizeof(TILE);
}

void menu_open(void) {
    page = MPAGE_MAIN;
    cursor = 0;
    is_closed = 0;
}

void menu_update(uint16_t pad, int (*pressed)(uint16_t)) {
    switch (page) {
        case MPAGE_MAIN:
            if (pressed(PAD_UP))   { cursor--; if (cursor < 0) cursor = 3; }
            if (pressed(PAD_DOWN)) { cursor++; if (cursor > 3) cursor = 0; }

            if (pressed(PAD_CROSS)) { // X
                if (cursor == 0) page = MPAGE_ITEMS;
                if (cursor == 1) page = MPAGE_STATUS;
                if (cursor == 2) page = MPAGE_OPTIONS;
                if (cursor == 3) is_closed = 1;
                cursor = 0;
            }

            if (pressed(PAD_CIRCLE) || pressed(PAD_SELECT)) { // O or Select
                is_closed = 1;
            }
            break;

        case MPAGE_OPTIONS:
            if (pressed(PAD_CIRCLE)) { page = MPAGE_MAIN; cursor = 0; break; }
            if (pressed(PAD_UP))   { cursor--; if (cursor < 0) cursor = 2; }
            if (pressed(PAD_DOWN)) { cursor++; if (cursor > 2) cursor = 0; }
            /* left/right (or X) adjust the focused setting */
            if (pressed(PAD_LEFT) || pressed(PAD_RIGHT) || pressed(PAD_CROSS)) {
                int dir = pressed(PAD_LEFT) ? -1 : 1;
                if (cursor == 0) {
                    int lvl = audio_music_level() + dir;
                    if (lvl < 0) lvl = 3; if (lvl > 3) lvl = 0;
                    audio_set_music(lvl);
                } else if (cursor == 1) {
                    audio_set_sfx(!audio_sfx_on());
                } else if (cursor == 2) {
                    audio_set_wind(!audio_wind_on());
                }
                audio_sfx(AUDIO_SFX_CONFIRM);
            }
            break;

        case MPAGE_ITEMS:
            if (pressed(PAD_CIRCLE) || pressed(PAD_CROSS)) {
                page = MPAGE_MAIN;
                cursor = 0;
            }

            if (pressed(PAD_UP))   { cursor--; if (cursor < 0) cursor = player.item_count - 1; }
            if (pressed(PAD_DOWN)) { cursor++; if (cursor >= player.item_count) cursor = 0; }

            if (pressed(PAD_CROSS)) { // Use item
                if (player.item_count > 0 && player.items[cursor].id == 0) {
                    player_heal(0, 50);
                    player.items[cursor].qty--;
                    if (player.items[cursor].qty == 0) {
                        for (int i = cursor; i < player.item_count - 1; i++)
                            player.items[i] = player.items[i+1];
                        player.item_count--;
                        if (cursor >= player.item_count && cursor > 0) cursor--;
                    }
                }
            }
            break;

        case MPAGE_STATUS:
            if (pressed(PAD_CIRCLE) || pressed(PAD_CROSS)) {
                page = MPAGE_MAIN;
                cursor = 0;
            }
            break;
    }
}

static const char *item_names[] = { "POTION", "ETHER", "PHOENIX", "ANTIDOTE" };

void menu_draw(uint32_t *ot, char **pri) {
    // Full screen dark background
    draw_box(0, 0, 320, 240, 0, 0, 40, ot, pri);

    switch (page) {
        case MPAGE_MAIN: {
            draw_box(20, 20, 120, 96, 0, 0, 80, ot, pri);
            text_draw("ITEMS",   40, 28, ot, pri);
            text_draw("STATUS",  40, 48, ot, pri);
            text_draw("OPTIONS", 40, 68, ot, pri);
            text_draw("CLOSE",   40, 88, ot, pri);
            text_draw(">", 30, 28 + cursor * 20, ot, pri);

            // Gold display
            draw_box(20, 124, 120, 26, 0, 0, 80, ot, pri);
            char buf[20];
            sprintf(buf, "GOLD: %d", (int)player.gold);
            text_draw(buf, 30, 130, ot, pri);

            // Party preview
            draw_box(160, 20, 140, 200, 0, 0, 80, ot, pri);
            for (int i = 0; i < player.party_size; i++) {
                PartyMember *m = &player.party[i];
                char line[32];
                text_draw(m->name, 170, 30 + i * 50, ot, pri);
                sprintf(line, "HP %d/%d", m->hp, m->hp_max);
                text_draw(line, 170, 42 + i * 50, ot, pri);
                sprintf(line, "MP %d/%d", m->mp, m->mp_max);
                text_draw(line, 170, 54 + i * 50, ot, pri);
            }
            break;
        }

        case MPAGE_ITEMS: {
            draw_box(20, 20, 280, 200, 0, 0, 80, ot, pri);
            text_draw("== ITEMS ==", 110, 28, ot, pri);

            if (player.item_count == 0) {
                text_draw("NO ITEMS", 100, 80, ot, pri);
            } else {
                for (int i = 0; i < player.item_count; i++) {
                    char buf[32];
                    const char *name = (player.items[i].id < 4) ? item_names[player.items[i].id] : "???";
                    sprintf(buf, "%s x%d", name, player.items[i].qty);
                    text_draw(buf, 50, 48 + i * 16, ot, pri);
                    if (i == cursor) {
                        text_draw(">", 38, 48 + i * 16, ot, pri);
                    }
                }
            }
            break;
        }

        case MPAGE_STATUS: {
            draw_box(20, 20, 280, 200, 0, 0, 80, ot, pri);
            text_draw("== STATUS ==", 105, 28, ot, pri);

            for (int i = 0; i < player.party_size; i++) {
                PartyMember *m = &player.party[i];
                int yy = 48 + i * 80;
                char buf[40];

                text_draw(m->name, 30, yy, ot, pri);
                sprintf(buf, "LV %d  XP %d/%d", m->level, (int)m->xp, (int)m->xp_next);
                text_draw(buf, 30, yy + 12, ot, pri);
                sprintf(buf, "HP %d/%d  MP %d/%d", m->hp, m->hp_max, m->mp, m->mp_max);
                text_draw(buf, 30, yy + 24, ot, pri);
                sprintf(buf, "ATK %d DEF %d MAG %d SPD %d", m->atk, m->def, m->mag, m->spd);
                text_draw(buf, 30, yy + 36, ot, pri);
            }
            break;
        }

        case MPAGE_OPTIONS: {
            draw_box(40, 40, 240, 150, 0, 0, 80, ot, pri);
            text_draw("== SOUND OPTIONS ==", 80, 50, ot, pri);

            char buf[32];
            static const char *lvl[] = { "OFF", "LOW", "MED", "HIGH" };
            sprintf(buf, "MUSIC      < %s >", lvl[audio_music_level()]);
            text_draw(buf, 64, 80, ot, pri);
            sprintf(buf, "SFX        < %s >", audio_sfx_on() ? "ON" : "OFF");
            text_draw(buf, 64, 100, ot, pri);
            sprintf(buf, "WIND       < %s >", audio_wind_on() ? "ON" : "OFF");
            text_draw(buf, 64, 120, ot, pri);
            text_draw(">", 52, 80 + cursor * 20, ot, pri);

            text_draw("LEFT/RIGHT ADJUST  O BACK", 56, 168, ot, pri);
            break;
        }
    }
}

int menu_closed(void) {
    return is_closed;
}
