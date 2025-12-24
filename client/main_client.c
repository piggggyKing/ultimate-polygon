#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <termios.h>
#include <SFML/Graphics.h>
#include <SFML/Window.h>

#include "TypeText.h"
#include "Button.h"
#include "Room.h"
#include "PlayerSlot.h"

#define	SA	struct sockaddr
#define MAXLINE 4096
#define MAX_ROOM 32
#define BUTTON (sfVector2f){140.f, 35.f}
#define HOVER_BUTTON (sfVector2f){150.f, 40.f}

//MAIN_MENU
TypeText* username;
TypeText* password;
Button* reg;
Button* login;
//GAME_LOBBY
Button* prev;
Button* next;
Button* create_room;
TypeText* room_name;
TypeText* room_pass;
Room* lobby_rooms[5];
// GAME_ROOM
PlayerSlot* room_players[4];
Button*     btn_ready;
Button*     btn_exit;

//variable
typedef enum Scene {
    MAIN_MENU,
    GAME_LOBBY,
    GAME_ROOM,
    GAME_PLAYING,
    GAME_MODE_A,
    GAME_MODE_B
} Scene;

typedef struct {
    int  in_use;
    char name[64];
    char host[64];
} RoomInfo;


Scene prev_scene = MAIN_MENU;
Scene scene = MAIN_MENU;
RoomInfo all_rooms[MAX_ROOM];
int rooms_offset = 0;
int rooms_max_index = -1;
bool is_paused = false;
bool is_write = false;
int act_TT = 0;
int act_B = 0;
int act_room = 0;
int my_slot = 0;
int n;
bool is_ready = false;
float g_move_send_accum = 0.f;   // 用來每隔固定時間送一次 move

sfColor main_menu_bg = {39, 16, 0, 0};
sfColor game_lobby_bg = {39, 16, 0, 0};
sfVector2i mousePos;

char sendline[MAXLINE], recvline[MAXLINE];
int bytes;


typedef struct {
    float x, y;          // 目前用來畫的座標（插值後）
    float target_x, target_y;  // server 最新告訴我的座標
    int   alive;     // 1: 還在場上, 0: 死了(之後可以用)
    int   has_bomb;  // 1: 手上有炸彈, 0: 沒有 (之後接 server)
} LocalPlayerState;

LocalPlayerState g_players[4];
sfCircleShape*   g_player_shapes[4];
int              g_slot_active[4];   // 1: 這個 slot 有玩家, 0: 空

// 分數
int    g_scores[4] = {0, 0, 0, 0};
sfText *g_score_text = NULL;

void* createwindow(int FPS) {
	sfVideoMode mode = {1440, 900, 32};
    sfRenderWindow* window = sfRenderWindow_create(
							mode, "ULTIMATE POLYGON", sfClose, NULL);
	if(!window) printf("Failed to create window\n");
	sfRenderWindow_setFramerateLimit(window, FPS);
	return window;
}

void* loadfont() {
	sfFont* font = sfFont_createFromFile("darkforest.ttf");
    if (!font) printf("Failed to load font\n");
	return font;
}

void update_lobby_view_from_all() {
    for (int i = 0; i < 5; ++i) {
        int rid = rooms_offset + i;
        if (!lobby_rooms[i]) continue;

        if (rid >= 0 && rid < MAX_ROOM && all_rooms[rid].in_use) {
            sfText_setString(lobby_rooms[i]->room_name, all_rooms[rid].name);
            sfText_setString(lobby_rooms[i]->host_name, all_rooms[rid].host);
        } else {
            sfText_setString(lobby_rooms[i]->room_name, "Empty");
            sfText_setString(lobby_rooms[i]->host_name, "-");
            lobby_rooms[i]->password->len = 0;
            lobby_rooms[i]->password->buffer[0] = '\0';
            sfText_setString(lobby_rooms[i]->password->text, "");
        }
    }
}

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

