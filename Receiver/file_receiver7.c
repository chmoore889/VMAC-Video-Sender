//File Receiver - Christopher Moore
//Version 1 - Functioning Receiver Program
//Version 2 - Writing to temp file to store out of order frames and rewriting to final file in order
//Version 3 - Added ability for partial image reconstruction with frame loss in PNG
//Version 4 - Made PNG frames completely independent of each other
//Version 5 - Added decompression for received data & user interface
//Version 6 - Allowed for increased size of decompressed data in each frame
//Version 7 - Added ability for partial video recovery with frame loss in mp4 and mov

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>

#include "lodepng.h"
#include "zlib.h"

#include <math.h>

//#define FILE_NAME "RPi_Logo.png"
//#define INTEREST_NAME "Raspberry"
#define RECV_TIMEOUT 5 //Amount of time(s) between each frame that is allowed to pass before automatically declaring the end of the transmission
#define BUFFER_SIZE 1024

FILE *compTemp;
FILE *timestamps;
//uint8_t isDone = 0;//Changes to 1 when transmission end statement is received
uint8_t hasStarted = 0;//Changes to 1 when first data frame is received
uint8_t firstSeqReceived = 0;
unsigned int highestSeq = 0, lowestSeq = 0;
volatile clock_t lastframeTime;
unsigned int frameCounter = 1;

//Queue declaration
struct Queue* queue;

//Multithreading
pthread_mutex_t lock;
pthread_t tid;

//Timestamp variables
long ms;
time_t sec;
struct timespec spec;

unsigned int receivedSize = 0;
unsigned int expectedSize = 0;

struct tempCompData//Struct for received compressed data
{
	uint16_t sequence;
	uint16_t len;
	char data[BUFFER_SIZE];
}toWrite;

struct sizeData//Struct for storing currSize variables(variables that store amount of data sent so far)
{
	uint16_t sequence;
	uint32_t size;
};

//Queue stuff
// A linked list (LL) node to store a queue entry 
struct QNode
{
    struct tempCompData* data; 
    struct QNode* next; 
}; 
  
// The queue, front stores the front node of LL and rear stores the 
// last node of LL 
struct Queue
{
    struct QNode *front, *rear;
}; 
  
// A utility function to create a new linked list node. 
struct QNode* newNode(struct tempCompData* data) 
{ 
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode)); 
    temp->data = malloc(sizeof(struct tempCompData));
    memcpy(temp->data, data, sizeof(struct tempCompData));
    
    temp->next = NULL; 
    return temp;
} 
  
// A utility function to create an empty queue 
struct Queue* createQueue() 
{ 
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue)); 
    q->front = q->rear = NULL; 
    return q; 
} 
  
void enQueue(struct Queue* q, struct tempCompData* data) 
{ 
    // Create a new LL node 
    struct QNode* temp = newNode(data); 
  
    // If queue is empty, then new node is front and rear both 
    if (q->rear == NULL)
    { 
		q->front = q->rear = temp; 
        return; 
    } 
  
    // Add the new node at the end of queue and change rear 
    q->rear->next = temp; 
    q->rear = temp; 
} 

struct tempCompData* deQueue(struct Queue* q) 
{ 
    // If queue is empty, return NULL. 
    if (q->front == NULL) 
        return NULL;
  
    // Store previous front and move front one node ahead 
    struct QNode* temp = q->front;
    struct tempCompData* data = temp->data; 
    free(temp);
  
    q->front = q->front->next; 
  
    // If front becomes NULL, then change rear also as NULL 
    if (q->front == NULL) 
        q->rear = NULL; 
    return data; 
}


/**
 *  changeEndian  - Change endianness
 *
 *  Changes byte order of an unsigned 32-bit integer.
 *	Used for reading directly from files as most store big-endian data.
 *	
 *	Arguments :
 *	@x : Unsigned 32-bit integer input.
 */
uint32_t changeEndian(uint32_t x)
{
	return (((x>>24) & 0x000000ff) | ((x>>8) & 0x0000ff00) | ((x<<8) & 0x00ff0000) | ((x<<24) & 0xff000000));
}

/**
 *  writeCompStruct  - Struct writing function
 *
 *  Copies data with its associated length and sequence into the tempCompData struct pointed to by toWriteStruct.
 *	
 *	Arguments :
 *	@toWriteStruct : Pointer to the tempCompData struct to write to.
 *	@length : Length of the data pointed to by buff.
 *	@sequence : Sequence of the data pointed to by buff.
 *	@buff : Pointer to the data to write to the struct.
 */
void writeCompStruct(struct tempCompData *toWriteStruct, uint16_t length, uint16_t sequence, char* buff)
{
	toWriteStruct->sequence = sequence;
	toWriteStruct->len = length;
	if(buff != NULL)
	{
		memcpy(toWriteStruct->data,buff,length);
	}
}

