#include "Button.h"

Button* Button_create(sfFont* font, char* string, sfVector2f cood) {
    Button* button = malloc(sizeof(Button));
	button->box = sfRectangleShape_create();
    sfRectangleShape_setSize(button->box, (sfVector2f){140.f, 35.f});
    sfRectangleShape_setFillColor(button->box, (sfColor){30, 30, 30, 255});
    sfRectangleShape_setOutlineThickness(button->box, 2.f);
    sfRectangleShape_setOutlineColor(button->box, sfWhite);
    button->text = sfText_create();
    sfText_setFont(button->text, font);
    sfText_setCharacterSize(button->text, 24);
    sfText_setFillColor(button->text, sfWhite);
    sfText_setString(button->text, string);

    Button_setPosition(button, cood);
    return button;
}

void Button_destroy(Button* button) {
	sfText_destroy(button->text);
	sfRectangleShape_destroy(button->box);
	free(button);
}

void Button_setPosition(Button* button, sfVector2f cood) {
    sfText_setPosition(button->text, (sfVector2f){cood.x+10.f, cood.y+2.f});
	sfRectangleShape_setPosition(button->box, (sfVector2f){cood.x, cood.y});
    button->bound = sfRectangleShape_getGlobalBounds(button->box);
}

void sfRenderWindow_drawButton(sfRenderWindow* window, Button* button) {
    sfRenderWindow_drawRectangleShape(window, button->box, NULL);
    sfRenderWindow_drawText(window, button->text, NULL);
}