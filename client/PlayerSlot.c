#include "PlayerSlot.h"

PlayerSlot* PlayerSlot_create(sfFont* font,
                              const char* name_str,
                              const char* status_str,
                              sfVector2f pos)
{
    PlayerSlot* p = malloc(sizeof(PlayerSlot));
    if (!p) return NULL;

    // 外框
    p->box = sfRectangleShape_create();
    sfRectangleShape_setSize(p->box, (sfVector2f){ 500.f, 80.f });
    sfRectangleShape_setFillColor(p->box, (sfColor){30, 30, 30, 255});
    sfRectangleShape_setOutlineThickness(p->box, 2.f);
    sfRectangleShape_setOutlineColor(p->box, sfWhite);
    sfRectangleShape_setPosition(p->box, pos);

    // 名字
    p->name = sfText_create();
    sfText_setFont(p->name, font);
    sfText_setCharacterSize(p->name, 28);
    sfText_setFillColor(p->name, sfWhite);
    sfText_setString(p->name, name_str);
    sfText_setPosition(p->name, (sfVector2f){ pos.x + 20.f, pos.y + 10.f });

    // 狀態（Ready / Not Ready / Host 等）
    p->status = sfText_create();
    sfText_setFont(p->status, font);
    sfText_setCharacterSize(p->status, 22);
    sfText_setFillColor(p->status, sfColor_fromRGB(150, 220, 150));
    sfText_setString(p->status, status_str);
    sfText_setPosition(p->status, (sfVector2f){ pos.x + 20.f, pos.y + 45.f });

    p->bound = sfRectangleShape_getGlobalBounds(p->box);

    return p;
}

void PlayerSlot_setName(PlayerSlot* p, const char* name_str)
{
    sfText_setString(p->name, name_str);
}

void PlayerSlot_setStatus(PlayerSlot* p, const char* status_str)
{
    sfText_setString(p->status, status_str);
}

void PlayerSlot_destroy(PlayerSlot* p)
{
    if (!p) return;
    sfRectangleShape_destroy(p->box);
    sfText_destroy(p->name);
    sfText_destroy(p->status);
    free(p);
}

void sfRenderWindow_drawPlayerSlot(sfRenderWindow* window, PlayerSlot* p)
{
    sfRenderWindow_drawRectangleShape(window, p->box, NULL);
    sfRenderWindow_drawText(window, p->name, NULL);
    sfRenderWindow_drawText(window, p->status, NULL);
}
