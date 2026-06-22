#include "shop.h"
#include "player.h"
#include "text.h"
#include <psxgpu.h>
#include <psxpad.h>
#include <stdio.h>

#define SHOP_PANEL_OT 16

typedef struct {
    const char *name;
    uint8_t     item_id;
    int32_t     price;
} ShopItem;

/* Item ids match menu.c item_names: 0 POTION, 1 ETHER, 2 PHOENIX, 3 ANTIDOTE */
static const ShopItem stock[] = {
    { "POTION",  0,  20 },
    { "ETHER",   1,  40 },
    { "PHOENIX", 2, 100 },
    { "ANTIDOTE",3,  15 },
};
#define STOCK_COUNT (int)(sizeof(stock) / sizeof(ShopItem))

static int shop_cursor;
static int shop_is_active;
static int flash_timer;   /* brief "bought!" / "no gold" feedback */
static int flash_ok;

void shop_open(void) {
    shop_cursor = 0;
    shop_is_active = 1;
    flash_timer = 0;
}

void shop_update(int (*pressed)(uint16_t)) {
    if (pressed(PAD_UP))   { shop_cursor--; if (shop_cursor < 0) shop_cursor = STOCK_COUNT - 1; }
    if (pressed(PAD_DOWN)) { shop_cursor++; if (shop_cursor >= STOCK_COUNT) shop_cursor = 0; }

    if (pressed(PAD_CROSS)) {
        const ShopItem *it = &stock[shop_cursor];
        if (player.gold >= it->price) {
            player.gold -= it->price;
            player_add_item(it->item_id, 1);
            flash_ok = 1;
        } else {
            flash_ok = 0;
        }
        flash_timer = 30;
    }

    if (pressed(PAD_CIRCLE) || pressed(PAD_SELECT)) {
        shop_is_active = 0;
    }

    if (flash_timer > 0) flash_timer--;
}

static void box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b,
                uint32_t *ot, char **pri) {
    TILE *t = (TILE *)*pri;
    setTile(t);
    setXY0(t, x, y);
    setWH(t, w, h);
    setRGB0(t, r, g, b);
    addPrim(ot + SHOP_PANEL_OT, t);
    *pri += sizeof(TILE);
}

void shop_draw(uint32_t *ot, char **pri) {
    box(0, 0, 320, 240, 0, 0, 30, ot, pri);
    box(24, 20, 272, 200, 0, 0, 70, ot, pri);

    text_draw("== ITEM SHOP ==", 100, 28, ot, pri);

    char buf[40];
    sprintf(buf, "GOLD: %d", (int)player.gold);
    text_draw(buf, 40, 44, ot, pri);

    for (int i = 0; i < STOCK_COUNT; i++) {
        sprintf(buf, "%-9s %d G", stock[i].name, (int)stock[i].price);
        text_draw(buf, 56, 72 + i * 18, ot, pri);
        if (i == shop_cursor)
            text_draw(">", 44, 72 + i * 18, ot, pri);
    }

    if (flash_timer > 0)
        text_draw(flash_ok ? "PURCHASED!" : "NOT ENOUGH GOLD", 70, 156, ot, pri);

    text_draw("X=BUY  O=EXIT", 90, 196, ot, pri);
}

int shop_active(void) {
    return shop_is_active;
}
