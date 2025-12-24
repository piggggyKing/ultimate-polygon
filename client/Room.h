#ifndef ROOM_H_
#define ROOM_H_


#include <SFML/Graphics.h>
#include <stddef.h>
#include <stdlib.h>

#include "TypeText.h"
#include "Button.h"

typedef struct Room {
    sfRectangleShape* box;
    sfText* room_name;
    sfText* host_name;
    TypeText* password;
    Button* join_button;
    sfFloatRect bound;
} Room;


Room* Room_create(sfFont* font, const char* room_name_str, const char* host_name_str, sfVector2f cood);

void Room_destroy(Room* room);

void Room_setPosition(Room* room, sfVector2f pos);

void sfRenderWindow_drawRoom(sfRenderWindow* window, Room* room);

#endif //ROOM_H_