#define BUFFER_SIZE 1024

#ifndef SEND_FUNCTIONS_H
#define SEND_FUNCTIONS_H

void generalSend(char fileName[],char *data,char *intname,uint16_t name_len);

void pngSend(char fileName[],char *data,char *intname,uint16_t name_len);

void mp4Send(char fileName[],char *data,char *intname,uint16_t name_len,int rate);

#endif