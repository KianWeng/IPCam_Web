#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define BACKLOG 5     //完成三次握手但没有accept的队列的长度
#define CONCURRENT_MAX 8   //应用层同时可以处理的连接
#define SERVER_PORT 11332
#define BUFFER_SIZE 1024

int client_fds[CONCURRENT_MAX];
static char input_msg[BUFFER_SIZE];
static char recv_msg[BUFFER_SIZE];
static int  server_sock_fd;

extern int handle_client_msg(int client_id, char *msg);

void *websocket_monitor(void *arg)
{
    //fd_set
    fd_set server_fd_set;
    int max_fd = -1;
    struct timeval tv; //超时时间设置

    while(1)
    {
    	tv.tv_sec = 20;
    	tv.tv_usec = 0;
    	FD_ZERO(&server_fd_set);
    
	    //服务器端socket
        FD_SET(server_sock_fd, &server_fd_set);
        // printf("server_sock_fd=%d\n", server_sock_fd);
        if(max_fd < server_sock_fd)
        {
        	max_fd = server_sock_fd;
        }
	    //客户端连接
        for(int i =0; i < CONCURRENT_MAX; i++)
        {
        	//printf("client_fds[%d]=%d\n", i, client_fds[i]);
        	if(client_fds[i] != 0)
        	{
        		FD_SET(client_fds[i], &server_fd_set);
        		if(max_fd < client_fds[i])
        		{
        			max_fd = client_fds[i];
        		}
        	}
        }

        int ret = select(max_fd + 1, &server_fd_set, NULL, NULL, &tv);
        if(ret < 0)
        {
        	perror("select 出错\n");
        	continue;
        }
        else if(ret == 0)
        {
        	printf("select 超时\n");
        	continue;
        }
        else
        {
        	if(FD_ISSET(server_sock_fd, &server_fd_set))
        	{
        		//有新的连接请求
        		struct sockaddr_in client_address;
        		socklen_t address_len;
        		int client_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_address, &address_len);
        		printf("new connection client_sock_fd = %d\n", client_sock_fd);
        		if(client_sock_fd > 0)
        		{
        			int index = -1;
        			for(int i = 0; i < CONCURRENT_MAX; i++)
        			{
        				if(client_fds[i] == 0)
        				{
        					index = i;
        					client_fds[i] = client_sock_fd;
        					break;
        				}
        			}
        			if(index >= 0)
        			{
        				printf("新客户端(%d)加入成功 %s:%d\n", index, inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
        			}
        			else
        			{
        				bzero(input_msg, BUFFER_SIZE);
        				strcpy(input_msg, "服务器加入的客户端数达到最大值,无法加入!\n");
        				send(client_sock_fd, input_msg, BUFFER_SIZE, 0);
        				printf("客户端连接数达到最大值，新客户端加入失败 %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
        			}
        		}
        	}
        	for(int i =0; i < CONCURRENT_MAX; i++)
        	{
        		if(client_fds[i] !=0)
        		{
        			if(FD_ISSET(client_fds[i], &server_fd_set))
        			{
        				//处理某个客户端过来的消息
        				bzero(recv_msg, BUFFER_SIZE);
        				long byte_num = recv(client_fds[i], recv_msg, BUFFER_SIZE, 0);
        				if (byte_num > 0)
        				{
        					if(byte_num > BUFFER_SIZE)
        					{
        						byte_num = BUFFER_SIZE;
        					}
        					recv_msg[byte_num] = '\0';
        					printf("客户端(%d):接收到%d个字节.\n", i, byte_num);
							for(int j = 0; j < byte_num; j++){
								printf("%d ", recv_msg[j]);
							}
							handle_client_msg(i, recv_msg);
        				}
        				else if(byte_num < 0)
        				{
        					printf("从客户端(%d)接受消息出错.\n", i);
        				}
        				else
        				{
        					FD_CLR(client_fds[i], &server_fd_set);
        					client_fds[i] = 0;
        					printf("客户端(%d)退出了\n", i);
        				}
        			}
        		}
        	}
        }
    }

    return ((void *)0);
}

int start_websocket_server(){
	int ret = 0, err;
	pthread_t ws_server_tid;
    //本地地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bzero(&(server_addr.sin_zero), 8);
    //创建socket
    server_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sock_fd == -1)
    {
    	perror("socket error");
    	return -1;
    }

    //绑定socket
    int bind_result = bind(server_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if(bind_result == -1)
    {
    	perror("bind error");
    	return -2;
    }

    //listen
    if(listen(server_sock_fd, BACKLOG) == -1)
    {
    	perror("listen error");
    	return -3;
    }

	err = pthread_create(&ws_server_tid, NULL, websocket_monitor, NULL);
	if (err != 0) {
		fprintf(stderr, "can't create websocket monitor thread: %s\n", strerror(err));
		ret = -4;
	}

	return ret;
}