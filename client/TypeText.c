#include "TypeText.h"

TypeText* TypeText_create(sfFont* font, char* string, sfVector2f cood) {
	TypeText* tt = malloc(sizeof(TypeText));
	tt->box = sfRectangleShape_create();
    sfRectangleShape_setSize(tt->box, (sfVector2f){400.f, 40.f});
    sfRectangleShape_setFillColor(tt->box, (sfColor){30, 30, 30, 255});
    sfRectangleShape_setOutlineThickness(tt->box, 2.f);
    sfRectangleShape_setOutlineColor(tt->box, sfWhite);
    tt->text = sfText_create();
    sfText_setFont(tt->text, font);
    sfText_setCharacterSize(tt->text, 24);
    sfText_setFillColor(tt->text, sfWhite);
    sfText_setString(tt->text, string);
	tt->len = 0;

    TypeText_setPosition(tt, cood);
    return tt;
}

void TypeText_destroy(TypeText* tt) {
	sfText_destroy(tt->text);
	sfRectangleShape_destroy(tt->box);
	free(tt);
}

void TypeText_push(TypeText* tt, sfUint32 code) {
    if (code >= 32 && code <= 126 && tt->len < sizeof(tt->buffer) - 1) {
        tt->buffer[tt->len++] = (char)code;
        tt->buffer[tt->len] = '\0';
        sfText_setString(tt->text, tt->buffer);
    }
}

void TypeText_delete(TypeText* tt) {
    if (tt->len > 0) {
        tt->len--;
        tt->buffer[tt->len] = '\0';
        sfText_setString(tt->text, tt->buffer);
    }
}

void TypeText_setPosition(TypeText* tt, sfVector2f cood) {
    sfText_setPosition(tt->text, (sfVector2f){cood.x+10.f, cood.y+2.f});
	sfRectangleShape_setPosition(tt->box, (sfVector2f){cood.x, cood.y});
    tt->bound = sfRectangleShape_getGlobalBounds(tt->box);
}

void sfRenderWindow_drawTypeText(sfRenderWindow* window, TypeText* tt) {
    sfRenderWindow_drawRectangleShape(window, tt->box, NULL);
    sfRenderWindow_drawText(window, tt->text, NULL);
}
