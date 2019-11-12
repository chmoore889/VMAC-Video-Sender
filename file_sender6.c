//File Sender - Christopher Moore
//Version 1 - Functioning Sender Program
//Version 2 - Added ability for partial image reconstruction with frame loss in PNG
//Version 3 - Made PNG frames completely independent of each other
//Version 4 - Added compression for sent data & user interface
//Version 5 - Packs more compressed data into each frame
//Version 6 - Added ability for partial video recovery with frame loss in mp4 and mov

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sendFunctions5.h"

//#define FILE_NAME "RPi_Logo.png"
//#define INTEREST_NAME "Raspberry"
//#define NUM_RECEIVERS 1
//#define RECV_TIMEOUT 5

char intname[BUFFER_SIZE];
clock_t lastInterestTime;

/**
 *  getExt  - Parses file extension
 *
 *  Finds file extension based on the presence of the '.' character and returns the pointer to the extension.
 *	
 *	Arguments :
 *	@fspec : Filename string to obtain the extension of.
 */
char *getExt (char *fspec) {
    char *e = strrchr (fspec, '.');
    if (e == NULL)
        e = "";
    return e+1;
}

void vmac_register(void* ptr);
void send_vmac(uint16_t type, uint16_t rate, uint16_t seq, char *buff, uint16_t len, char * interest_name, uint16_t name_len);
void recv_frame(uint8_t type, uint64_t enc, char * buff, uint16_t len, uint16_t seq,char* interest_name, uint16_t interest_name_len)
{
}
void setfixed_rate(uint8_t rate);
void disable_frame_adaptation();

/**
 *	main - Main function
 *
 *	Function registers the process and obtains user input for the input filename, the name of the interest to send, frame rate value, and interest timeout(seconds).
 *
 *	The function waits for the interest timeout and calls pngSend, mp4Send, or generalSend based on the input filename extension.
 */
int main()
{
	void (*recv_frame_ptr)(uint8_t, uint64_t, char *, uint16_t, uint16_t, char *, uint16_t)=&recv_frame;
	vmac_register(recv_frame_ptr);
	char fileName[255];
	char data[BUFFER_SIZE];
	
	//User input for file name, interest name, and the number of receivers to wait for
	printf("Enter name of file to send: ");
	scanf("%s",fileName);
	
	printf("Enter interest name: ");
	scanf("%s",intname);
	uint16_t name_len=strlen(intname);
	
	int rate = -1;
	printf("Choose frame rate value from V-MAC Doc table(<0 uses V-MAC frame rate adaptation): ");
	scanf("%d",&rate);
	if(rate>=0)
	{
		disable_frame_adaptation();
		setfixed_rate(rate);
	}
	
	unsigned int timeout = 0;
	printf("Enter interest timeout(seconds): ");
	scanf("%u",&timeout);
	
	clock_t temp = clock()/CLOCKS_PER_SEC;
	clock_t stopTime = clock()/CLOCKS_PER_SEC + timeout;
	printf("%ld seconds remaining",stopTime-clock()/CLOCKS_PER_SEC);
	fflush(stdout);

	while(stopTime-clock()/CLOCKS_PER_SEC > 0)
	{
		if(clock()/CLOCKS_PER_SEC != temp)
		{
			printf("\r%ld seconds remaining ",stopTime-clock()/CLOCKS_PER_SEC);
			fflush(stdout);
			temp = clock()/CLOCKS_PER_SEC;
		}
	}
	
	printf("\rSending...           \n");
	fflush(stdout);
	
	//Checks file extensions to determine sending method
	//printf("File extension: %s\n",getExt(fileName));
	if(strcmp(getExt(fileName),"png") == 0)
	{
		//printf("PNG Send\n");
		pngSend(fileName,data,intname,name_len);
	}
	else if(strcmp(getExt(fileName),"mov") == 0 || strcmp(getExt(fileName),"mp4") == 0)//mov and mp4 are similar enough in most cases that allows them to be sent the same way.
	{
		//printf("MP4 Send\n");
		mp4Send(fileName,data,intname,name_len,rate);
	}
	else
	{
		//printf("General Send\n");
		generalSend(fileName,data,intname,name_len);
	}
	
	/*
	char done[] = "DONE";
	int len = strlen(done);
	send_vmac(1,0,0,done,len,intname,name_len);
	*/
	printf("Sent\n");
	
	return 0;
}