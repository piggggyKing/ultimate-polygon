#ifndef SCENE_H_
#define SCENE_H_

#include "TypeText.h"
#include "Button.h"
#include "Room.h"

void main_menu_handle_event(sfRenderWindow* window, sfEvent event,
							TypeText* typetext[2], Button* button[2]);

#endif //SCENE_H_