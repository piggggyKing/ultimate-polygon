#ifndef TYPETEXT_H_
#define TYPETEXT_H_

#include <SFML/Graphics.h>
#include <stddef.h>
#include <stdlib.h>

#define BASE_SIZE 32
#define CHAT_SIZE 512


typedef struct TypeText {
    sfRectangleShape* box;
	sfText* text;
    sfFloatRect bound;
	char buffer[BASE_SIZE];
	size_t len;
}TypeText;


TypeText* TypeText_create(sfFont* font, char* string, sfVector2f cood);

void TypeText_destroy(TypeText* tt);

void TypeText_push(TypeText* tt, sfUint32 code);

void TypeText_delete(TypeText* tt);

void TypeText_setPosition(TypeText* tt, sfVector2f cood);

void sfRenderWindow_drawTypeText(sfRenderWindow* window, TypeText* tt);



#endif //TYPETEXT_H_