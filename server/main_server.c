#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <termios.h>

#define MAX_USERS 100
#define MAX_NAME 32
#define MAX_PASS 32
#define MAX_ROOMS 32
#define MAX_ROOM_PLAYERS 4
#define MIN_PLAYERS_TO_START 2
#define MAXLINE 4096

#define	SA	struct sockaddr
#define	LISTENQ		1024
#define	max(a,b)	((a) > (b) ? (a) : (b))


typedef struct {
    float x, y;
    float vx, vy;   // 可以一開始先不用，之後想做慣性再加
    int   alive;    // 1=還在玩, 0=被炸死/淘汰
} BombPlayerState;

typedef struct {
    int   bomb_holder_slot;  // 哪一個 slot 拿著炸彈 (-1 表示暫時沒人？)
    float bomb_timer;        // 距離爆炸還有幾秒
    float map_width, map_height;

    int   last_thrower_slot; // ★ 最後一次丟炸彈的人，沒有就 -1

    BombPlayerState players[MAX_ROOM_PLAYERS];
} BombGameState;

typedef enum Scene {
    MAIN_MENU,
    GAME_LOBBY,
    GAME_ROOM,
    GAME_PLAYING,
    GAME_MODE_A,
    GAME_MODE_B
} Scene;

typedef struct {
    int  connfd;
    Scene scene;
    char username[MAX_NAME];
    char password[MAX_PASS];
    int  room_id;
    int  ready;
    int  slot_in_room;
    int  score;
} User;

typedef enum {
    ROOM_IDLE,      // 沒在用（in_use=0 時其實用不到，也可以留著）
    ROOM_LOBBY,     // 房間中，大家在準備
    ROOM_COUNTDOWN, // 全員 ready，進入倒數（例如 3 秒）
    ROOM_PLAYING,   // 正式遊戲中
    ROOM_RESULT     // 結算中
} RoomState;

typedef struct {
    int  in_use;
    int  id;
    int  host_idx;
    char name[MAX_NAME];
    char password[MAX_PASS];

    RoomState state;      // ROOM_LOBBY / ROOM_COUNTDOWN / ROOM_PLAYING / ROOM_RESULT
    int       game_mode;  // 例如 1: 模式 A, 2: 模式 B ...
    double    state_since;     // 進入目前 state 的時間（秒）
    double    game_start_time; // 正式遊戲開始的時間（秒）
    int       scores[MAX_ROOM_PLAYERS]; // 依照「房間內 slot 編號」存分數

	BombGameState bomb;
    int   slot_uid[MAX_ROOM_PLAYERS]; // slot 對應的 user index, -1 表示沒人
    float px[MAX_ROOM_PLAYERS];       // slot 的 x 座標
    float py[MAX_ROOM_PLAYERS];       // slot 的 y 座標
} Room;


User users[MAX_USERS];
Room rooms[MAX_ROOMS];
// 房間 ready 計時用
double room_all_ready_since[MAX_ROOMS];   // 0 表示目前不是「全部 ready」狀態
int    room_game_started[MAX_ROOMS];      // 0 尚未開始遊戲, 1 已經觸發 game start

char sendline[MAXLINE], recvline[MAXLINE];
int user_count = 0;
int n;


int find_empty_user(User users[MAX_USERS]) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].connfd == 0) {
            return i;
        }
    }
    return -1;
}

int find_max_fd(User users[MAX_USERS]) {
    int maxfd = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].connfd > maxfd) maxfd = users[i].connfd;
    }
    return maxfd;
}

int find_empty_room(Room rooms[MAX_ROOMS]) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!rooms[i].in_use) return i;
    }
    return -1;
}

void send_room_snapshot(int rid, int user_idx) {
    int fd = users[user_idx].connfd;
    if (fd <= 0) return;
    if (rid < 0 || rid >= MAX_ROOMS) return;
    if (!rooms[rid].in_use) return;

    int members[MAX_ROOM_PLAYERS];
    int mcount = 0;

    for (int j = 0; j < MAX_USERS && mcount < MAX_ROOM_PLAYERS; j++) {
        if (users[j].connfd <= 0) continue;
        if (users[j].room_id != rid) continue;
        members[mcount++] = j;
    }

    char buf[MAXLINE];
    ssize_t n;

    // 先清空 client 的房間 UI
    snprintf(buf, sizeof(buf), "room players clear\n");
    n = write(fd, buf, strlen(buf));
    (void)n;
    printf("send (idx %d): %s", user_idx, buf);

    // 依照 slot 0..3 送玩家資訊
    for (int slot = 0; slot < MAX_ROOM_PLAYERS; slot++) {
        if (slot < mcount) {
            int uj = members[slot];
            const char *name = users[uj].username[0] ? users[uj].username : "Unknown";
            const char *role = (uj == rooms[rid].host_idx) ? "host" : "member";
            const char *ready = users[uj].ready ? "ready" : "notready";

            snprintf(buf, sizeof(buf),
                     "room player %d %s %s %s\n",
                     slot, name, role, ready);
        } else {
            snprintf(buf, sizeof(buf),
                     "room player %d empty none none\n", slot);
        }

        n = write(fd, buf, strlen(buf));
        (void)n;
        printf("send (idx %d): %s", user_idx, buf);
    }
}