void game_lobby_handle_event(sfRenderWindow* window, sfEvent event,
                             TypeText* typetext[2], Room* rooms[5],
                             Button* nav[3]) {
    // Close
    if (event.type == sfEvtClosed) {
        sfRenderWindow_close(window);
    }

    // Esc
    if (event.type == sfEvtKeyPressed &&
        event.key.code == sfKeyEnd) {
        is_paused = true;
    }

    // =========================
    // Hover：房間 join 按鈕 + 下方 nav 按鈕
    // =========================
    mousePos = sfMouse_getPositionRenderWindow(window);

    int hover_join = -1;    // 哪一個房間的 join 被 hover，-1 表示沒有
    int hover_nav  = -1;    // 哪一個 nav 被 hover，0:Prev 1:Next 2:Refresh 3:Create

    // 房間 join 按鈕 hover
    for (int i = 0; i < 5; ++i) {
        if (!rooms[i]) continue;
        if (sfFloatRect_contains(&rooms[i]->join_button->bound,
                                 (float)mousePos.x, (float)mousePos.y)) {
            hover_join = i;
        }
    }

    // 下方按鈕 hover（Prev / Next / Create）
    for (int i = 0; i < 3; ++i) {
        if (!nav[i]) continue;
        if (sfFloatRect_contains(&nav[i]->bound,
                                 (float)mousePos.x, (float)mousePos.y)) {
            hover_nav = i;
        }
    }

    // 根據 hover 改變 join 按鈕外觀
    for (int i = 0; i < 5; ++i) {
        if (!rooms[i]) continue;
        sfRectangleShape_setSize(rooms[i]->join_button->box,
            (i == hover_join) ? HOVER_BUTTON : BUTTON);
    }

    // 根據 hover 改變下方按鈕外觀
    for (int i = 0; i < 3; ++i) {
        if (!nav[i]) continue;
        sfRectangleShape_setSize(nav[i]->box,
            (i == hover_nav) ? HOVER_BUTTON : BUTTON);
    }

    // =========================
    // Mouse Click：房間密碼欄 / join / Prev / Next / Refresh / Create / create-room typetext
    // =========================
    if (event.type == sfEvtMouseButtonPressed &&
        event.mouseButton.button == sfMouseLeft) {

        mousePos = sfMouse_getPositionRenderWindow(window);

        int clicked_create_tt     = 0;   // 是否點到「創房的 typetext」
        int clicked_room_password = -1;
        int clicked_room_join     = -1;
        int clicked_nav           = -1;

        // 先檢查：有沒有點到「創建房間用的 2 個 TypeText」
        if (sfFloatRect_contains(&typetext[0]->bound,
                                 (float)mousePos.x, (float)mousePos.y)) {
            act_TT     = 1;   // 編輯 typetext[0]
            act_room   = -1;  // 取消房間密碼的選取
            clicked_create_tt = 1;
        } else if (sfFloatRect_contains(&typetext[1]->bound,
                                        (float)mousePos.x, (float)mousePos.y)) {
            act_TT     = 2;   // 編輯 typetext[1]
            act_room   = -1;
            clicked_create_tt = 1;
        }

        // 更新「創房用欄位」的外框顏色
        sfRectangleShape_setOutlineColor(typetext[0]->box,
            (act_TT == 1) ? sfGreen : sfWhite);
        sfRectangleShape_setOutlineColor(typetext[1]->box,
            (act_TT == 2) ? sfGreen : sfWhite);

        // 如果點到的是創房欄位，就不用再檢查房間/按鈕
        if (!clicked_create_tt) {
            // 有沒有點到某個房間的密碼輸入欄
            for (int i = 0; i < 5; ++i) {
                if (!rooms[i]) continue;
                if (sfFloatRect_contains(&rooms[i]->password->bound,
                                         (float)mousePos.x, (float)mousePos.y)) {
                    clicked_room_password = i;
                    break;
                }
            }

            // 有沒有點到某個房間的 join 按鈕
            if (clicked_room_password == -1) {
                for (int i = 0; i < 5; ++i) {
                    if (!rooms[i]) continue;
                    if (sfFloatRect_contains(&rooms[i]->join_button->bound,
                                             (float)mousePos.x, (float)mousePos.y)) {
                        clicked_room_join = i;
                        break;
                    }
                }
            }

            // 有沒有點到 Prev / Next / Create 按鈕
            if (clicked_room_password == -1 && clicked_room_join == -1) {
                for (int i = 0; i < 3; ++i) {
                    if (!nav[i]) continue;
                    if (sfFloatRect_contains(&nav[i]->bound,
                                             (float)mousePos.x, (float)mousePos.y)) {
                        clicked_nav = i;
                        break;
                    }
                }
            }

            // 1) 點到房間密碼欄位 → 設定 active room
            if (clicked_room_password != -1) {
                act_room = clicked_room_password;
                act_TT   = 0;  // 不再編輯創房欄位

                // 更新每個房間密碼框線顏色
                for (int i = 0; i < 5; ++i) {
                    if (!rooms[i]) continue;
                    sfRectangleShape_setOutlineColor(rooms[i]->password->box,
                        (i == act_room) ? sfGreen : sfWhite);
                }

                // 創房欄位外框恢復白色
                sfRectangleShape_setOutlineColor(typetext[0]->box, sfWhite);
                sfRectangleShape_setOutlineColor(typetext[1]->box, sfWhite);
            }
			// 2) 點到 join 按鈕 → 送加入房間的指令
			else if (clicked_room_join != -1) {
				int slot = clicked_room_join;  // 目前頁上的第幾格 (0..4)
				int room_id = rooms_offset + slot;  // 對應全域 room id

				sprintf(sendline, "join room:%d pass:%s",
						room_id, rooms[slot]->password->buffer);
				is_write = 1;
			}
            // 3) Prev / Next / Create
			else if (clicked_nav != -1) {
				if (clicked_nav == 0) {          // Prev
					if (rooms_offset >= 5) {
						rooms_offset -= 5;
						update_lobby_view_from_all();
					}
					// 不用送給 server
				}
				else if (clicked_nav == 1) {     // Next
					if (rooms_offset + 5 <= rooms_max_index) {
						rooms_offset += 5;
						update_lobby_view_from_all();
					}
				}
				else if (clicked_nav == 2) {     // Create
					// Create：檢查創房用的 name / pass 不為空
					if (typetext[0]->len > 0 && typetext[1]->len > 0) {
						sprintf(sendline, "room create name:%s pass:%s",
								typetext[0]->buffer, typetext[1]->buffer);
						is_write = 1;
					} else {
						if (typetext[0]->len == 0)
							sfRectangleShape_setOutlineColor(typetext[0]->box, sfRed);
						if (typetext[1]->len == 0)
							sfRectangleShape_setOutlineColor(typetext[1]->box, sfRed);
					}
				}
			}
        }
    }

    // =========================
    // Keyboard：輸入/刪除（房間密碼 或 創房欄位）
    // =========================
    if (event.type == sfEvtTextEntered) {
        sfUint32 code = event.text.unicode;

        // 先看是不是在編輯某個房間的密碼
        if (act_room >= 0 && act_room < 5 && rooms[act_room]) {
            TypeText_push(rooms[act_room]->password, code);
        }
        // 否則看是不是在編輯創房欄位
        else if (act_TT == 1) {
            TypeText_push(typetext[0], code);
        }
        else if (act_TT == 2) {
            TypeText_push(typetext[1], code);
        }
    }
    else if (event.type == sfEvtKeyPressed &&
             event.key.code == sfKeyBackspace) {

        if (act_room >= 0 && act_room < 5 && rooms[act_room]) {
            TypeText_delete(rooms[act_room]->password);
        }
        else if (act_TT == 1) {
            TypeText_delete(typetext[0]);
        }
        else if (act_TT == 2) {
            TypeText_delete(typetext[1]);
        }
    }
}

