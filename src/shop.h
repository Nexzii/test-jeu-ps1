#ifndef SHOP_H
#define SHOP_H

#include <stdint.h>

void shop_open(void);
void shop_update(int (*pressed)(uint16_t));
void shop_draw(uint32_t *ot, char **pri);
int  shop_active(void);

#endif