void broadcast_rooms_snapshot_to_lobby(void)
{
    char buf[MAXLINE];

    for (int u = 0; u < MAX_USERS; u++) {
        if (users[u].connfd <= 0) continue;
        if (users[u].scene   != GAME_LOBBY) continue;

        int fd = users[u].connfd;

        // 先清空畫面
        snprintf(buf, sizeof(buf), "rooms clear\n");
        n = write(fd, buf, strlen(buf));
        printf("send (idx %d): %s", u, buf);

        // 再送目前所有房間
        for (int r = 0; r < MAX_ROOMS; r++) {
            if (!rooms[r].in_use) continue;

            int host_idx = rooms[r].host_idx;
            const char *host_name = "-";
            if (host_idx >= 0 && host_idx < MAX_USERS &&
                users[host_idx].connfd > 0) {
                host_name = users[host_idx].username;
            }

            snprintf(buf, sizeof(buf),
                     "room %d %s %s\n", r, rooms[r].name, host_name);
            n = write(fd, buf, strlen(buf));
            printf("send (idx %d): %s", u, buf);
        }
    }
}

void close_room_and_notify(int rid, int host_idx)
{
    if (rid < 0 || rid >= MAX_ROOMS) return;
    if (!rooms[rid].in_use)        return;

    printf("room %d closed (host left)\n", rid);

    // 先找這個房間所有成員
    int members[MAX_ROOM_PLAYERS];
    int mcount = 0;

    for (int j = 0; j < MAX_USERS && mcount < MAX_ROOM_PLAYERS; j++) {
        if (users[j].connfd <= 0) continue;
		if (users[j].room_id != rid) continue;
		if (users[j].scene != GAME_ROOM && users[j].scene != GAME_PLAYING) continue;

        members[mcount++] = j;
    }

    // 把房間標記為未使用 & 清除 ready 計時
    rooms[rid].in_use = 0;
    room_all_ready_since[rid] = 0.0;
    room_game_started[rid]    = 0;

    // 可選：也把房間其它欄位清掉
    rooms[rid].host_idx = -1;
    rooms[rid].name[0]  = '\0';
    rooms[rid].password[0] = '\0';

    char buf[MAXLINE];

    // 通知房間內所有玩家，並把他們踢回 Lobby
    for (int k = 0; k < mcount; k++) {
        int uid = members[k];
        int cfd = users[uid].connfd;

        // 重置使用者狀態
        users[uid].room_id = -1;
        users[uid].ready   = 0;

        if (uid == host_idx) {
            // host 已經在斷線流程裡會被關掉 fd，不一定要再送訊息
            continue;
        }

        // 還在線上的人，丟回 Lobby
        if (cfd > 0) {
            users[uid].scene = GAME_LOBBY;

            // 告訴他：房間被關閉了
            snprintf(buf, sizeof(buf), "room closed\n");
            n = write(cfd, buf, strlen(buf));
            printf("send (idx %d): %s", uid, buf);
        }
    }

    // 最後：對所有在 Lobby 的人廣播新的房間列表 snapshot
    broadcast_rooms_snapshot_to_lobby();
}

double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}


void bombgame_init(Room *room);
// void bombgame_handle_input(Room *room, int slot, char move_key);
void bombgame_update(Room *room, double now);


void broadcast_bomb_owner(int rid)
{
    if (rid < 0 || rid >= MAX_ROOMS) return;
    if (!rooms[rid].in_use) return;

    int owner_slot = rooms[rid].bomb.bomb_holder_slot;

    char buf[MAXLINE];
    snprintf(buf, sizeof(buf), "bomb owner %d\n", owner_slot);

    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != rid) continue;
        if (users[u].scene   != GAME_PLAYING) continue;  // 只有在玩的人才需要

        write(users[u].connfd, buf, strlen(buf));
        printf("send (idx %d): %s", u, buf);
    }
}

void broadcast_score(int rid, int slot)
{
    if (rid < 0 || rid >= MAX_ROOMS) return;
    if (!rooms[rid].in_use) return;

    int score = rooms[rid].scores[slot];

    char buf[MAXLINE];
    snprintf(buf, sizeof(buf), "score %d %d\n", slot, score);

    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != rid) continue;
        if (users[u].scene   != GAME_PLAYING) continue;

        write(users[u].connfd, buf, strlen(buf));
    }
}

void bomb_round_restart(Room *room, double now)
{
    int rid = room->id;
    BombGameState *g = &room->bomb;

    // 把所有在這個房間、還在 GAME_PLAYING 的 user 按 slot 排好
    int slots[MAX_ROOM_PLAYERS];
    int slot_count = 0;

    for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
        if (room->slot_uid[s] < 0) continue;
        slots[slot_count++] = s;
    }

    // 沒人就直接回 Lobby 也可以，但正常來說不會出現
    if (slot_count == 0) return;

    // 重新設定位置 / alive
    for (int i = 0; i < slot_count; ++i) {
        int slot = slots[i];

        // 簡單排一排
        float px = 300.f + 300.f * i;
        float py = 450.f;

        room->px[slot] = px;
        room->py[slot] = py;

        g->players[slot].x     = px;
        g->players[slot].y     = py;
        g->players[slot].vx    = 0.f;
        g->players[slot].vy    = 0.f;
        g->players[slot].alive = 1;
    }

    // 其他沒用到的 slot 標記為死亡
    for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
        if (room->slot_uid[s] < 0) {
            g->players[s].alive = 0;
        }
    }

    // 隨機挑一個活著的人拿炸彈（這裡先拿 slots[0]）
    g->bomb_holder_slot = slots[0];
    g->bomb_timer       = 10.0f; // 一回合 10 秒（示意）

    // 廣播新的位置與炸彈持有者給所有玩家
    char buf[MAXLINE];

    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != rid) continue;
        if (users[u].scene   != GAME_PLAYING) continue;

        int cfd = users[u].connfd;

        // 可以送一個 round_start 讓 client 做提示（可選）
        snprintf(buf, sizeof(buf), "round start\n");
        write(cfd, buf, strlen(buf));

        // 傳每個玩家初始位置
        for (int i = 0; i < slot_count; ++i) {
            int slot = slots[i];
            snprintf(buf, sizeof(buf),
                     "game player %d %.1f %.1f\n",
                     slot, room->px[slot], room->py[slot]);
            write(cfd, buf, strlen(buf));
        }

        // 告訴現在炸彈在誰身上
        snprintf(buf, sizeof(buf), "bomb owner %d\n", g->bomb_holder_slot);
        write(cfd, buf, strlen(buf));
    }

    room->state       = ROOM_PLAYING;
    room->state_since = now;
}