void game_room_handle_event(sfRenderWindow* window, sfEvent event,
                            PlayerSlot* players[4],
                            Button* btn_ready, Button* btn_exit) {
    // 關窗
    if (event.type == sfEvtClosed) {
        sfRenderWindow_close(window);
    }

    // Esc → 退出房間
    if (event.type == sfEvtKeyPressed &&
        event.key.code == sfKeyEscape) {
        snprintf(sendline, sizeof(sendline), "room leave\n");
        is_write = 1;
    }

    // Hover 兩顆按鈕
    mousePos = sfMouse_getPositionRenderWindow(window);
    int hover_btn = 0; // 0: none, 1: btn_ready, 2: btn_exit

    if (sfFloatRect_contains(&btn_ready->bound,
                             (float)mousePos.x, (float)mousePos.y)) {
        hover_btn = 1;
    } else if (sfFloatRect_contains(&btn_exit->bound,
                                    (float)mousePos.x, (float)mousePos.y)) {
        hover_btn = 2;
    }

    sfRectangleShape_setSize(btn_ready->box,
        (hover_btn == 1) ? HOVER_BUTTON : BUTTON);
    sfRectangleShape_setSize(btn_exit->box,
        (hover_btn == 2) ? HOVER_BUTTON : BUTTON);

    // Click
    if (event.type == sfEvtMouseButtonPressed &&
        event.mouseButton.button == sfMouseLeft) {

        mousePos = sfMouse_getPositionRenderWindow(window);

        if (sfFloatRect_contains(&btn_ready->bound,
                                 (float)mousePos.x, (float)mousePos.y)) {
            // 切換 ready 狀態
            is_ready = !is_ready;

            // 準備送給 server 的指令
            if (is_ready) {
                snprintf(sendline, sizeof(sendline), "room ready on\n");
            } else {
                snprintf(sendline, sizeof(sendline), "room ready off\n");
            }
            is_write = 1;

            // 本地 UI：更新自己那一格的狀態
            if (my_slot >= 0 && my_slot < 4 && players[my_slot]) {
                PlayerSlot_setStatus(players[my_slot],
                                     is_ready ? "Ready" : "Not Ready");
            }
        }
        else if (sfFloatRect_contains(&btn_exit->bound,
                                      (float)mousePos.x, (float)mousePos.y)) {
            snprintf(sendline, sizeof(sendline), "room leave\n");
            is_write = 1;
            // 真正切回 Lobby 建議等 server 回應後在 recv 裡面換 scene
        }
    }
}

