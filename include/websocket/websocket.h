#ifndef _WEBSOCKET_H
#define _WEBSOCKET_H

int response(int conn_fd, const u_char *message, unsigned long size, u_char opcode);
void websocket_shakehand(int conn_fd, const char *server_key);
char *deal_data(const char *buffer,const int buf_len);
char *calculate_accept_key(const char * buffer);

#endif