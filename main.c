#include <stdio.h>
#include <unistd.h>

extern int start_websocket_server();
extern int init_stream();

int main(){
    init_stream();
    start_websocket_server();
    while(1){
        sleep(5);
    }
    return 0;
}