void game_play_handle_event(sfRenderWindow* window, sfEvent event)
{
    // 關窗
    if (event.type == sfEvtClosed) {
        sfRenderWindow_close(window);
    }

    // Esc → 還是用 room leave 回到 lobby（server 還是把你當 GAME_ROOM）
    if (event.type == sfEvtKeyPressed &&
        event.key.code == sfKeyEscape) {
        snprintf(sendline, sizeof(sendline), "room leave\n");
        is_write = 1;
    }

    // ★ 空白鍵：如果我身上有炸彈，要求 server 幫我「嘗試丟出去」
    if (event.type == sfEvtKeyPressed &&
        event.key.code == sfKeySpace) {

        if (my_slot >= 0 && my_slot < 4 &&
            g_slot_active[my_slot] &&
            g_players[my_slot].alive &&
            g_players[my_slot].has_bomb) {

            snprintf(sendline, sizeof(sendline), "bomb throw\n");
            is_write = 1;
        }
    }
}

void game_play_update(float dt, unsigned int win_width, unsigned int win_height)
{
    if (my_slot < 0 || my_slot >= 4) return;
    if (!g_slot_active[my_slot])     return;

    float speed = 260.f;
    float vx = 0.f, vy = 0.f;

    if (sfKeyboard_isKeyPressed(sfKeyW)) vy -= 1.f;
    if (sfKeyboard_isKeyPressed(sfKeyS)) vy += 1.f;
    if (sfKeyboard_isKeyPressed(sfKeyA)) vx -= 1.f;
    if (sfKeyboard_isKeyPressed(sfKeyD)) vx += 1.f;

    int moving = 0;

    if (vx != 0.f || vy != 0.f) {
        moving = 1;
        float len = sqrtf(vx * vx + vy * vy);
        vx /= len;
        vy /= len;

        g_players[my_slot].x += vx * speed * dt;
        g_players[my_slot].y += vy * speed * dt;

        float margin = 50.f;
        if (g_players[my_slot].x < margin) g_players[my_slot].x = margin;
        if (g_players[my_slot].x > (float)win_width - margin)
            g_players[my_slot].x = (float)win_width - margin;
        if (g_players[my_slot].y < margin) g_players[my_slot].y = margin;
        if (g_players[my_slot].y > (float)win_height - margin)
            g_players[my_slot].y = (float)win_height - margin;
    }

	g_players[my_slot].target_x = g_players[my_slot].x;
    g_players[my_slot].target_y = g_players[my_slot].y;

	static float accum = 0.f;
	accum += dt;

	if (vx != 0.f || vy != 0.f) {
		// 每 0.1 秒送一次
		if (accum >= 0.1f) {
			accum = 0.f;
			snprintf(sendline, sizeof(sendline),
					"move %d %.2f %.2f\n",
					my_slot,
					g_players[my_slot].target_x,
					g_players[my_slot].target_y);
			is_write = 1;
		}
	}

    // ★ 對「不是自己」的玩家做平滑插值
    float lerp_speed = 10.0f;                 // 越大追得越快
    float alpha      = lerp_speed * dt;
    if (alpha > 1.f) alpha = 1.f;

    for (int i = 0; i < 4; ++i) {
        if (!g_slot_active[i])       continue;
        if (!g_players[i].alive)     continue;
        if (i == my_slot)            continue;  // 自己已經本地更新了

        g_players[i].x += (g_players[i].target_x - g_players[i].x) * alpha;
        g_players[i].y += (g_players[i].target_y - g_players[i].y) * alpha;
    }
}

