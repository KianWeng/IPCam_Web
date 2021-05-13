#include <stdio.h>
#include <unistd.h>
#include <signal.h>

extern int start_websocket_server();
extern int init_stream();
extern int server_sock_fd;
extern int write_video_run;
extern int websocket_run;
static int main_thread_run = 1;

static void sigstop()
{
    printf("receive sigstop\n");
    write_video_run = 0;
    websocket_run = 0;
    sleep(1);
	if (server_sock_fd != 0) {
        printf("close server.\n");
        close(server_sock_fd);
    }
    main_thread_run = 0;
}

int main(){

    signal(SIGINT, sigstop);
	signal(SIGQUIT, sigstop);
	signal(SIGTERM, sigstop);

    init_stream();
    start_websocket_server();
    while(main_thread_run){
        sleep(5);
    }
    return 0;
}