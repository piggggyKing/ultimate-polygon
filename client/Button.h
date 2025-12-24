#ifndef BUTTON_H_
#define BUTTON_H_

#include <SFML/Graphics.h>
#include <stddef.h>
#include <stdlib.h>

#define BUTTON_SIZE 16
#define BUTTON_WIDTH 64
#define BUTTON_HEIGHT 16


typedef struct Button {
    sfRectangleShape* box;
    sfText* text;
    sfFloatRect bound;
}Button;


Button* Button_create(sfFont* font, char* string, sfVector2f cood);

void Button_destroy(Button* button);

void Button_setPosition(Button* button, sfVector2f cood);

void sfRenderWindow_drawButton(sfRenderWindow* window, Button* button);

#endif //BUTTON_H