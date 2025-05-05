#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>


#define PORT 8080
#define DEST_IP "10.10.50.142"     // Set this to the target machine's IP
#define DEST_PORT 8081


#define MAX_EVENTS 10
#define BUFFER_SIZE 1024


int s_sockfd, r_sockfd;
struct sockaddr_in destaddr;
struct sockaddr_in addr;

struct termios orig_termios;

void send_cmd(char c);
int create_timerfd(int interval_ms);
int create_send_socket();
void disable_raw_mode();
void enable_raw_mode();
int send_udp_data(char *pstr);


int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() 
{
   
    int r_sockfd, epfd, tfd;
    struct epoll_event event, events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    char c = 0;
    int rd_key = 0, ret;
    uint64_t  expirations;

    enable_raw_mode();
    set_nonblocking(STDIN_FILENO);

    // 1. Create UDP socket
    r_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (r_sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(r_sockfd);

    // 2. Bind UDP socket
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(r_sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(r_sockfd);
        exit(EXIT_FAILURE);
    }

    ret = create_send_socket();
    if (ret < 0)
	return -1;

    // 3. Create epoll instance
    epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        close(r_sockfd);
        exit(EXIT_FAILURE);
    }

    tfd = create_timerfd(100);  // fire every  100ms
        if (tfd < 0){
                return -1;
    }

    // 4. Add UDP socket to epoll
    event.events = EPOLLIN;
    event.data.fd = r_sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, r_sockfd, &event);

    // 5. Add stdin to epoll
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event);

    // 5. Add tfd to epoll
    event.events = EPOLLIN;
    event.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &event);


    printf("Listening on UDP port %d and waiting for commands\n", PORT);

    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("epoll_wait failed");
            break;
        }


        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == r_sockfd) {
                struct sockaddr_in cliaddr;
                socklen_t len = sizeof(cliaddr);
                ssize_t recvd = recvfrom(r_sockfd, buffer, BUFFER_SIZE - 1, 0,
                                         (struct sockaddr *)&cliaddr, &len);
                if (recvd > 0) {
                    buffer[recvd] = '\0';
                    printf("\r%s",buffer);
		    fflush(stdout);
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
    close(r_sockfd);
    close(tfd);
    return 0;
}

void send_cmd(char c)
{
	char str_cmd[64];

	switch(c){
	case 'i':
		sprintf(str_cmd,"%s", "m1000");
		send_udp_data(str_cmd);
		break;
	case 'm':
		sprintf(str_cmd,"%s", "m-1000");
		send_udp_data(str_cmd);
		break;
	case 'j':
		sprintf(str_cmd,"%s", "r1000");
		send_udp_data(str_cmd);
		break;
	case 'k':
		sprintf(str_cmd,"%s", "r-1000");
		send_udp_data(str_cmd);
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

int create_send_socket()
{

	// 1. Create socket
    	if ((s_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        	perror("Socket creation failed");
        	return -1;
    	}

    	// 2. Set destination address
    	memset(&destaddr, 0, sizeof(destaddr));
    	destaddr.sin_family = AF_INET;
    	destaddr.sin_port = htons(DEST_PORT);

    	if (inet_pton(AF_INET, DEST_IP, &destaddr.sin_addr) <= 0) {
        	perror("Invalid destination IP address");
        	close(s_sockfd);
        	return -2;
    	}

    	printf("Cmd sender ready\n");
	return 0;
}


int send_udp_data(char *pstr)
{

        ssize_t sent = sendto(s_sockfd, pstr, strlen(pstr), 0,
                              (struct sockaddr *)&destaddr, sizeof(destaddr));
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