/**
 *  copyFile  - Copys data from one file to another
 *
 *  Copies srcSize bytes of data from the src file to the current file stream location of the dest file through a 1024 byte data buffer.
 *	
 *	Arguments :
 *	@dest : Destination file's pointer. Must be opened with write permissions.
 *	@src : Source file's pointer. Must be opened with read permissions.
 *	@srcSize : Number of bytes to be copied from the source file.
 */
void copyFile(FILE* dest,FILE* src,long int srcSize)//Appends all data in src to current file stream location of dest
{
	char data[BUFFER_SIZE];
	
	fseek(src,0,SEEK_SET);
	
	unsigned int strayBytes = srcSize%BUFFER_SIZE;
	unsigned int iterations = srcSize/BUFFER_SIZE;
	
	for(int x = 0;x <iterations;x++)
	{
		fread(data,BUFFER_SIZE,1,src);
		fwrite(data,BUFFER_SIZE,1,dest);
	}
	fread(data,strayBytes,1,src);
	fwrite(data,strayBytes,1,dest);
}

void* processQueue(void* queue)
{
  while(1)
  {
    pthread_mutex_lock(&lock);
    struct tempCompData* data = deQueue(queue);
    pthread_mutex_unlock(&lock); 

    if((clock()/CLOCKS_PER_SEC >= lastframeTime + RECV_TIMEOUT) && data == NULL && hasStarted == 1)
    {
      return NULL;
    }

    if(data != NULL)
    {
      fwrite(data, sizeof(struct tempCompData), 1, compTemp);
    }
  }
}

void vmac_register(void* ptr);
void del_name(char* interest_name, uint16_t name_len);
void send_vmac(uint16_t type, uint16_t rate, uint16_t seq, char *buff, uint16_t len, char * interest_name, uint16_t name_len);

/**
 *  recv_frame  - Receives and stores data frames
 *
 *  Writes data length, frame sequence, and data buffer to a struct and writes that struct to a file. 
 *	Also does comparisons to find the range of sequences, records the time of the frame, and writes formatted timestamps to a separate file.
 *
 *	No processing of the received data is done other than writing it to a file.
 */
void recv_frame(uint8_t type, uint64_t enc, char * buff, uint16_t len, uint16_t seq,char* interest_name, uint16_t interest_name_len)
{	
	if(type==1 /*&& isDone == 0*/)
	{
		//printf("seq: %u\n",seq);
		/*
		char buffer[len+1];
		memcpy(buffer,buff,len);
		
		buffer[len]='\0';//Adding null terminator for string comparison
		if(strcmp("DONE",buffer)==0)//String comparison to determine last frame
		{
			//printf("DONE Received");
			highestSeq = seq-1;
			isDone=1;
			return;
		}
		*/
		
		writeCompStruct(&toWrite,len,seq,buff);
		//fwrite(&toWrite,sizeof(struct tempCompData),1,compTemp);

		pthread_mutex_lock(&lock); 
		enQueue(queue, &toWrite);
		pthread_mutex_unlock(&lock);

		hasStarted = 1;
		
		if(firstSeqReceived==0)
		{
			lowestSeq = seq;
			firstSeqReceived = 1;
		}
		if(seq>highestSeq)
		{
			highestSeq = seq;
		}
		if(seq<lowestSeq)
		{
			lowestSeq = seq;
		}
		
		lastframeTime = clock()/CLOCKS_PER_SEC;//Records frame time for the receiver timeout
		
		//fprintf(timestamps,"Received Frame @ timestamp=%lu %"PRIdMAX".%03ld - Count: %u\n",(unsigned long)time(NULL),(intmax_t)sec, ms, frameCounter);
		frameCounter++;
		receivedSize += len;
	}
}

/**
 *	main - Main function
 *
 *	Function registers the process and obtains user input for the output filename and the name of the interest to send.
 *	The function waits for data to be received and times out RECV_TIMEOUT seconds after the last frame is received.
 *	
 *	Frames are found by searching through the file written to by recv_frame. The frame with the lowest sequence is used 
 *	to determine the filetype which is then used to determine how the data is processed and written to the output file.
 *	
 *	Missing data is replaced with 0x00 and if there is too much loss, the function exits without writing to the output file.
 */
