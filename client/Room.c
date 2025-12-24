#include "Room.h"

Room* Room_create(sfFont* font, const char* room_name_str, const char* host_name_str, sfVector2f cood)
{
    Room* room = malloc(sizeof(Room));

    room->box = sfRectangleShape_create();
    sfRectangleShape_setSize(room->box, (sfVector2f){600.f, 140.f});
    sfRectangleShape_setFillColor(room->box, (sfColor){20, 20, 20, 255});
    sfRectangleShape_setOutlineThickness(room->box, 2.f);
    sfRectangleShape_setOutlineColor(room->box, sfWhite);

    room->room_name = sfText_create();
    sfText_setFont(room->room_name, font);
    sfText_setCharacterSize(room->room_name, 26);
    sfText_setFillColor(room->room_name, sfWhite);
    sfText_setString(room->room_name, room_name_str);

    room->host_name = sfText_create();
    sfText_setFont(room->host_name, font);
    sfText_setCharacterSize(room->host_name, 20);
    sfText_setFillColor(room->host_name, sfColor_fromRGB(180, 180, 180));
    sfText_setString(room->host_name, host_name_str);

    sfVector2f pwdPos = { cood.x + 20.f, cood.y + 70.f };
    room->password = TypeText_create(font, "room_password", pwdPos);

    sfVector2f btnPos = { cood.x + 600.f - 160.f, cood.y + 75.f };
    room->join_button = Button_create(font, "Join", btnPos);

    Room_setPosition(room, cood);

    room->bound = sfRectangleShape_getGlobalBounds(room->box);

    return room;
}

void Room_destroy(Room* room) {
    sfText_destroy(room->room_name);
    sfText_destroy(room->host_name);
    sfRectangleShape_destroy(room->box);
    TypeText_destroy(room->password);
    Button_destroy(room->join_button);
}

void Room_setPosition(Room* room, sfVector2f pos)
{
    sfRectangleShape_setPosition(room->box, pos);

    sfVector2f namePos = { pos.x + 20.f, pos.y + 15.f };
    sfText_setPosition(room->room_name, namePos);

    sfVector2f hostPos = { pos.x + 20.f, pos.y + 45.f };
    sfText_setPosition(room->host_name, hostPos);

    sfVector2f pwdPos = { pos.x + 20.f, pos.y + 80.f };
    TypeText_setPosition(room->password, pwdPos);

    sfVector2f btnPos = { pos.x + 600.f - 160.f, pos.y + 80.f };
    Button_setPosition(room->join_button, btnPos);

    room->bound = sfRectangleShape_getGlobalBounds(room->box);
}

void sfRenderWindow_drawRoom(sfRenderWindow* window, Room* room) {
    sfRenderWindow_drawRectangleShape(window, room->box, NULL);
    sfRenderWindow_drawText(window, room->room_name, NULL);
    sfRenderWindow_drawText(window, room->host_name, NULL);
    sfRenderWindow_drawTypeText(window, room->password);
    sfRenderWindow_drawButton(window, room->join_button);
}