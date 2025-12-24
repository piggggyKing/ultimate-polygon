#include "Scene.h"

void main_menu_handle_event(sfRenderWindow* window, sfEvent event,
							TypeText* typetext[2], Button* button[2]) {
	if(event.type == sfEvtClosed) {
		sfRenderWindow_close(window);
	}

	//Esc
	if(event.type == sfEvtKeyPressed && 
	event.key.code == sfKeyEnd) {
		is_paused = true;
	}

	//Hover//
	mousePos = sfMouse_getPositionRenderWindow(window);
	act_B = 0;
	if(sfFloatRect_contains(&button[0]->bound, mousePos.x, mousePos.y)) {
		act_B = 1;
	}
	else if(sfFloatRect_contains(&button[1]->bound, mousePos.x, mousePos.y)) {
		act_B = 2;
	}
	sfRectangleShape_setSize(button[0]->box,
		(act_B == 1) ? HOVER_BUTTON : BUTTON);
	sfRectangleShape_setSize(button[1]->box,
		(act_B == 2) ? HOVER_BUTTON : BUTTON);

	//Click//
	if(event.type == sfEvtMouseButtonPressed &&
	event.mouseButton.button == sfMouseLeft) {
		mousePos = sfMouse_getPositionRenderWindow(window);
		act_TT = 0;

		if (sfFloatRect_contains(&typetext[0]->bound, mousePos.x, mousePos.y)) {
			act_TT = 1;
		} 
		else if (sfFloatRect_contains(&typetext[1]->bound, mousePos.x, mousePos.y)) {
			act_TT = 2;
		}
        else if (sfFloatRect_contains(&button[0]->bound, mousePos.x, mousePos.y)) {
            if (typetext[0]->len > 0 && typetext[1]->len > 0) {
                sprintf(sendline, "reg user:%s pass:%s",
                        typetext[0]->buffer, typetext[1]->buffer);
                is_write = 1;
            } else {
                if (typetext[0]->len == 0)
                    sfRectangleShape_setOutlineColor(typetext[0]->box, sfRed);
                if (typetext[1]->len == 0)
                    sfRectangleShape_setOutlineColor(typetext[1]->box, sfRed);
            }
        }
        else if (sfFloatRect_contains(&button[1]->bound, mousePos.x, mousePos.y)) {
            if (typetext[0]->len > 0 && typetext[1]->len > 0) {
                sprintf(sendline, "log user:%s pass:%s",
                        typetext[0]->buffer, typetext[1]->buffer);
                is_write = 1;
            } else {
                if (typetext[0]->len == 0)
                    sfRectangleShape_setOutlineColor(typetext[0]->box, sfRed);
                if (typetext[1]->len == 0)
                    sfRectangleShape_setOutlineColor(typetext[1]->box, sfRed);
            }
        }

        if (act_TT != 0) {
            sfRectangleShape_setOutlineColor(typetext[0]->box,
                (act_TT == 1) ? sfGreen : sfWhite);
            sfRectangleShape_setOutlineColor(typetext[1]->box,
                (act_TT == 2) ? sfGreen : sfWhite);
        }
	}

	//Keyboard//
	if (event.type == sfEvtTextEntered) {
		sfUint32 code = event.text.unicode;
		if(act_TT == 1) {
			TypeText_push(typetext[0], code);
		}
		else if(act_TT == 2) {
			TypeText_push(typetext[1], code);
		}
	}
	else if (event.type == sfEvtKeyPressed && 
	event.key.code == sfKeyBackspace) {
		if(act_TT == 1) {
			TypeText_delete(typetext[0]);
		}
		else if(act_TT == 2) {
			TypeText_delete(typetext[1]);
		}
	}
}