int main(int argc, char **argv) {
	memset(all_rooms, 0, sizeof(all_rooms));
	rooms_offset = 0;
	rooms_max_index = -1;

	if(argc != 2) {
		printf("usage: client <IPaddress>\n");
	}

	int sockfd;
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(9877);
	inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

	connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

	//Create window with FPS
	sfRenderWindow* window = createwindow(60);
	sfVector2u size = sfRenderWindow_getSize(window);
	unsigned int width = size.x;
	unsigned int height = size.y;

	sfClock* clock = sfClock_create();

	//Load font
	sfFont* font = loadfont();


	// Score UI
	g_score_text = sfText_create();
	sfText_setFont(g_score_text, font);
	sfText_setCharacterSize(g_score_text, 24);
	sfText_setFillColor(g_score_text, sfWhite);
	// 右上角，預留一點 margin
	sfText_setPosition(g_score_text, (sfVector2f){ width - 220.f, 20.f });

	sfText* title = sfText_create();
	sfText_setFont(title, font);
	sfText_setString(title, "ULTIMATE POLYGON");
	sfText_setCharacterSize(title, 120);
	sfText_setFillColor(title, sfWhite);
	sfText_setPosition(title, (sfVector2f){width/5.f, height/7.f});
	username = TypeText_create(font, "username",
						(sfVector2f){width/2.f-200.f, 500.f});
	password = TypeText_create(font, "password",
						(sfVector2f){width/2.f-200.f, 570.f});

	reg = Button_create(font, "register",
						(sfVector2f){width/2.f-180.f, 660.f});
	login = Button_create(font, " log in ",
						(sfVector2f){width/2.f+40.f, 660.f});
	prev = Button_create(font, "   prev ",
						(sfVector2f){width/2.f+260.f, 300.f});
	next = Button_create(font, "   next ",
						(sfVector2f){width/2.f+260.f, 420.f});
	create_room = Button_create(font, "create_room",
						(sfVector2f){width/2.f+260.f, 660.f});
	room_name = TypeText_create(font, "roomname",
						(sfVector2f){width/2.f+180.f, 700.f});
	room_pass = TypeText_create(font, "password",
						(sfVector2f){width/2.f+180.f, 740.f});

	for (int i = 0; i < 4; ++i) {
		g_player_shapes[i] = sfCircleShape_create();
		sfCircleShape_setRadius(g_player_shapes[i], 20.f);
		sfCircleShape_setOrigin(g_player_shapes[i], (sfVector2f){20.f, 20.f});
		sfCircleShape_setFillColor(g_player_shapes[i], sfWhite);
	}

	// 建立 4 個房間內的玩家格 (位置你可以自己調整)
	for (int i = 0; i < 4; ++i) {
		sfVector2f pos = { width/2.f - 300.f, 200.f + i * 120.f };
		room_players[i] = PlayerSlot_create(font, "Empty", "", pos);
	}
	btn_ready = Button_create(font, " Ready ", (sfVector2f){ width/2.f - 200.f, 720.f });
	btn_exit  = Button_create(font, " Exit  ", (sfVector2f){ width/2.f + 40.f, 720.f });
    for (int i = 0; i < 5; ++i) {
        sfVector2f pos = { 80.f, 80.f + i * 150.f };
        char room_name[64];
        char host_name[64];

        snprintf(room_name, sizeof(room_name), "Room %d", i + 1);
        snprintf(host_name, sizeof(host_name), "Host %d", i + 1);

        lobby_rooms[i] = Room_create(font, room_name, host_name, pos);
    }


	sfEvent event;
	while(sfRenderWindow_isOpen(window)) {
		//Event process
		while(sfRenderWindow_pollEvent(window, &event)) {
			if(scene == MAIN_MENU) {
				TypeText* typetext[2];
				typetext[0] = username;
				typetext[1] = password;
				Button* button[2];
				button[0] = reg;
				button[1] = login;
				main_menu_handle_event(window, event, typetext, button);
			}

			if (scene == GAME_LOBBY) {
				TypeText* typetext[2] = {room_name, room_pass};
				Room* rooms[5] = {
					lobby_rooms[0], lobby_rooms[1], lobby_rooms[2],
					lobby_rooms[3], lobby_rooms[4]
				};
				Button* nav[3] = { prev, next, create_room };

				game_lobby_handle_event(window, event, typetext, rooms, nav);
			}

			if (scene == GAME_ROOM) {
				PlayerSlot* players[4] = {
					room_players[0], room_players[1],
					room_players[2], room_players[3]
				};
				game_room_handle_event(window, event, players, btn_ready, btn_exit);
			}

			if (scene == GAME_PLAYING) {
				game_play_handle_event(window, event);
			}

		}

		//Receive data
		n = recv(sockfd, recvline, MAXLINE - 1, MSG_DONTWAIT);
		if (n > 0) {
			recvline[n] = '\0';

			// 逐行處理
			char *line = strtok(recvline, "\n");
			while (line) {
				// 如果 server 用 \r\n，先把行尾的 \r 去掉
				size_t len = strlen(line);
				if (len > 0 && line[len-1] == '\r') {
					line[len-1] = '\0';
				}

				printf("line: %s\n", line);

				if (scene == MAIN_MENU) {
					if (strncmp("success", line, 7) == 0) {
						prev_scene = MAIN_MENU;
						scene      = GAME_LOBBY;
					}
				}
				else if (scene == GAME_LOBBY) {
					// 1) 自己創房成功 → 進入 GAME_ROOM
					if (strncmp(recvline, "room created", 12) == 0) {
						int room_id;
						char room_name_buf[64];

						if (sscanf(recvline, "room created %d %63s",
								&room_id, room_name_buf) == 2) {
							// 創房者就當作 slot 0
							my_slot = 0;
						}
						prev_scene = GAME_LOBBY;
						scene      = GAME_ROOM;
					}
					// 2) 更新房間資訊：room <idx> <room_name> <host_name>
					else if (strncmp(line, "room ", 5) == 0) {
						int idx;
						char room_name_buf[64];
						char host_name_buf[64];

						if (sscanf(line, "room %d %63s %63s",
								&idx, room_name_buf, host_name_buf) == 3) {

							if (idx >= 0 && idx < MAX_ROOM) {
								all_rooms[idx].in_use = 1;
								strncpy(all_rooms[idx].name, room_name_buf,
										sizeof(all_rooms[idx].name) - 1);
								all_rooms[idx].name[sizeof(all_rooms[idx].name)-1] = '\0';

								strncpy(all_rooms[idx].host, host_name_buf,
										sizeof(all_rooms[idx].host) - 1);
								all_rooms[idx].host[sizeof(all_rooms[idx].host)-1] = '\0';

								if (idx > rooms_max_index) rooms_max_index = idx;

								// 全部重畫一遍目前這一頁
								update_lobby_view_from_all();
							}
						}
					}
					// 3) 加入房間成功
					else if (strncmp(recvline, "join ok", 7) == 0) {
						int slot = 0;
						// server 回的是 "join ok %d\n"
						sscanf(recvline, "join ok %d", &slot);
						my_slot = slot;

						prev_scene = GAME_LOBBY;
						scene      = GAME_ROOM;
					}
					// 4) 加入房間失敗
					else if (strncmp(line, "join fail", 9) == 0) {
						printf("join failed: %s\n", line + 10);
					}
					// 5) 清空所有房間列表 (snapshot 開頭)
					else if (strncmp(line, "rooms clear", 11) == 0) {
						memset(all_rooms, 0, sizeof(all_rooms));
						rooms_offset    = 0;
						rooms_max_index = -1;
						update_lobby_view_from_all();
					}
				}
				else if (scene == GAME_ROOM) {
					// 1) 房間玩家列表清空
					if (strncmp(line, "room players clear", 18) == 0) {
						for (int i = 0; i < 4; ++i) {
							if (!room_players[i]) continue;
							PlayerSlot_setName(room_players[i], "Empty");
							PlayerSlot_setStatus(room_players[i], "");
							g_slot_active[i] = 0;
							g_players[i].alive = 0;
						}
					}
					// 2) 更新某個 slot 的玩家資訊
					else if (strncmp(line, "room player ", 12) == 0) {
						int  slot;
						char name[64];
						char role[16];
						char status[32];

						if (sscanf(line, "room player %d %63s %15s %31s",
								&slot, name, role, status) == 4) {

							if (slot >= 0 && slot < 4 && room_players[slot]) {
								if (strcmp(name, "empty") == 0) {
									PlayerSlot_setName(room_players[slot], "Empty");
									PlayerSlot_setStatus(room_players[slot], "");
									g_slot_active[slot] = 0;
									g_players[slot].alive = 0;
								} else {
									PlayerSlot_setName(room_players[slot], name);

									char status_text[64];
									if (strcmp(role, "host") == 0) {
										if (strcmp(status, "ready") == 0)
											snprintf(status_text, sizeof(status_text),
													"Host (Ready)");
										else
											snprintf(status_text, sizeof(status_text),
													"Host (Not Ready)");
									} else {
										if (strcmp(status, "ready") == 0)
											snprintf(status_text, sizeof(status_text),
													"Ready");
										else
											snprintf(status_text, sizeof(status_text),
													"Not Ready");
									}
									PlayerSlot_setStatus(room_players[slot], status_text);

									// 標記這個 slot 真的有一個玩家
									g_slot_active[slot] = 1;
									g_players[slot].alive = 1;
								}
							}
						}
					}
					// 收到 server 告知：你已成功離開房間
					else if (strncmp(line, "room leave ok", 13) == 0) {
						// 清自己的 ready 狀態
						is_ready = false;

						// 清畫面（可選，因為換 scene 就不會再畫）
						for (int i = 0; i < 4; ++i) {
							if (!room_players[i]) continue;
							PlayerSlot_setName(room_players[i], "Empty");
							PlayerSlot_setStatus(room_players[i], "");
						}

						// 切回 Lobby，讓 scene 切換那段去送 "rooms sync"
						prev_scene = GAME_ROOM;
						scene      = GAME_LOBBY;
					}
					// 3) 房間被關閉（例如房主離開）
					else if (strncmp(line, "room closed", 11) == 0) {
						// 清掉本地的 room UI
						for (int i = 0; i < 4; ++i) {
							if (!room_players[i]) continue;
							PlayerSlot_setName(room_players[i], "Empty");
							PlayerSlot_setStatus(room_players[i], "");
						}
						is_ready = false;      // 自己的 ready 狀態也順便重置（如果你有這個全域）

						// 切回 Lobby，讓主迴圈那邊的 scene!=prev_scene 邏輯去送 "rooms sync"
						prev_scene = GAME_ROOM;
						scene      = GAME_LOBBY;

						// 如果你沒有在 scene 切換時送 "rooms sync"，也可以這裡主動送：
						// snprintf(sendline, sizeof(sendline), "rooms sync");
						// is_write = 1;
					}
					// 4) game_start
					else if (strncmp(line, "game start", 10) == 0) {
						// 先初始化遊戲狀態
						for (int i = 0; i < 4; ++i) {
							g_slot_active[i]   = 0;
							g_players[i].alive = 0;
							g_players[i].has_bomb = 0;
						}
						for (int i = 0; i < 4; ++i) {
							if (!g_player_shapes[i]) continue;
							if (i == my_slot)
								sfCircleShape_setFillColor(g_player_shapes[i], sfBlue);
							else
								sfCircleShape_setFillColor(g_player_shapes[i], sfWhite);
						}

						// 再切 scene
						scene = GAME_PLAYING;
					}
				}
				else if (scene == GAME_PLAYING) {
					if (strncmp(line, "game player ", 12) == 0) {
						int   slot;
						float px, py;
						if (sscanf(line, "game player %d %f %f", &slot, &px, &py) == 3) {
							if (slot >= 0 && slot < 4) {
								g_slot_active[slot]        = 1;
								g_players[slot].alive      = 1;
								g_players[slot].x          = px;
								g_players[slot].y          = py;
								g_players[slot].target_x   = px;
								g_players[slot].target_y   = py;
							}
						}
					}
					else if (strncmp(line, "pos ", 4) == 0) {
						int   slot;
						float px, py;
						if (sscanf(line, "pos %d %f %f", &slot, &px, &py) == 3) {
							if (slot >= 0 && slot < 4) {
								g_players[slot].target_x = px;
								g_players[slot].target_y = py;

								if (slot == my_slot) {
									g_players[slot].x = px;
									g_players[slot].y = py;
								}
							}
						}
					}
					else if (strncmp(line, "bomb owner ", 11) == 0) {
						int slot = -1;
						if (sscanf(line, "bomb owner %d", &slot) == 1) {
							for (int i = 0; i < 4; ++i)
								g_players[i].has_bomb = 0;
							if (slot >= 0 && slot < 4)
								g_players[slot].has_bomb = 1;
						}
					}
					else if (strncmp(line, "bomb explode ", 13) == 0) {
						int slot = -1;
						if (sscanf(line, "bomb explode %d", &slot) == 1) {
							if (slot >= 0 && slot < 4) {
								g_players[slot].alive    = 0;
								g_players[slot].has_bomb = 0;
							}
						}
					}
					else if (strncmp(line, "score ", 6) == 0) {
						int slot, sc;
						if (sscanf(line, "score %d %d", &slot, &sc) == 2) {
							if (slot >= 0 && slot < 4) {
								g_scores[slot] = sc;
							}
						}
					}
					else if (strncmp(line, "game over ", 10) == 0) {
						int win_slot = -1;
						if (sscanf(line, "game over %d", &win_slot) == 1) {
							printf("winner slot = %d\n", win_slot);
							// 先切回 GAME_ROOM，等待下一局
							prev_scene = GAME_PLAYING;
							scene      = GAME_ROOM;
						}
					}
				}

				
				// 下一行
				line = strtok(NULL, "\n");
			}
		}
		else if (n == 0) {
			printf("server closed connection\n");
			close(sockfd);
	        sfRenderWindow_close(window);
		}
		else {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// no data
			}
			else if (errno == ENOTCONN) {
				printf("socket not connected anymore\n");
				close(sockfd);
        		sfRenderWindow_close(window);
			}
			else {
				perror("recv error");
				close(sockfd);
		        sfRenderWindow_close(window);
			}
		}


		//Send data//
		if(is_write) {
			n = write(sockfd, sendline, strlen(sendline));
			printf("send: %s\n", sendline);
			is_write = 0;
		}

		// need create/destroy object?
		if (scene != prev_scene) {
			if (scene == MAIN_MENU) {
				//to be implement
			}
			if (scene == GAME_LOBBY) {
				// 剛進入大廳：要求目前所有房間資訊
				// 協定舉例："rooms sync"
				sprintf(sendline, "rooms sync");
				is_write = 1;

				// TODO: 這裡之後也可以順便做 Lobby UI 初始化 / 清空欄位
				// rooms_offset = 0;
				// memset(all_rooms, 0, sizeof(all_rooms));
				// update_lobby_view_from_all();
			}
			else if (scene == GAME_ROOM) {
				// 剛進入房間：要求這個房間內 4 個玩家的資訊
				// 協定舉例："room info"
				sprintf(sendline, "room info");
				is_write = 1;

				// TODO: 也可以在這裡先把 room_players[0..3] 顯示成 "Loading..." 之類
			}
			else if (scene == GAME_PLAYING) {
				// 先把顏色設好：自己藍色，其它白色
				for (int i = 0; i < 4; ++i) {
					if (!g_player_shapes[i]) continue;
					if (i == my_slot) {
						sfCircleShape_setFillColor(g_player_shapes[i], sfBlue);
					} else {
						sfCircleShape_setFillColor(g_player_shapes[i], sfWhite);
					}
				}
			}

			// ★ 很重要：處理完場景切換後，要更新 prev_scene
			prev_scene = scene;
		}


		sfTime elapsed = sfClock_restart(clock);
		float dt = sfTime_asSeconds(elapsed);


		//Render
		if(scene == MAIN_MENU) {
			sfRenderWindow_clear(window, main_menu_bg);
			sfRenderWindow_drawButton(window, reg);
			sfRenderWindow_drawButton(window, login);
			sfRenderWindow_drawTypeText(window, username);
			sfRenderWindow_drawTypeText(window, password);
			sfRenderWindow_drawText(window, title, NULL);
		}
		if(scene == GAME_LOBBY) {
			sfRenderWindow_clear(window, main_menu_bg);
			sfRenderWindow_drawButton(window, prev);
			sfRenderWindow_drawButton(window, next);
			sfRenderWindow_drawButton(window, create_room);
			sfRenderWindow_drawTypeText(window, room_name);
			sfRenderWindow_drawTypeText(window, room_pass);
			for(int i = 0; i < 5; i++) {
				sfRenderWindow_drawRoom(window, lobby_rooms[i]);
			}
		}
		if (scene == GAME_ROOM) {
			sfRenderWindow_clear(window, main_menu_bg);

			// 畫 4 個玩家資訊格
			for (int i = 0; i < 4; ++i) {
				if (!room_players[i]) continue;
				sfRenderWindow_drawPlayerSlot(window, room_players[i]);
			}

			// 畫 Ready / Exit 按鈕
			sfRenderWindow_drawButton(window, btn_ready);
			sfRenderWindow_drawButton(window, btn_exit);
		}
		if (scene == GAME_PLAYING) {
    		game_play_update(dt, width, height);

			sfRenderWindow_clear(window, main_menu_bg);

			sfRectangleShape* arena = sfRectangleShape_create();
			sfRectangleShape_setPosition(arena, (sfVector2f){50.f, 50.f});
			sfRectangleShape_setSize(arena, (sfVector2f){1340.f, 800.f});
			sfRectangleShape_setOutlineColor(arena, sfWhite);
			sfRectangleShape_setOutlineThickness(arena, 3.f);
			sfRectangleShape_setFillColor(arena, sfTransparent);
			sfRenderWindow_drawRectangleShape(window, arena, NULL);
			sfRectangleShape_destroy(arena);

			// 畫 4 個玩家
			for (int i = 0; i < 4; ++i) {
				if (!g_player_shapes[i]) continue;
				if (!g_slot_active[i])   continue;
				if (!g_players[i].alive) continue;

				sfVector2f pos = { g_players[i].x, g_players[i].y };

				// 有炸彈的人：外框顏色變紅色、變粗
				if (g_players[i].has_bomb) {
					sfCircleShape_setOutlineThickness(g_player_shapes[i], 4.f);
					sfCircleShape_setOutlineColor(g_player_shapes[i], sfRed);
				} else {
					sfCircleShape_setOutlineThickness(g_player_shapes[i], 1.f);
					sfCircleShape_setOutlineColor(g_player_shapes[i], sfWhite);
				}

				sfCircleShape_setPosition(g_player_shapes[i], pos);
				sfRenderWindow_drawCircleShape(window, g_player_shapes[i], NULL);
			}

			if (g_score_text) {
				char buf[128];
				snprintf(buf, sizeof(buf),
						"Score\nP0: %d\nP1: %d\nP2: %d\nP3: %d",
						g_scores[0], g_scores[1], g_scores[2], g_scores[3]);
				sfText_setString(g_score_text, buf);
				sfRenderWindow_drawText(window, g_score_text, NULL);
			}

			// TODO：之後可以加一個炸彈圖示、計時文字等等
		}

		sfRenderWindow_display(window);
	}

	//Destroy the World!!!
	sfText_destroy(title);
	TypeText_destroy(username);
	TypeText_destroy(password);
	Button_destroy(reg);
	Button_destroy(login);
	Button_destroy(prev);
	Button_destroy(next);
	Button_destroy(create_room);
	sfText_destroy(g_score_text);

	for (int i = 0; i < 4; ++i) {
		PlayerSlot_destroy(room_players[i]);
	}
	Button_destroy(btn_ready);
	Button_destroy(btn_exit);

	for (int i = 0; i < 4; ++i) {
		if (g_player_shapes[i])
			sfCircleShape_destroy(g_player_shapes[i]);
	}

	sfClock_destroy(clock);
	sfFont_destroy(font);
    sfRenderWindow_destroy(window);

    return 0;
}
