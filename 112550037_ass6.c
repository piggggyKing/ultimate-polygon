#include	"unp.h"
#include	"stdlib.h"


int find_empty(int* connfds) {
	for(int i = 0; i < 10; i++) {
		if(connfds[i] == 0) return i;
	}
	return -1;
}

int find_index(int* connfds, int connfd) {
	for(int i = 0; i < 10; i++) {
		if(connfds[i] == connfd) return i;
	}
	return -1;
}

int find_max(int* connfds){
	int max = 0;
	for(int i = 0; i < 10; i++) {
		if(connfds[i] > max) {
			max = connfds[i];
		}
	}
	return max;
}


int
main(int argc, char **argv)
{
	int					listenfd, connfd;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;

	listenfd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(SERV_PORT+5);

	int opt = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		close(listenfd);
		exit(1);
	}

	Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listenfd, LISTENQ);


	int connfds[10], user_num = 0, n, waiting[10];
	char sendline[MAXLINE], recvline[2048];
	char** users = malloc(10 * sizeof(char*));
	char** w_users = malloc(10 * sizeof(char*));
	for(int i = 0 ; i < 10; i++) {
		connfds[i] = 0;
		waiting[i] = 0;
		users[i] = malloc(32 * sizeof(char));
		w_users[i] = malloc(32 * sizeof(char));
	}

	fd_set rset, wset;
	int maxfdp1;


	for ( ; ; ) {
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_SET(listenfd, &rset);
		for(int i = 0; i < 10; i++) {
			if(connfds[i] != 0) {
				FD_SET(connfds[i], &rset);
				FD_SET(connfds[i], &wset);
			}
		}
		maxfdp1 = max(listenfd, find_max(connfds)) + 1;
		Select(maxfdp1, &rset, &wset, NULL, NULL);

		if(user_num < 10 && waiting[0] != 0) {
			int e = find_empty(connfds);

			connfds[e] = waiting[0];
			strcpy(users[e], w_users[0]);
			for(int i = 0; i < 9; i++) {
				strcpy(w_users[i], w_users[i+1]);
			}
			for(int i = 0; i < 9; i++) {
				waiting[i] = waiting[i+1];
			}
			user_num++;
			sprintf(sendline, "You are the #%d user.\n", e+1);
			Write(connfds[e], sendline, strlen(sendline));
			printf("send %d-%s: %s\n", connfds[e], users[e], sendline);
			sprintf(sendline, "You may now type in or wait for other users.\n");
			Write(connfds[e], sendline, strlen(sendline));
			printf("send %d-%s: %s\n", connfds[e], users[e], sendline);

			for(int i = 0; i < 10; i++) {
				sprintf(sendline, "(#%d user %s enters)\n", e+1, users[e]);
				if(connfds[i] != 0 && i != e) {
					Write(connfds[i], sendline, strlen(sendline));
					printf("send %d-%s: %s", connfds[i], users[i], sendline);
				}
			}
		}

		if(FD_ISSET(listenfd, &rset)) {
			clilen = sizeof(cliaddr);
			connfd = accept(listenfd, (SA *) &cliaddr, &clilen);
			n = read(connfd, recvline, 2048);
			recvline[n] = '\0';
			printf("recv %d: %s\n", connfd, recvline);
		
			if(user_num >= 10) {
				int e = find_empty(waiting);
				waiting[e] = connfd;
				strcpy(w_users[e], recvline);
				continue;
			}

			int e = find_empty(connfds);
			connfds[e] = connfd;
			strcpy(users[e], recvline);
			user_num++;
			sprintf(sendline, "You are the #%d user.\n", e+1);
			Write(connfd, sendline, strlen(sendline));
			printf("send %d-%s: %s\n", connfds[e], users[e], sendline);
			sprintf(sendline, "You may now type in or wait for other users.\n");
			Write(connfd, sendline, strlen(sendline));
			printf("send %d-%s: %s\n", connfds[e], users[e], sendline);

			for(int i = 0; i < 10; i++) {
				sprintf(sendline, "(#%d user %s enters)\n", e+1, users[e]);
				if(connfds[i] != 0 && connfds[i] != connfd) {
					Write(connfds[i], sendline, strlen(sendline));
					printf("send %d-%s: %s", connfds[i], users[i], sendline);
				}
			}
		}

		for(int i = 0; i < 10; i++) {
			if(FD_ISSET(connfds[i], &rset)) {
				n = read(connfds[i], recvline, 2048);
				if(n == 0) {
					if(FD_ISSET(connfds[i], &wset)) {
						sprintf(sendline, "Bye!\n");
						Write(connfds[i], sendline, strlen(sendline));
						printf("send %d-%s: %s", connfds[i], users[i], sendline);
						close(connfds[i]);
					}

					user_num--;
					printf("%s call shutdown\n", users[i]);

					if(user_num == 1) {
						sprintf(sendline, "(%s left the room. You are the last one. Press Ctrl+D to leave or wait for a new user.)\n", users[i]);
						for(int j = 0; j < 10; j++) {
							if(connfds[j] != 0 && connfds[j] != connfds[i]) {
								Write(connfds[j], sendline, strlen(sendline));
								printf("send %d-%s: %s", connfds[j], users[j], sendline);
							}
						}
					}
					else {
						sprintf(sendline, "(%s left the room. %d users left)\n", users[i], user_num);
						for(int j = 0; j < 10; j++) {
							if(connfds[j] != 0 && connfds[j] != connfds[i]) {
								Write(connfds[j], sendline, strlen(sendline));
								printf("send %d-%s: %s", connfds[j], users[j], sendline);
							}
						}
					}

					connfds[i] = 0;
					strcpy(users[i], "");
				}
				else {
					recvline[n] = '\0';
					printf("recv %d-%s: %s\n", connfds[i], users[i], recvline);
					sprintf(sendline, "(%s) %s\n", users[i], recvline);
					for(int j = 0; j < 10; j++) {
						if(connfds[j] != 0 && connfds[j] != connfds[i]) {
							Write(connfds[j], sendline, strlen(sendline));
							printf("send %d-%s: %s", connfds[j], users[j], sendline);
						}
					}
				}
			}
		}
	}
}