void bomb_handle_explode(Room *room, int dead_slot, double now)
{
    int rid = room->id;
    BombGameState *g = &room->bomb;

    if (dead_slot < 0 || dead_slot >= MAX_ROOM_PLAYERS)
        return;
    if (room->slot_uid[dead_slot] < 0)
        return;                     // 這個 slot 根本沒玩家
    if (!g->players[dead_slot].alive)
        return;                     // 已經死掉就不要重複爆

    int killer = g->last_thrower_slot;

    // 1) 標記死亡
    g->players[dead_slot].alive = 0;

    // 2) 廣播爆炸給所有 client
    char buf[MAXLINE];
    snprintf(buf, sizeof(buf), "bomb explode %d\n", dead_slot);

    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != rid) continue;
        if (users[u].scene   != GAME_PLAYING) continue;
        write(users[u].connfd, buf, strlen(buf));
    }

    // 3) 計分：最後丟炸彈的人 +1，被炸的人 -1（但不低於 0）

    // 3-1) killer +1 分（只要有最後丟就加，不管是不是 2 人或多於 2 人）
    if (killer >= 0 && killer < MAX_ROOM_PLAYERS &&
        room->slot_uid[killer] >= 0) {

        room->scores[killer] += 1;
        if (room->scores[killer] < 0)
            room->scores[killer] = 0;

        broadcast_score(rid, killer);
    }

    // 3-2) 被炸的人 -1 分，但不低於 0
    if (room->scores[dead_slot] > 0) {
        room->scores[dead_slot] -= 1;
        if (room->scores[dead_slot] < 0)
            room->scores[dead_slot] = 0;
        broadcast_score(rid, dead_slot);
    }

    // 4) 計算目前活著的人數
    int alive_count = 0;
    int last_alive  = -1;

    for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
        if (!g->players[s].alive) continue;
        if (room->slot_uid[s] < 0) continue;
        alive_count++;
        last_alive = s;
    }

    // 5-1) 沒活人 → 直接重開一局，分數保留
    if (alive_count == 0) {
        bomb_round_restart(room, now);
        return;
    }

    // 5-2) 只剩 1 個活著 → 存活者再 +1 分（這樣 2 人局就是 killer 1 分 + 存活 1 分，共 2 分）
    if (alive_count == 1 && last_alive >= 0 && room->slot_uid[last_alive] >= 0) {

        room->scores[last_alive] += 1;         // 存活加分
        if (room->scores[last_alive] < 0)
            room->scores[last_alive] = 0;
        broadcast_score(rid, last_alive);

        // 存活加分完再檢查有沒有人 ≥ 5 分
        int winner_slot = -1;
        for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
            if (room->slot_uid[s] < 0) continue;
            if (room->scores[s] >= 5) {
                winner_slot = s;
                break;
            }
        }

        if (winner_slot != -1) {
            // 廣播 game over
            snprintf(buf, sizeof(buf), "game over %d\n", winner_slot);

            for (int u = 0; u < MAX_USERS; ++u) {
                if (users[u].connfd <= 0) continue;
                if (users[u].room_id != rid) continue;
                if (users[u].scene   != GAME_PLAYING) continue;

                write(users[u].connfd, buf, strlen(buf));
                users[u].scene = GAME_ROOM;
                users[u].ready = 0;
            }

            room->state       = ROOM_RESULT;
            room->state_since = now;
            return;
        }

        // 還沒到 5 分 → 重新開一局，分數保留
        bomb_round_restart(room, now);
        return;
    }

    // 5-3) 還有兩個以上活人 → 遊戲繼續
    //     這時候也要檢查一下，有沒有因為殺人加分直接 ≥ 5 分
    {
        int winner_slot = -1;
        for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
            if (room->slot_uid[s] < 0) continue;
            if (room->scores[s] >= 5) {
                winner_slot = s;
                break;
            }
        }

        if (winner_slot != -1) {
            snprintf(buf, sizeof(buf), "game over %d\n", winner_slot);

            for (int u = 0; u < MAX_USERS; ++u) {
                if (users[u].connfd <= 0) continue;
                if (users[u].room_id != rid) continue;
                if (users[u].scene   != GAME_PLAYING) continue;

                write(users[u].connfd, buf, strlen(buf));
                users[u].scene = GAME_ROOM;
                users[u].ready = 0;
            }

            room->state       = ROOM_RESULT;
            room->state_since = now;
            return;
        }
    }

    // 還在遊戲中 → 隨機把炸彈給一個活著的人，重設倒數
    int alive_slots[MAX_ROOM_PLAYERS];
    int ac = 0;
    for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
        if (!g->players[s].alive) continue;
        if (room->slot_uid[s] < 0) continue;
        alive_slots[ac++] = s;
    }

    int new_owner = alive_slots[rand() % ac];
    g->bomb_holder_slot  = new_owner;
    g->bomb_timer        = 10.0f;
    g->last_thrower_slot = -1;   // 下一顆炸彈重新記錄最後丟的人

    snprintf(buf, sizeof(buf), "bomb owner %d\n", new_owner);

    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != rid) continue;
        if (users[u].scene   != GAME_PLAYING) continue;
        write(users[u].connfd, buf, strlen(buf));
    }
}

