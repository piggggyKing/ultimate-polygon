#ifndef PLAYERSLOT_H_
#define PLAYERSLOT_H_

#include <SFML/Graphics.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct PlayerSlot {
    sfRectangleShape* box;
    sfText* name;
    sfText* status;
    sfFloatRect bound;
} PlayerSlot;

PlayerSlot* PlayerSlot_create(sfFont* font, const char* name_str,
                              const char* status_str, sfVector2f pos);

void PlayerSlot_setName(PlayerSlot* p, const char* name_str);

void PlayerSlot_setStatus(PlayerSlot* p, const char* status_str);

void PlayerSlot_destroy(PlayerSlot* p);

void sfRenderWindow_drawPlayerSlot(sfRenderWindow* window, PlayerSlot* p);

#endif //PLAYERSLOT_H_