int main()
{
	void (*recv_frame_ptr)(uint8_t, uint64_t, char *, uint16_t, uint16_t, char *, uint16_t)=&recv_frame;
	vmac_register(recv_frame_ptr);
	
	char fileName[255];
	char intname[BUFFER_SIZE];
	
	//User input for file name, interest name, and timeout before returning data to receivers
	printf("Enter name of file to obtain: ");
	scanf("%s",fileName);
	
	printf("Enter interest name: ");
	scanf("%s",intname);
	
	char data[BUFFER_SIZE];
	memcpy(data,intname,strlen(intname)+1);
	
	uint16_t len = strlen(data)+1;
	uint16_t name_len = strlen(intname);
	
	//Queue Initialization
	queue = createQueue();

	//Thread stuff
	if (pthread_mutex_init(&lock, NULL) != 0) 
	{ 
		printf("\n mutex init has failed\n"); 
		return 1; 
	} 

	int threadError = pthread_create(&tid, NULL, processQueue, queue);
	if (threadError != 0) 
		printf("\nThread can't be created :[%s]", strerror(threadError));

	//Creating temp files to write struct with data frame and associated sequence number
	compTemp = fopen("compTemp", "wb+");//Received compressed data
	
	if (compTemp == NULL) 
    {   
		printf("Error! Could not open temporary file\n"); 
		exit(-1);
    }
	
	timestamps = fopen("timestamps", "w");//Received compressed data
	
	if (timestamps == NULL) 
    {   
		printf("Error! Could not open timestamp file\n"); 
        exit(-1);
    }
	
	clock_gettime(CLOCK_REALTIME,&spec);
	sec=spec.tv_sec;
	ms=round(spec.tv_nsec / 1.0e6);
	if (ms > 999) 
	{
		sec++;
		ms=0;
	}
	fprintf(timestamps,"Interest Sent @ timestamp=%lu %"PRIdMAX".%03ld\n",(unsigned long)time(NULL),(intmax_t)sec, ms);
	
	//Sending Interest
	send_vmac(0,0,0,data,len,intname,name_len);
	printf("Interest Sent\n");
	
	//Waits for other thread to finish writing data to file
	pthread_join(tid, NULL);
	pthread_mutex_destroy(&lock);
	
	fprintf(timestamps,"Data Received @ timestamp=%lu %"PRIdMAX".%03ld\n",(unsigned long)time(NULL),(intmax_t)sec, ms);

	
	//isDone = 1;
	printf("Data Received\n");
	printf("%u Frames Received\n",frameCounter);
	
	//printf("Lowest seq; %u",lowestSeq);
	
	char fileType[4];
	fseek(compTemp,0,SEEK_SET);
	while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Writes frame with lowest sequence to toWrite
	{
		if(toWrite.sequence == lowestSeq)
		{
			break;
		}
	}
	memcpy(&fileType,&toWrite.data,3);
	fileType[3] = '\0';
	//printf("Filetype: %s\n",fileType);
	
	//Processes received data as PNG data based on extension
	//NOT RELATED TO VIDEO TRANSMISSION
	if(strcmp(fileType,"PNG")==0)
	{
		uint16_t headerSize = 0;
		uint8_t bytesPerPixel, colortype;
		unsigned width, height;
		
		unsigned error;
		unsigned char* png;
		size_t pngsize;
		LodePNGState state;
	
		lodepng_state_init(&state);
		
		headerSize += 3;//Increase header size to include the "PNG" string
		
		memcpy(&bytesPerPixel,&toWrite.data[headerSize],sizeof(bytesPerPixel));
		//printf("BytesPerPixel: %u\n",bytesPerPixel);
		headerSize += sizeof(bytesPerPixel);
		
		memcpy(&colortype,&toWrite.data[headerSize],sizeof(colortype));
		//printf("Colortype: %u\n",colortype);
		headerSize += sizeof(colortype);
		if(colortype==0)
		{
			state.info_raw.colortype = LCT_GREY;
		}
		else if(colortype==2)
		{
			state.info_raw.colortype = LCT_RGB;
		}
		else if(colortype==3)
		{
			state.info_raw.colortype = LCT_PALETTE;
		}
		else if(colortype==4)
		{
			state.info_raw.colortype = LCT_GREY_ALPHA;
		}
		else
		{
			state.info_raw.colortype = LCT_RGBA;
		}
		
		state.info_raw.bitdepth = (bytesPerPixel/(colortype==0?1:(colortype==2?3:(colortype==4?2:4))))*8;
		
		memcpy(&width,&toWrite.data[headerSize],sizeof(width));
		//printf("Width: %u\n",width);
		headerSize += sizeof(width);
		
		memcpy(&height,&toWrite.data[headerSize],sizeof(height));
		//printf("Height: %u\n",height);
		headerSize += sizeof(height);
		
		unsigned char* image = malloc(bytesPerPixel*width*height);
		
		char chunkName[5];
		memcpy(&chunkName,&toWrite.data[headerSize],4);
		chunkName[4] = '\0';
		//printf("First chunk: %s\n",chunkName);
		
		const unsigned isPresent = 1;
		if(strcmp("bKGD",chunkName)==0)
		{
			//printf("bkGD Found\n");
			
			memcpy(&state.info_png.background_defined,&isPresent,sizeof(state.info_png.background_defined));
			
			headerSize += 4;
			memcpy(&state.info_png.background_r,&toWrite.data[headerSize],sizeof(state.info_png.background_r));
			headerSize += sizeof(state.info_png.background_r);
			memcpy(&state.info_png.background_g,&toWrite.data[headerSize],sizeof(state.info_png.background_g));
			headerSize += sizeof(state.info_png.background_g);
			memcpy(&state.info_png.background_b,&toWrite.data[headerSize],sizeof(state.info_png.background_b));
			headerSize += sizeof(state.info_png.background_b);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		if(strcmp("pHYs",chunkName)==0)
		{
			//printf("pHYs Found\n");
			
			memcpy(&state.info_png.phys_defined,&isPresent,sizeof(state.info_png.phys_defined));
			
			headerSize += 4;
			memcpy(&state.info_png.phys_x,&toWrite.data[headerSize],sizeof(state.info_png.phys_x));
			headerSize += sizeof(state.info_png.phys_x);
			
			memcpy(&state.info_png.phys_y,&toWrite.data[headerSize],sizeof(state.info_png.phys_y));
			headerSize += sizeof(state.info_png.phys_y);
			
			memcpy(&state.info_png.phys_unit,&toWrite.data[headerSize],sizeof(state.info_png.phys_unit));
			headerSize += sizeof(state.info_png.phys_unit);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		if(strcmp("iCCP",chunkName)==0)
		{
			//printf("iCCP Found\n");
			
			memcpy(&state.info_png.iccp_defined,&isPresent,sizeof(state.info_png.iccp_defined));
			
			headerSize += 4;
			
			uint8_t profNameLen;
			unsigned iccSize;
			memcpy(&profNameLen,&toWrite.data[headerSize],sizeof(profNameLen));
			headerSize += sizeof(profNameLen);
			char* profName = malloc(profNameLen);
			
			memcpy(profName,&toWrite.data[headerSize],profNameLen);
			headerSize += profNameLen;

			memcpy(&iccSize,&toWrite.data[headerSize],sizeof(iccSize));
			headerSize += sizeof(iccSize);
			unsigned char *iccProf = malloc(iccSize);
			
			memcpy(iccProf,&toWrite.data[headerSize],iccSize);
			headerSize += state.info_png.iccp_profile_size;
			
			lodepng_set_icc(&state.info_png,profName,iccProf,iccSize);
			free(profName);
			free(iccProf);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		if(strcmp("sRGB",chunkName)==0)
		{
			//printf("sRGB Found\n");
			
			//Set cHRM & gAMA to sRGB values in case decoder does not use sRGB
			memcpy(&state.info_png.gama_defined,&isPresent,sizeof(state.info_png.gama_defined));
			state.info_png.gama_gamma = 45455;
			memcpy(&state.info_png.chrm_defined,&isPresent,sizeof(state.info_png.chrm_defined));
			state.info_png.chrm_white_x = 31270;
			state.info_png.chrm_white_y = 32900;
			state.info_png.chrm_red_x = 64000;
			state.info_png.chrm_red_y = 33000;
			state.info_png.chrm_green_x = 30000;
			state.info_png.chrm_green_y = 60000;
			state.info_png.chrm_blue_x = 15000;
			state.info_png.chrm_blue_y = 6000;
			
			memcpy(&state.info_png.srgb_defined,&isPresent,sizeof(state.info_png.srgb_defined));
			
			headerSize += 4;
			memcpy(&state.info_png.srgb_intent,&toWrite.data[headerSize],sizeof(state.info_png.srgb_intent));
			headerSize += sizeof(state.info_png.srgb_intent);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		if(strcmp("cHRM",chunkName)==0)
		{
			//printf("cHRM Found\n");
			
			memcpy(&state.info_png.chrm_defined,&isPresent,sizeof(state.info_png.chrm_defined));
			
			headerSize += 4;
			memcpy(&state.info_png.chrm_white_x,&toWrite.data[headerSize],sizeof(state.info_png.chrm_white_x));
			headerSize += sizeof(state.info_png.chrm_white_x);
			
			memcpy(&state.info_png.chrm_white_y,&toWrite.data[headerSize],sizeof(state.info_png.chrm_white_y));
			headerSize += sizeof(state.info_png.chrm_white_y);
			
			memcpy(&state.info_png.chrm_red_x,&toWrite.data[headerSize],sizeof(state.info_png.chrm_red_x));
			headerSize += sizeof(state.info_png.chrm_red_x);
			
			memcpy(&state.info_png.chrm_red_y,&toWrite.data[headerSize],sizeof(state.info_png.chrm_red_y));
			headerSize += sizeof(state.info_png.chrm_red_y);
			
			memcpy(&state.info_png.chrm_green_x,&toWrite.data[headerSize],sizeof(state.info_png.chrm_green_x));
			headerSize += sizeof(state.info_png.chrm_green_x);
			
			memcpy(&state.info_png.chrm_green_y,&toWrite.data[headerSize],sizeof(state.info_png.chrm_green_y));
			headerSize += sizeof(state.info_png.chrm_green_y);
			
			memcpy(&state.info_png.chrm_blue_x,&toWrite.data[headerSize],sizeof(state.info_png.chrm_blue_x));
			headerSize += sizeof(state.info_png.chrm_blue_x);
			
			memcpy(&state.info_png.chrm_blue_y,&toWrite.data[headerSize],sizeof(state.info_png.chrm_blue_y));
			headerSize += sizeof(state.info_png.chrm_blue_y);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		if(strcmp("gAMA",chunkName)==0)
		{
			//printf("gAMA Found\n");
			
			memcpy(&state.info_png.gama_defined,&isPresent,sizeof(state.info_png.gama_defined));
			
			headerSize += 4;
			memcpy(&state.info_png.gama_gamma,&toWrite.data[headerSize],sizeof(state.info_png.gama_gamma));
			headerSize += sizeof(state.info_png.gama_gamma);
			
			memcpy(&chunkName,&toWrite.data[headerSize],4);
			chunkName[4] = '\0';
		}
		
		if(strcmp("IDAT",chunkName)==0)
		{
			//printf("IDAT Found\n");
			headerSize += 4;		
			
			struct sizeData* currSizeArr = malloc(sizeof(struct sizeData));
			fseek(compTemp,0,SEEK_SET);
			
			int count = 1;
			while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))
			{
				currSizeArr = realloc(currSizeArr,count*sizeof(struct sizeData));
				memcpy(&currSizeArr[count-1].sequence,&toWrite.sequence,sizeof(toWrite.sequence));
				memcpy(&currSizeArr[count-1].size,&toWrite.data[headerSize],sizeof(uint32_t));
				//printf("Size: %llu\n",currSizeArr[count-1].size);
				count++;
			}
			count -= 2;
			//printf("currSizeArr allocated and filled\n");
			
			uint32_t currSize = 0;
			uint16_t offsetOut = 0;
			uint32_t nextSeq = 0;
			uint8_t wasFound = 0;//=0 when the expected sequence wasn't found
			uLongf destLen, temp, compLen;
			while(nextSeq<=highestSeq)
			{
				//printf("Current Seq %d\n",nextSeq);
				fseek(compTemp,0,SEEK_SET);
				while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Scans temp file for data associated with nextSeq
				{
					//printf("Sequence Read: %d\n",toWrite.sequence);
					if(toWrite.sequence == nextSeq)
					{
						memcpy(&currSize,&toWrite.data[headerSize],sizeof(currSize));
						
						destLen = bytesPerPixel*width*height-currSize;
						//printf("Width: %u height: %u bytesPerPixel %d currSize %u headerSize: %u\n",width,height,bytesPerPixel,currSize,headerSize);
						temp = destLen;
						
						uint16_t offsetIn = 0;
						offsetOut = 0;
						while(1)
						{
							destLen = temp;
							
							memcpy(&compLen,&toWrite.data[headerSize+sizeof(currSize)+offsetIn],sizeof(compLen));
							//printf("Comp len: %lu	destLen: %lu\n",compLen,destLen);
							
							if((headerSize+sizeof(currSize)+sizeof(compLen)+offsetIn)>=toWrite.len)
							{
								break;
							}
							
							int error = uncompress((Bytef *)&image[currSize+offsetOut],&destLen,(Bytef *)&toWrite.data[headerSize+sizeof(currSize)+sizeof(compLen)+offsetIn],compLen);
							
							if(error != Z_OK)
							{
								switch(error)
								{
									case Z_MEM_ERROR:
										printf("Compression Memory Error\n");
										break;

									case Z_BUF_ERROR:
										printf("Compression Buffer Error\n");
										break;
										
									case Z_DATA_ERROR:
										printf("Compression Data Error\n");
										break;
										
									default:
										printf("Compression Unknown error: %d\n",error);
										break;
								}
								exit(error);
							}
							
							temp -= destLen;
							offsetOut += destLen;
							offsetIn += sizeof(compLen) + compLen;
						}
						
						wasFound = 1;
						break;
					}
				}
				
				if(wasFound)
				{
					wasFound = 0;
				}
				else
				{
					int shouldBreak = 0;
					while(1)
					{
						int requestedSeq = nextSeq+1;
						//printf("Requested Seq: %d\n",requestedSeq);
						for(int x = 0;x<=count;x++)
						{
							if(currSizeArr[x].sequence==requestedSeq||nextSeq==highestSeq)
							{
								memset(&image[currSize+offsetOut],0x00,(nextSeq==highestSeq?width*height*bytesPerPixel:currSizeArr[x].size)-currSize-offsetOut);
								shouldBreak = 1;
								break;
							}
						}
						
						if(shouldBreak)
						{
							wasFound = 0;
							break;
						}
						else
						{
							nextSeq++;
						}
					}
				}
				//printf("Seq written: %u\n",nextSeq);
				nextSeq++;
			}
			free(currSizeArr);
		}		
		
		//printf("Data extracted\n");
		//printf("bytesperpixel %d\n",bytesPerPixel);
		
		error = lodepng_encode(&png, &pngsize, image, width, height, &state);
		if(!error)
		{
			lodepng_save_file(png, pngsize, fileName);
		}
		if(error)
		{
			printf("error %u: %s\n", error, lodepng_error_text(error));
		}
		
		lodepng_state_cleanup(&state);
		
		free(image);
		free(png);
	}
	
	//Processes received data as MP4 data based on extension
	else if(strcmp(fileType,"MP4")==0)
	{
		//Opening files that are to be used
		FILE* file  = fopen(fileName, "wb");
		
		if (file == NULL) 
		{   
			printf("Error! Could not open file\n"); 
			exit(-1);
		}
		
		FILE* mdatTemp  = fopen("mdatTemp", "wb+");
		
		if (mdatTemp == NULL) 
		{   
			printf("Error! Could not open mdat temporary file\n"); 
			exit(-1);
		}
		
		FILE* moovTemp  = fopen("moovTemp", "wb+");
		
		if (moovTemp == NULL) 
		{   
			printf("Error! Could not open moov temporary file\n"); 
			exit(-1);
		}
		
		uint16_t headerSize = 3;
		char chunkName[5];
		chunkName[4] = '\0';
		uint32_t chunkSize;
		
		fseek(compTemp,0,SEEK_SET);
		while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))
		{
			memcpy(chunkName,&toWrite.data[headerSize+sizeof(chunkSize)],4);
			if(strcmp(chunkName,"mdat")==0)
			{
				break;
			}
		}
		
		memcpy(&chunkSize,&toWrite.data[headerSize],sizeof(chunkSize));
		headerSize += sizeof(chunkSize);
		
		uint32_t mdatSize = changeEndian(chunkSize);
		
		memcpy(chunkName,&toWrite.data[headerSize],4);
		headerSize+=4;
		
		fwrite(&chunkSize,sizeof(chunkSize),1,mdatTemp);
		fwrite("mdat",4,1,mdatTemp);
		
		
		//////////////////////////
		//Processing mdat frames//
		//////////////////////////
		struct sizeData* currSizeArr = malloc(sizeof(struct sizeData));
		
		fseek(compTemp,0,SEEK_SET);
		int count = 1;
		unsigned int mdatHighestSeq = 0;
		unsigned int mdatLowestSeq = highestSeq;
		unsigned int mdatLowestSize = 0;
		while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Makes an array of currSize's
		{
			memcpy(chunkName,&toWrite.data[headerSize-4],4);
			//printf("Chunk name: %s\n",chunkName);
			if(strcmp(chunkName,"mdat")==0)
			{
				if(mdatHighestSeq<toWrite.sequence)
				{
					mdatHighestSeq = toWrite.sequence;
				}
				currSizeArr = realloc(currSizeArr,count*sizeof(struct sizeData));
				memcpy(&currSizeArr[count-1].sequence,&toWrite.sequence,sizeof(toWrite.sequence));
				memcpy(&currSizeArr[count-1].size,&toWrite.data[headerSize],sizeof(uint32_t));
				
				if(mdatLowestSeq>toWrite.sequence)
				{
					mdatLowestSeq = toWrite.sequence;
					memcpy(&mdatLowestSize,&toWrite.data[headerSize],sizeof(uint32_t));
					//printf("mdatLowestSeq: %u\n",mdatLowestSeq);
				}
				//printf("Size: %u\n",currSizeArr[count-1].size);
				count++;
			}
		}
	
		currSizeArr = realloc(currSizeArr,count*sizeof(struct sizeData));//Adds fake entry for when the last frame is not received
		uint16_t tempSeq = mdatHighestSeq+1;
		memcpy(&currSizeArr[count-1].sequence,&tempSeq,sizeof(toWrite.sequence));
		uint32_t tempSize = changeEndian(chunkSize);
		memcpy(&currSizeArr[count-1].size,&tempSize,sizeof(tempSize));
		
		//printf("mdatHighestSeq: %u mdatLowestSeq: %u\n",mdatHighestSeq,mdatLowestSeq);
		
		count -= 1;
		headerSize += sizeof(uint32_t);
		
		if(mdatLowestSize!=0)
		{
			mdatLowestSeq -= 1;
		}
		
		expectedSize += tempSize;
		
		int32_t nextSeq = mdatLowestSeq;
		uint8_t hasFinished = 0;//Changes to 1 when chunk has ended
		uint8_t frameFound = 0;//Changes to 1 when frame is found
		uint32_t currSize = 0;
		uint16_t dataSize = 0;

		while(hasFinished == 0||currSize+toWrite.len == tempSize)
		{
			//printf("Current Seq %d\n",nextSeq);
			fseek(compTemp,0,SEEK_SET);
			while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Scans temp file for data associated with nextSeq
			{
				//printf("Sequence Read: %d\n",toWrite.sequence);
				if(toWrite.sequence == nextSeq)
				{	
					frameFound = 1;
					memcpy(chunkName,&toWrite.data[headerSize-sizeof(uint32_t)-4],4);
					if(strcmp(chunkName,"mdat")==0)
					{
						memcpy(&currSize,&toWrite.data[headerSize-sizeof(uint32_t)],sizeof(currSize));
						//printf("CurrSize read: %u\n",currSize);
						
						fwrite(&toWrite.data[headerSize],toWrite.len-headerSize,1,mdatTemp);
						
						dataSize = toWrite.len-headerSize;
					}
					break;
				}
			}
			
			if(frameFound == 1)
			{
				frameFound = 0;
			}
			else
			{
				int shouldBreak = 0;
				while(1)//Loops until correct amount of missing data is found and is written to temp file
				{
					uint16_t requestedSeq = nextSeq+1;
					//printf("Requested Seq: %d\n",requestedSeq);
					for(int x = 0;x<=count;x++)
					{
						//printf("currSize: %u\n",currSizeArr[x].sequence);
						char hexZero = 0x00;
						uint32_t numZeros;
						if(currSizeArr[x].sequence==requestedSeq)
						{
							numZeros = currSizeArr[x].size-(currSize+dataSize);
							//printf("numZeros: %u currSizearr: %u seq: %u currSize: %u dataSize: %u requestedSeq: %u nextSeq: %u\n",numZeros,currSizeArr[x].size,currSizeArr[x].sequence,currSize,dataSize,requestedSeq,nextSeq);
							for(int y = 0;y<numZeros;y++)
							{
								fwrite(&hexZero,sizeof(hexZero),1,mdatTemp);
							}
							shouldBreak = 1;
							break;
						}
						if(requestedSeq>mdatHighestSeq)
						{
							numZeros = tempSize;
							for(int y = 0;y<numZeros;y++)
							{
								fwrite(&hexZero,sizeof(hexZero),1,mdatTemp);
							}
							shouldBreak = 1;
							break;
						}
					}
					
					if(shouldBreak)
					{
						if(requestedSeq > mdatHighestSeq)
						{
							hasFinished =1;
						}
						frameFound = 0;
						break;
					}
					else
					{
						nextSeq++;
					}
				}
			}	
			nextSeq++;
			expectedSize += headerSize;
		}
		free(currSizeArr);
		
		
		//////////////////////////
		//Processing moov frames//
		//////////////////////////
		uLongf destLen, compLen;
		
		headerSize = 3 + sizeof(chunkSize);
		uint8_t moovFirst = 0;
		fseek(compTemp,0,SEEK_SET);
		while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Searches for ftyp and writes to final file if exists
		{
			memcpy(chunkName,&toWrite.data[headerSize],4);
			if(strcmp(chunkName,"moov")==0)
			{
				headerSize += 4;
				
				memcpy(&moovFirst,&toWrite.data[headerSize],sizeof(moovFirst));
				headerSize += sizeof(moovFirst);
				
				memcpy(chunkName,&toWrite.data[headerSize+sizeof(chunkSize)],4);
				if(strcmp(chunkName,"ftyp")==0)
				{
					memcpy(&chunkSize,&toWrite.data[headerSize],sizeof(chunkSize));
					
					chunkSize = changeEndian(chunkSize);
					
					fwrite(&toWrite.data[headerSize],chunkSize,1,file);
					headerSize += chunkSize;
				}
				
				memcpy(&chunkSize,&toWrite.data[3],sizeof(chunkSize));//Copies moov chunk size to chunkSize
				break;
			}
		}
		
		
		uint32_t highestSubSeq = 0, subSeq;
		compLen = 0;
		
		fseek(compTemp,0,SEEK_SET);
		while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Find highest sub sequence
		{
			memcpy(chunkName,&toWrite.data[3 + sizeof(chunkSize)],4);
			if(strcmp(chunkName,"moov")==0)
			{
				memcpy(&subSeq,&toWrite.data[headerSize],sizeof(highestSubSeq));
				if(highestSubSeq<subSeq)
				{
					highestSubSeq = subSeq;
				}
			}
		}
		Bytef* moovDat = malloc(BUFFER_SIZE*(highestSubSeq+1));
		//printf("Highest sub seq: %u\n",highestSubSeq);
		
		for(uint32_t x = 0;x <= highestSubSeq;x++)//Writes moov data to temp file
		{
			fseek(compTemp,0,SEEK_SET);
			while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))
			{
				memcpy(chunkName,&toWrite.data[3 + sizeof(chunkSize)],4);
				//printf("%s\n",chunkName);
				if(strcmp(chunkName,"moov")==0)
				{
					memcpy(&subSeq,&toWrite.data[headerSize],sizeof(subSeq));
					//printf("%u\n",subSeq);
					if(subSeq == x)
					{
						memcpy(&moovDat[compLen],&toWrite.data[headerSize + sizeof(subSeq)],toWrite.len-(headerSize+4));
						
						compLen += toWrite.len-(headerSize + sizeof(subSeq));
						
						//printf("%d\n",toWrite.len-(headerSize + sizeof(subSeq)));
						expectedSize += toWrite.len*2;
						break;
					}
				}
			}
		}
		
		//Printing calculated loss
		//NOTE: Loss will not be accurate if either moov and mdat data is completely lost
		printf("Loss: %f%%\n",((1-(double)receivedSize/expectedSize))*100);
		
		//Decompression of moov data
		destLen = changeEndian(chunkSize) - sizeof(chunkSize) - 4;
		Bytef* decompDat = malloc(destLen);
		
		//printf("CompLen: %lu destLen: %lu\n",compLen,destLen);

		uLongf tempLen = destLen;
		int error = uncompress(decompDat,&destLen,moovDat,compLen);
		
		if(destLen != tempLen)//Exits and cleans up if there is missing moov data
		{
			printf("Error: moov data lost in transmission\n");
			
			fclose(mdatTemp);
			fclose(moovTemp);
			
			if(remove("mdatTemp")!=0)
			{
				printf("Error: unable to delete mdat temporary file\n");
			}
			
			if(remove("moovTemp")!=0)
			{
				printf("Error: unable to delete moov temporary file\n");
			}
			
			fclose(compTemp);
	
			del_name(intname,name_len);
			
			if(remove("compTemp")!=0)
			{
				printf("Error: unable to delete compressed temporary file\n");
			}
			
			exit(-1);
		}
		
		if(error != Z_OK)
		{
			switch(error)
			{
				case Z_MEM_ERROR:
					printf("Compression Memory Error\n");
					break;

				case Z_BUF_ERROR:
					printf("Compression Buffer Error\n");
					break;
					
				case Z_DATA_ERROR:
					printf("Compression Data Error\n");
					break;
					
				default:
					printf("Compression Unknown error: %d\n",error);
					break;
			}
			fclose(mdatTemp);
			fclose(moovTemp);
			fclose(compTemp);
			exit(error);
		}
		
		free(moovDat);
		
		fwrite(&chunkSize,sizeof(chunkSize),1,moovTemp);
		uint32_t moovSize = changeEndian(chunkSize);
		fwrite("moov",4,1,moovTemp);
		fwrite(decompDat,destLen,1,moovTemp);
		
		free(decompDat);
		
		//Putting all data in correct order in a single file
		uint32_t wideSize = 4+sizeof(uint32_t);
		wideSize = changeEndian(wideSize);
		if(moovFirst != 1)
		{
			fwrite(&wideSize,sizeof(wideSize),1,file);
			fwrite("wide",4,1,file);
			copyFile(file,mdatTemp,mdatSize);
			copyFile(file,moovTemp,moovSize);
		}
		else
		{
			copyFile(file,moovTemp,moovSize);
			fwrite(&wideSize,sizeof(wideSize),1,file);
			fwrite("wide",4,1,file);
			copyFile(file,mdatTemp,mdatSize);
		}	
		
		fclose(mdatTemp);
		fclose(moovTemp);
		
		if(remove("mdatTemp")!=0)
		{
			printf("Error: unable to delete mdat temporary file\n");
		}
		
		if(remove("moovTemp")!=0)
		{
			printf("Error: unable to delete moov temporary file\n");
		}
		
	}
	
	//If file type is not found then do general receive
	else
	{
		FILE* file  = fopen(fileName, "wb");
		
		if (file == NULL) 
		{   
			printf("Error! Could not open file\n"); 
			exit(-1);
		}
		
		int nextSeq = 0;
		while(nextSeq<=highestSeq)
		{
			//printf("Current Seq %d\n",nextSeq);
			fseek(compTemp,0,SEEK_SET);
			while(fread(&toWrite, sizeof(struct tempCompData), 1, compTemp))//Scans temp file for data associated with nextSeq
			{
				//printf("Sequence Read: %d\n",toWrite.sequence);
				if(toWrite.sequence == nextSeq)
				{
					fwrite(&toWrite.data,toWrite.len,1,file);
					break;
				}
			}
			nextSeq++;
		}
		fclose(file);
	}
	
	fclose(compTemp);
	
	del_name(intname,name_len);
	
	if(remove("compTemp")!=0)
	{
		printf("Error: unable to delete compressed temporary file\n");
	}

	return 0;
}