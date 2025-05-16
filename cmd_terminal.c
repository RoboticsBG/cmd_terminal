#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define SERVER_PORT 9000
#define SERVER_IP "10.10.50.142"     // Set this to the target machine's IP


#define MAX_EVENTS 10
#define BUFFER_SIZE 1024


int sockfd;
struct sockaddr_in server_addr;

struct termios orig_termios;

void send_cmd(char c);
int create_timerfd(int interval_ms);
int create_send_socket();
void disable_raw_mode();
void enable_raw_mode();
int send_tcp_data(char *pstr);

volatile sig_atomic_t stop_flag = 0;

void handle_sigint(int sig) 
{
    stop_flag = 1;
}


int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() 
{
   
    int epfd, tfd;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    char c = 0;
    int rd_key = 0, ret;
    uint64_t  expirations;

    signal(SIGINT, handle_sigint);
	enable_raw_mode();
    set_nonblocking(STDIN_FILENO);

    // 1. Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

	// 2. Connect to server
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);


 	if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        	perror("connect");
        	close(sockfd);
        	return 1;
    	}

	printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

	// 3. Set non-blocking I/O
    set_nonblocking(sockfd);



    // Create epoll instance
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    tfd = create_timerfd(100);  // fire every  100ms
        if (tfd < 0){
                return -1;
    }

    //  Add TCP socket to epoll
    event.events = EPOLLIN;
    event.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event);

    //  Add stdin to epoll
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event);

    //  Add tfd to epoll
    event.events = EPOLLIN;
    event.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &event);


    printf("Ready to sent on TCP port %d and waiting for commands\n", SERVER_PORT);

    while (!stop_flag) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("epoll_wait failed");
            break;
        }


        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == sockfd) {
		int r = read(sockfd, buffer, BUFFER_SIZE - 1);
                if (r > 0) {
                    buffer[r] = '\0';
                    printf("\r%s",buffer);
		    fflush(stdout);
		 }
		else{
		    printf("Server disconnected.\n");
                    stop_flag = 1;
                    break;

		}
            }

            else if (fd == STDIN_FILENO) {
                ssize_t r = read(STDIN_FILENO, &c, 1);
                if (r > 0) {
                    //printf("Key pressed: '%c'\n", c);
		   rd_key = 1;
                    if (c == 'q') {
                        //printf("Quit key pressed. Exiting...\n");
                        goto cleanup;
                    }
                }
            }

	    else if (fd == tfd) {
		read(tfd, &expirations, sizeof(expirations));
                if (rd_key == 1){
                    //printf("Key pressed: '%c'\n", c);
		    send_cmd(c);
		    rd_key  = 0;
		}

            }

        }
    }

cleanup:
    close(epfd);
    close(sockfd);
    close(tfd);
    return 0;
}

void send_cmd(char c)
{
	char str_cmd[64];

	switch(c){
	case 'i':
		sprintf(str_cmd,"%s", "m2000");
		send_tcp_data(str_cmd);
		break;
	case 'm':
		sprintf(str_cmd,"%s", "m-2000");
		send_tcp_data(str_cmd);
		break;
	case 'j':
		sprintf(str_cmd,"%s", "r1000");
		send_tcp_data(str_cmd);
		break;
	case 'k':
		sprintf(str_cmd,"%s", "r-1000");
		send_tcp_data(str_cmd);
		break;
	}

	//printf("Key pressed: '%c'\n", c);

}


int create_timerfd(int interval_ms) 
{

        struct itimerspec ts;
        int tfd;

        tfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (tfd == -1) {
                perror("timerfd_create");
                return -1;
        }

        ts.it_value.tv_sec = interval_ms / 1000;
        ts.it_value.tv_nsec = (interval_ms % 1000) * 1000000;
        ts.it_interval.tv_sec = interval_ms / 1000;
        ts.it_interval.tv_nsec = (interval_ms % 1000) * 1000000;

        if (timerfd_settime(tfd, 0, &ts, NULL) == -1) {
                perror("timerfd_settime");
                return -1;
        }

        return tfd;
}


int send_tcp_data(char *pstr)
{

	ssize_t sent = send(sockfd, pstr, strlen(pstr), 0);
	if (sent < 0) {
            perror("Cmd send failed");
            return -1;
        } else {
            //printf("[SENT] %s\n", pstr);
        }

        return 0;
}



void disable_raw_mode() 
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() 
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);  // Restore on exit

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Turn off echo and canonical mode
    raw.c_cc[VMIN] = 1;   // Minimum 1 char
    raw.c_cc[VTIME] = 0;  // No timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