int main() {
    int listenfd, connfd;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;
    clilen = sizeof(cliaddr);

    memset(users, 0, sizeof(users));
    memset(rooms, 0, sizeof(rooms));

	memset(room_all_ready_since, 0, sizeof(room_all_ready_since));
	memset(room_game_started, 0, sizeof(room_game_started));

	srand(time(NULL));

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(9877);

    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(listenfd);
        exit(1);
    }

    bind(listenfd, (SA *)&servaddr, sizeof(servaddr));
    listen(listenfd, LISTENQ);

    printf("Server running on port 9877...\n");

    fd_set rset;
    int maxfdp1;

    for (;;) {
        FD_ZERO(&rset);
        FD_SET(listenfd, &rset);

        int maxfd = listenfd;

        for (int i = 0; i < MAX_USERS; i++) {
            if (users[i].connfd > 0) {
                FD_SET(users[i].connfd, &rset);
                if (users[i].connfd > maxfd)
                    maxfd = users[i].connfd;
            }
        }

        maxfdp1 = maxfd + 1;

		struct timeval tv;
		tv.tv_sec  = 0;
		tv.tv_usec = 500000;

		int nready = select(maxfdp1, &rset, NULL, NULL, &tv);
        if (nready < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)) {
            connfd = accept(listenfd, (SA *)&cliaddr, &clilen);
            if (connfd < 0) {
                perror("accept");
            } else {
                int e = find_empty_user(users);
                if (e == -1) {
                    printf("too many users, reject\n");
                    close(connfd);
                } else {
					users[e].connfd = connfd;
					users[e].scene  = MAIN_MENU;
					users[e].room_id = -1;
					users[e].ready   = 0;
                    users[e].username[0] = '\0';
                    users[e].password[0] = '\0';
                    user_count++;
                    printf("new client at idx %d, fd=%d\n", e, connfd);
                }
            }
            if (--nready <= 0) continue;
        }

        // 處理各個 client 的訊息
        for (int i = 0; i < MAX_USERS; i++) {
            int fd = users[i].connfd;
            if (fd <= 0) continue;

            if (FD_ISSET(fd, &rset)) {
                int n = read(fd, recvline, MAXLINE - 1);
				if (n <= 0) {
					printf("client idx %d disconnected\n", i);

					// 如果他是任何房間的 host，就把房間關掉並通知成員
					for (int r = 0; r < MAX_ROOMS; r++) {
						if (rooms[r].in_use && rooms[r].host_idx == i) {
							close_room_and_notify(r, i);
						}
					}

					close(fd);
					users[i].connfd = 0;
					users[i].scene  = MAIN_MENU;
					users[i].room_id = -1;
					users[i].ready   = 0;
					user_count--;
				}
				else {
					recvline[n] = '\0';
					// printf("recv (idx %d): %s\n", i, recvline);

					// ==========
					// 解析指令
					// ==========

					// 1) 註冊 / 登入
					if (strncmp(recvline, "reg ", 4) == 0 ||
						strncmp(recvline, "log ", 4) == 0) {

						char user[MAX_NAME], pass[MAX_PASS];
						if (sscanf(recvline, "%*s user:%31s pass:%31s", user, pass) == 2) {
							snprintf(users[i].username, sizeof(users[i].username), "%s", user);
							users[i].username[MAX_NAME - 1] = '\0';
							snprintf(users[i].password, sizeof(users[i].password), "%s", pass);
							users[i].password[MAX_PASS - 1] = '\0';
							users[i].scene   = GAME_LOBBY;
							users[i].room_id = -1;
							users[i].ready   = 0;

							// reg/log 成功
							snprintf(sendline, sizeof(sendline), "success\n");
						} else {
							snprintf(sendline, sizeof(sendline), "error invalid_format\n");
						}

						n = write(fd, sendline, strlen(sendline));
						printf("send (idx %d): %s", i, sendline);
					}

					// 2) 創建房間 (在 GAME_LOBBY)
					else if (strncmp(recvline, "room create", 11) == 0) {
						if (users[i].scene != GAME_LOBBY) {
							snprintf(sendline, sizeof(sendline), "error not_in_lobby\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
						} else {
							char name[MAX_NAME], pass[MAX_PASS];
							if (sscanf(recvline, "room create name:%31s pass:%31s",
									name, pass) == 2) {

								int r = find_empty_room(rooms);
								if (r == -1) {
									snprintf(sendline, sizeof(sendline), "error room_full\n");
									n = write(fd, sendline, strlen(sendline));
									printf("send (idx %d): %s", i, sendline);
								} else {
									rooms[r].in_use   = 1;
									rooms[r].id       = r;
									rooms[r].host_idx = i;
									snprintf(rooms[r].name, sizeof(rooms[r].name), "%s", name);
									rooms[r].name[MAX_NAME - 1] = '\0';
									snprintf(rooms[r].password, sizeof(rooms[r].password), "%s", pass);
									rooms[r].password[MAX_PASS - 1] = '\0';

									// 新增：遊戲狀態初始化
									rooms[r].state           = ROOM_LOBBY;  // 剛創好房間在 Lobby 狀態
									rooms[r].game_mode       = 1;           // 先固定一種模式，之後可以改成玩家選
									rooms[r].state_since     = 0.0;         // 你可以改成 current_time_seconds()
									rooms[r].game_start_time = 0.0;
									for (int s = 0; s < MAX_ROOM_PLAYERS; s++) {
										rooms[r].scores[s] = 0;
									}
								
									users[i].room_id = r;
									users[i].scene   = GAME_ROOM;   // 創房者進入 GAME_ROOM

									// 回給創房者：room created <id> <name>
									snprintf(sendline, sizeof(sendline),
											"room created %d %s\n", r, rooms[r].name);
									n = write(fd, sendline, strlen(sendline));
									printf("send (idx %d): %s", i, sendline);

									// 廣播給其他在 Lobby 的玩家：有新房間
									char buf[MAXLINE];
									snprintf(buf, sizeof(buf),
											"room %d %s %s\n", r, rooms[r].name, users[i].username);
									for (int j = 0; j < MAX_USERS; j++) {
										if (j == i) continue;
										if (users[j].connfd <= 0) continue;
										if (users[j].scene != GAME_LOBBY) continue; // 只通知在 Lobby 的人

										n = write(users[j].connfd, buf, strlen(buf));
										printf("broadcast to idx %d: %s", j, buf);
									}
								}
							}
							else {
								snprintf(sendline, sizeof(sendline), "error invalid_format\n");
								n = write(fd, sendline, strlen(sendline));
								printf("send (idx %d): %s", i, sendline);
							}
						}
					}

					// 3) 查詢所有房間列表 (在 GAME_LOBBY)
					else if (strncmp(recvline, "rooms sync", 10) == 0) {
						if (users[i].scene != GAME_LOBBY) {
							snprintf(sendline, sizeof(sendline), "error not_in_lobby\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
						} else {
							// 先清空 client 畫面
							snprintf(sendline, sizeof(sendline), "rooms clear\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);

							// 再把目前所有房間丟過去
							char buf[MAXLINE];
							for (int r = 0; r < MAX_ROOMS; r++) {
								if (!rooms[r].in_use) continue;

								int host_idx = rooms[r].host_idx;
								const char* host_name = "-";
								if (host_idx >= 0 && host_idx < MAX_USERS &&
									users[host_idx].connfd > 0) {
									host_name = users[host_idx].username;
								}

								snprintf(buf, sizeof(buf),
										"room %d %s %s\n", r, rooms[r].name, host_name);
								n = write(fd, buf, strlen(buf));
								printf("send (idx %d): %s", i, buf);
							}
						}
					}

					// 4) 查詢目前房間內資訊 (在 GAME_ROOM)
					else if (strncmp(recvline, "room info", 9) == 0) {
						int rid = users[i].room_id;

						if (users[i].scene != GAME_ROOM ||
							rid < 0 || rid >= MAX_ROOMS || !rooms[rid].in_use) {

							snprintf(sendline, sizeof(sendline), "error not_in_room\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
						} else {
							// 共用函式：只對這個 client(i) 丟snapshot
							send_room_snapshot(rid, i);
						}
					}

					// 5) 玩家要求加入房間: "join room:<id> pass:<password>"
					else if (strncmp(recvline, "join ", 5) == 0) {
						int rid;
						char pass[MAX_PASS];

						// 解析指令格式
						if (sscanf(recvline, "join room:%d pass:%31s", &rid, pass) != 2) {
							snprintf(sendline, sizeof(sendline),
									"join fail invalid_format\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
							continue;
						}

						// 必須在 Lobby 才能加入房間
						if (users[i].scene != GAME_LOBBY) {
							snprintf(sendline, sizeof(sendline),
									"join fail not_in_lobby\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
							continue;
						}

						// 檢查房間 id 合法 & 是否存在
						if (rid < 0 || rid >= MAX_ROOMS || !rooms[rid].in_use) {
							snprintf(sendline, sizeof(sendline),
									"join fail no_such_room\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
							continue;
						}

						// 檢查密碼
						if (strcmp(rooms[rid].password, pass) != 0) {
							snprintf(sendline, sizeof(sendline),
									"join fail wrong_password\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
							continue;
						}

						// 檢查目前房間人數，避免超過 MAX_ROOM_PLAYERS
						int members[MAX_ROOM_PLAYERS];
						int mcount = 0;

						for (int j = 0; j < MAX_USERS && mcount < MAX_ROOM_PLAYERS; j++) {
							if (users[j].connfd <= 0) continue;
							if (users[j].room_id != rid) continue;
							members[mcount++] = j;
						}

						if (mcount >= MAX_ROOM_PLAYERS) {
							// 房間已滿
							snprintf(sendline, sizeof(sendline),
									"join fail room_full\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
							continue;
						}

						// 都通過 → 讓使用者加入房間
						users[i].room_id = rid;
						users[i].scene   = GAME_ROOM;

						// 重新計算一次 members，包含自己，並找到自己的 slot
						mcount = 0;
						int my_slot = -1;
						for (int j = 0; j < MAX_USERS && mcount < MAX_ROOM_PLAYERS; j++) {
							if (users[j].connfd <= 0) continue;
							if (users[j].room_id != rid) continue;

							if (j == i) {
								my_slot = mcount;
							}
							members[mcount++] = j;
						}
						// 理論上 my_slot 一定 >= 0，如果意外沒有，就設個預設值
						if (my_slot < 0) my_slot = 0;

						// 先回給自己 join ok <slot>
						users[i].slot_in_room = my_slot;
						users[i].score        = 0;

						snprintf(sendline, sizeof(sendline), "join ok %d\n", my_slot);
						n = write(fd, sendline, strlen(sendline));
						printf("send (idx %d): %s", i, sendline);

						// ★ 廣播：對這個房間裡所有玩家送一次最新 snapshot
						for (int j = 0; j < MAX_USERS; j++) {
							if (users[j].connfd <= 0) continue;
							if (users[j].room_id != rid) continue;
							if (users[j].scene != GAME_ROOM) continue;  // 保險一點

							send_room_snapshot(rid, j);
						}
					}
					
					// 5) 玩家在房間內切換 ready 狀態: "room ready on" / "room ready off"
					else if (strncmp(recvline, "room ready", 10) == 0) {
						int rid = users[i].room_id;

						// 只能在房間內才允許 ready
						if (users[i].scene != GAME_ROOM ||
							rid < 0 || rid >= MAX_ROOMS || !rooms[rid].in_use) {

							snprintf(sendline, sizeof(sendline), "error not_in_room\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
						} else {
							char state[8] = {0};

							// 解析 "room ready on/off"
							if (sscanf(recvline, "room ready %7s", state) != 1) {
								snprintf(sendline, sizeof(sendline),
										"error invalid_ready_cmd\n");
								n = write(fd, sendline, strlen(sendline));
								printf("send (idx %d): %s", i, sendline);
							} else {
								if (strcmp(state, "on") == 0) {
									users[i].ready = 1;
								} else if (strcmp(state, "off") == 0) {
									users[i].ready = 0;
								} else {
									snprintf(sendline, sizeof(sendline),
											"error invalid_ready_state\n");
									n = write(fd, sendline, strlen(sendline));
									printf("send (idx %d): %s", i, sendline);
									// 不廣播
									continue;
								}

								// （可選）回自己一個簡單 ack
								snprintf(sendline, sizeof(sendline),
										"ready ok %s\n", state);
								n = write(fd, sendline, strlen(sendline));
								printf("send (idx %d): %s", i, sendline);

								// ★ 廣播最新 snapshot 給房間裡所有玩家
								for (int j = 0; j < MAX_USERS; j++) {
									if (users[j].connfd <= 0) continue;
									if (users[j].room_id != rid) continue;
									if (users[j].scene != GAME_ROOM) continue;

									send_room_snapshot(rid, j);
								}
							}
						}
					}

					// 6) room leave
					else if (strncmp(recvline, "room leave", 10) == 0) {
						int rid = users[i].room_id;

						if (rid < 0 || rid >= MAX_ROOMS || !rooms[rid].in_use) {
							snprintf(sendline, sizeof(sendline), "error not_in_room\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
						} else {
							if (rooms[rid].host_idx == i) {
								// 先回 host 一個成功離開的訊息
								snprintf(sendline, sizeof(sendline), "room leave ok\n");
								n = write(fd, sendline, strlen(sendline));
								printf("send (idx %d): %s", i, sendline);

								// 再關掉房間、通知其他人
								close_room_and_notify(rid, i);

								// host 自己也要回到 Lobby 狀態
								users[i].room_id = -1;
								users[i].ready   = 0;
								users[i].scene   = GAME_LOBBY;
							} else {
								// 一般成員離開：只把自己踢出去，然後對剩下的人送 snapshot
								users[i].room_id = -1;
								users[i].ready   = 0;
								users[i].scene   = GAME_LOBBY;

								snprintf(sendline, sizeof(sendline), "room leave ok\n");
								n = write(fd, sendline, strlen(sendline));
								printf("send (idx %d): %s", i, sendline);

								// 對房間剩下的人更新 snapshot
								for (int j = 0; j < MAX_USERS; j++) {
									if (users[j].connfd <= 0) continue;
									if (users[j].room_id != rid) continue;
									if (users[j].scene   != GAME_ROOM) continue;
									send_room_snapshot(rid, j);
								}

								// 大廳也可以順便更新一次房間列表（有人離開可能讓房間「非滿」）
								broadcast_rooms_snapshot_to_lobby();
							}
						}
					}

					// 7) bomb throw：玩家嘗試把身上的炸彈丟出去
					else if (strncmp(recvline, "bomb throw", 10) == 0) {
						int rid = users[i].room_id;

						// 必須在有效房間 & 遊戲進行中才處理
						if (rid < 0 || rid >= MAX_ROOMS) {
							// ignore or回錯誤都可以，這裡直接忽略
						} else if (!rooms[rid].in_use || rooms[rid].state != ROOM_PLAYING) {
							// ignore
						} else {
							// 找出這個 user 在房間中的 slot
							int sender_slot = -1;
							for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
								if (rooms[rid].slot_uid[s] == i) {
									sender_slot = s;
									break;
								}
							}

							if (sender_slot < 0) {
								// 找不到 slot，直接忽略
							} else {
								// 確認他現在是不是炸彈持有者
								if (rooms[rid].bomb.bomb_holder_slot != sender_slot) {
									// 他沒拿炸彈，亂丟 → 忽略
								} else {
									// 正式判定有沒有丟到別人
									float throw_range = 100.0f;        // 丟炸彈最大距離（你可以改）
									float max_d2      = throw_range * throw_range;

									int   target_slot = -1;
									float best_d2     = max_d2 + 1.f;

									float sx = rooms[rid].px[sender_slot];
									float sy = rooms[rid].py[sender_slot];

									for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
										if (s == sender_slot) continue;
										if (rooms[rid].slot_uid[s] < 0) continue;  // 這個 slot 沒人

										float tx = rooms[rid].px[s];
										float ty = rooms[rid].py[s];

										float dx = tx - sx;
										float dy = ty - sy;
										float d2 = dx*dx + dy*dy;

										if (d2 <= max_d2 && d2 < best_d2) {
											best_d2     = d2;
											target_slot = s;
										}
									}

									int new_owner;
									if (target_slot >= 0) {
										// 有命中某個玩家
										new_owner = target_slot;
										printf("room %d: bomb thrown from slot %d → hit slot %d\n",
											rid, sender_slot, target_slot);
									} else {
										// 沒命中 → 回到原本玩家
										new_owner = sender_slot;
										printf("room %d: bomb thrown from slot %d → miss, back to self\n",
											rid, sender_slot);
									}

									rooms[rid].bomb.bomb_holder_slot = new_owner;
									rooms[rid].bomb.last_thrower_slot = sender_slot;
									rooms[rid].bomb.bomb_timer        = 10.0f;

									// 廣播新的炸彈持有者
									broadcast_bomb_owner(rid);
								}
							}
						}
					}

					// 8) move
					else if (strncmp(recvline, "move ", 5) == 0) {
						int rid = users[i].room_id;
						if (rid < 0 || rid >= MAX_ROOMS) goto unknown_cmd;
						if (!rooms[rid].in_use)           goto unknown_cmd;
						if (rooms[rid].state != ROOM_PLAYING) goto unknown_cmd;

						int   slot;
						float px, py;

						if (sscanf(recvline, "move %d %f %f", &slot, &px, &py) != 3) {
							// 格式錯誤直接忽略即可
						} else {
							// 確認這個 user 確實是這個 slot
							if (slot < 0 || slot >= MAX_ROOM_PLAYERS) {
								// ignore
							} else if (rooms[rid].slot_uid[slot] != i) {
								// 有人亂報 slot，就不理
							} else {
								// 更新房間內權威座標
								rooms[rid].px[slot] = px;
								rooms[rid].py[slot] = py;

								// 廣播給該房間所有人
								char buf[MAXLINE];
								snprintf(buf, sizeof(buf),
										"pos %d %.1f %.1f\n",
										slot, px, py);

								for (int j = 0; j < MAX_USERS; ++j) {
									if (users[j].connfd <= 0) continue;
									if (users[j].room_id != rid) continue;
									if (users[j].scene   != GAME_PLAYING) continue;

									write(users[j].connfd, buf, strlen(buf));
								}
							}
						}
					}

					// 其他未知指令
					else {
						unknown_cmd:
							snprintf(sendline, sizeof(sendline), "error unknown_command\n");
							n = write(fd, sendline, strlen(sendline));
							printf("send (idx %d): %s", i, sendline);
					}
				}
            }
        }
    
		// 放在主回圈的最後、處理完所有 recv 之後
		double now = now_sec();

		for (int r = 0; r < MAX_ROOMS; r++) {

			// 房間沒在用，順便把計時清掉
			if (!rooms[r].in_use) {
				room_all_ready_since[r] = 0.0;
				room_game_started[r]    = 0;
				continue;
			}

			// 如果這個房間已經在正式遊戲中了，就直接走「遊戲更新」邏輯
			if (rooms[r].state == ROOM_PLAYING) {
				bombgame_update(&rooms[r], now);
				continue;
			}

			// 走到這裡代表：房間存在，但還沒進入遊戲（例如 ROOM_LOBBY / ROOM_COUNTDOWN）
			// 收集這個房間的成員
			int  members[MAX_ROOM_PLAYERS];
			int  mcount    = 0;
			int  all_ready = 1;   // 假設全部 ready，等一下用 AND 驗證

			for (int j = 0; j < MAX_USERS && mcount < MAX_ROOM_PLAYERS; j++) {
				if (users[j].connfd <= 0) continue;
				if (users[j].room_id != r) continue;
				if (users[j].scene   != GAME_ROOM) continue;

				members[mcount++] = j;
				if (users[j].ready == 0) {
					all_ready = 0;
				}
			}

			// 沒有人，就不用判斷 ready
			if (mcount == 0) {
				room_all_ready_since[r] = 0.0;
				room_game_started[r]    = 0;
				continue;
			}

			// 如果你想限制「至少要 MIN_PLAYERS_TO_START 人」才可以開始，在這邊加：
			if (mcount < MIN_PLAYERS_TO_START) {
				room_all_ready_since[r] = 0.0;
				room_game_started[r]    = 0;
				continue;
			}

			// 有人沒 ready → 直接清計時
			if (!all_ready) {
				room_all_ready_since[r] = 0.0;
				continue;
			}

			// 走到這裡表示：
			// 1) 房間有至少 MIN_PLAYERS_TO_START 個玩家
			// 2) 所有人都 ready
			if (room_all_ready_since[r] == 0.0) {
				// 剛變成「全部 ready」→ 開始計時
				room_all_ready_since[r] = now;
			} else {

				double elapsed = now - room_all_ready_since[r];
				if (elapsed < 3.0) {
					continue;  // 還在倒數，不開局
				}
				printf("room %d game start!\n", r);
				rooms[r].state           = ROOM_PLAYING;
				rooms[r].game_start_time = now;
				rooms[r].state_since     = now;
				room_game_started[r]     = 1;

				// 先把 slot_uid 填好，並排出初始位置
				for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
					rooms[r].slot_uid[s] = -1;
					rooms[r].px[s] = rooms[r].py[s] = 0.f;
				}

				for (int slot = 0; slot < mcount; ++slot) {
					int uid = members[slot];
					rooms[r].slot_uid[slot] = uid;

					// 簡單排成一排
					rooms[r].px[slot] = 270.f + 300.f * slot;
					rooms[r].py[slot] = 450.f;
				}

				// ★ 初始化分數
				for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
					rooms[r].scores[s] = 0;
				}

				// ★ 初始化炸彈狀態
				rooms[r].bomb.map_width  = 1200.0f;
				rooms[r].bomb.map_height = 750.0f;

				if (mcount > 0) {
					rooms[r].bomb.bomb_holder_slot  = rand() % mcount; // 隨機一個 slot
					rooms[r].bomb.bomb_timer        = 10.0f;           // 10 秒倒數
					rooms[r].bomb.last_thrower_slot = -1;
				} else {
					rooms[r].bomb.bomb_holder_slot  = -1;
					rooms[r].bomb.bomb_timer        = 0.0f;
					rooms[r].bomb.last_thrower_slot = -1;
				}

				// 先清一下 bomb.players
				for (int s = 0; s < MAX_ROOM_PLAYERS; ++s) {
					rooms[r].bomb.players[s].alive = 0;
					rooms[r].bomb.players[s].vx    = 0.f;
					rooms[r].bomb.players[s].vy    = 0.f;
				}

				// 把這一局要玩的 mcount 個 slot 填進 bomb.players
				for (int slot = 0; slot < mcount; ++slot) {
					float px = rooms[r].px[slot];
					float py = rooms[r].py[slot];

					rooms[r].bomb.players[slot].x     = px;
					rooms[r].bomb.players[slot].y     = py;
					rooms[r].bomb.players[slot].vx    = 0.f;
					rooms[r].bomb.players[slot].vy    = 0.f;
					rooms[r].bomb.players[slot].alive = 1;
				}

				// 廣播：game start + game player
				char buf[MAXLINE];

				for (int k = 0; k < mcount; k++) {
					int uid = members[k];
					users[uid].scene = GAME_PLAYING;
					int cfd = users[uid].connfd;
					if (cfd <= 0) continue;

					// 1) 先告知開始
					snprintf(buf, sizeof(buf), "game start\n");
					write(cfd, buf, strlen(buf));

					// 2) 再把所有 slot 的初始位置都丟給他
					for (int slot = 0; slot < mcount; ++slot) {
						snprintf(buf, sizeof(buf),
								"game player %d %.1f %.1f\n",
								slot,
								rooms[r].px[slot],
								rooms[r].py[slot]);
						write(cfd, buf, strlen(buf));
					}
				}				
				broadcast_bomb_owner(r);
			}
		}
	}
}


void bombgame_init(Room *room) {
    BombGameState *g = &room->bomb;

    g->map_width  = 1200.0f;
    g->map_height = 750.0f;

    int n = 0;
    int members[MAX_ROOM_PLAYERS];

    // 先找出這個房間目前有哪些 slot
    for (int slot = 0; slot < MAX_ROOM_PLAYERS; ++slot) {
        g->players[slot].alive = 0;
    }

    // 掃 users 把 room_id == room->id 的人塞進 slot（你也可以沿用之前的 members 陣列）
    for (int u = 0; u < MAX_USERS; ++u) {
        if (users[u].connfd <= 0) continue;
        if (users[u].room_id != room->id) continue;
        if (users[u].scene   != GAME_ROOM) continue;

        if (n < MAX_ROOM_PLAYERS) {
            int slot = n;
            g->players[slot].x = 100.0f + 150.0f * slot;
            g->players[slot].y = 100.0f;
            g->players[slot].vx = 0;
            g->players[slot].vy = 0;
            g->players[slot].alive = 1;
            n++;
        }
    }

    // 隨機選一個存活玩家拿炸彈
    if (n > 0) {
        g->bomb_holder_slot = 0;      // 先固定 0，之後再改成 rand() % n
        g->bomb_timer = 10.0f;        // 例如 10 秒爆炸
    } else {
        g->bomb_holder_slot = -1;
        g->bomb_timer = 0.0f;
    }
}

void bombgame_update(Room *room, double now)
{
    BombGameState *g = &room->bomb;

    static double last_update = 0.0;
    double dt = 0.0;

    if (last_update == 0.0) {
        last_update = now;
        return;
    } else {
        dt = now - last_update;
        last_update = now;
    }

    if (g->bomb_holder_slot >= 0) {
        g->bomb_timer -= dt;
		printf("[DEBUG] room %d timer=%.3f (dt=%.3f)\n", room->id, g->bomb_timer, dt);


        if (g->bomb_timer <= 0.0f) {
            int dead = g->bomb_holder_slot;

            // 呼叫你剛剛寫的爆炸/計分邏輯
            bomb_handle_explode(room, dead, now);
        }
    }

}
