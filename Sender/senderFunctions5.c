//File Sender Supplement - Christopher Moore
//Contains different methods of breaking up files to send

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sendFunctions5.h"
#include "lodepng.h"
#include "zlib.h"

/**
 *  changeEndian  - Change endianness
 *
 *  Changes byte order of an unsigned 32-bit integer.
 *	
 *	Arguments :
 *	@x : Unsigned 32-bit integer input.
 */
uint32_t changeEndian(uint32_t x)
{
	int y = (((x>>24) & 0x000000ff) | ((x>>8) & 0x0000ff00) | ((x<<8) & 0x00ff0000) | ((x<<24) & 0xff000000));
	return y;
}

/**
 *  findMaxUncompData  - Inverse compressBound
 *
 *  Finds the compressBound input value at which the compressBound output is equal to or less than the function input.
 *	
 *	Arguments :
 *	@desiredCompSize : desired output of compressBound.
 */
uint16_t findMaxUncompData(uLong desiredCompSize)
{
	for(int x = 0;;x++)
	{
		if(compressBound(x)>desiredCompSize)
		{
			return x-1;
		}
	}
}

void send_vmac(uint16_t type, uint16_t rate, uint16_t seq, char *buff, uint16_t len, char * interest_name, uint16_t name_len);

/**
 *  generalSend  - Sends unprocessed file data
 *
 *  Reads file and sends the data in the file with no modifications, using the provided data pointer as a buffer
 *	and intname/name_len as the interest input for send_vmac.
 *	
 *	Arguments :
 *	@fileName : Filename of file to read from.
 *	@data : Pointer to memory to be used as buffer for sending.
 *	@intname : Interest name
 *	@name_len : Length of the interest name
 */
void generalSend(char fileName[],char *data,char *intname,uint16_t name_len)
{
	uint16_t len;
	
	FILE *file = fopen(fileName, "rb");
	
	if (file == NULL) 
    {   
		printf("Error! Could not open file\n"); 
        exit(-1);
    }	
	
	//Finding file size
	fseek(file,0,SEEK_END);
	uint32_t size = ftell(file);
	//printf("Size %d\n",size);
	uint16_t bytesLeft = size%BUFFER_SIZE;//Finding stray bytes that need to be read
	
	//Reading and sending main data chunk
	fseek(file,0,SEEK_SET);
	for(int x = 0;x<(size/1024);x++)
	{
		fread(data,BUFFER_SIZE,1,file);
		len = BUFFER_SIZE;
		//printf("Len %d\n",len);
		send_vmac(1,0,0,data,len,intname,name_len);
	}
	
	if(bytesLeft!=0)
	{
		//Reading and sending stray data
		fread(data,bytesLeft,1,file);
		send_vmac(1,0,0,data,bytesLeft,intname,name_len);
	}
	
	fclose(file);
}

/**
 *  pngSend  - Sends specially formatted PNG data
 *
 *  Uses PNG file to decode raw pixel data and sends raw pixel data with a header using the data pointer as a buffer
 *	and intname/name_len as the interest input for send_vmac.
 *	
 *	Arguments :
 *	@fileName : Filename of file to read from.
 *	@data : Pointer to memory to be used as buffer for sending.
 *	@intname : Interest name
 *	@name_len : Length of the interest name
 */
void pngSend(char fileName[],char *data,char *intname,uint16_t name_len)//NOT RELATED TO VIDEO TRANSMISSION
{
	unsigned error;
	unsigned char* image;
	unsigned width, height;
	unsigned char* png = 0;
	uint32_t pngsize;
	LodePNGState state;
	
	lodepng_state_init(&state);
	
	state.decoder.color_convert = 0;
	
	error = lodepng_load_file(&png, &pngsize, fileName);

	if(!error)
	{
		error = lodepng_decode(&image, &width, &height, &state, png, pngsize);//Writes pixel array to "image"
	}
	if(error)
	{
		printf("error %u: %s\n", error, lodepng_error_text(error));
		exit(-1);
	}
	
	free(png);
	
	uint8_t colortype = state.info_png.color.colortype;
	uint8_t bytesPerPixel = (state.info_png.color.bitdepth/8+(state.info_png.color.bitdepth%8!=0?1:0))*(colortype==0?1:(colortype==2?3:(colortype==4?2:4)));//Determines file color type
	uint32_t imageSize = bytesPerPixel*width*height;
	
	//printf("BytesPerPixel: %u\n",bytesPerPixel);
	
	uint16_t headerSize = 0;
	memcpy(&data[headerSize],"PNG",3);//Sets beginning of every frame to be PNG
	headerSize += 3;
	
	memcpy(&data[headerSize],&bytesPerPixel,sizeof(bytesPerPixel));//Sets next bytes to be bytesPerPixel, colortype, width, and height for encoding
	headerSize += sizeof(bytesPerPixel);
	
	memcpy(&data[headerSize],&colortype,sizeof(colortype));
	headerSize += sizeof(colortype);
	
	memcpy(&data[headerSize],&width,sizeof(width));
	headerSize += sizeof(width);
	
	memcpy(&data[headerSize],&height,sizeof(height));
	headerSize += sizeof(height);
	
	
	if(state.info_png.background_defined)//Checks for bKGD chunk and if present the data is written to the buffer
	{
		memcpy(&data[headerSize],"bKGD",4);
		headerSize += 4;
		
		data[headerSize] = state.info_png.background_r;
		headerSize += sizeof(state.info_png.background_r);
		
		data[headerSize] = state.info_png.background_g;
		headerSize += sizeof(state.info_png.background_g);
		
		data[headerSize] = state.info_png.background_b;
		headerSize += sizeof(state.info_png.background_b);
	}
	if(state.info_png.phys_defined)//Checks for pHYs chunk and if present the data is written to the buffer
	{
		memcpy(&data[headerSize],"pHYs",4);
		headerSize += 4;
		
		data[headerSize] = state.info_png.phys_x;
		headerSize += sizeof(state.info_png.phys_x);

		data[headerSize] = state.info_png.phys_y;
		headerSize += sizeof(state.info_png.phys_y);
		
		data[headerSize] = state.info_png.phys_unit;
		headerSize += sizeof(state.info_png.phys_unit);
	}
	if(state.info_png.iccp_defined)//Checks for iCCP chunk and if present the data is written to the buffer
	{
		memcpy(&data[headerSize],"iCCP",4);
		headerSize += 4;
		
		uint8_t profNameLen = strlen(state.info_png.iccp_name);
		memcpy(&data[headerSize],&profNameLen,sizeof(profNameLen));
		headerSize += sizeof(profNameLen);
		
		memcpy(&data[headerSize],state.info_png.iccp_name,profNameLen);
		headerSize += profNameLen;

		data[headerSize] = state.info_png.iccp_profile_size;
		headerSize += sizeof(state.info_png.iccp_profile_size);
		
		memcpy(&data[headerSize],state.info_png.iccp_profile,state.info_png.iccp_profile_size);
		headerSize += state.info_png.iccp_profile_size;
	}
	if(state.info_png.srgb_defined)
	{
		memcpy(&data[headerSize],"sRGB",4);
		headerSize += 4;
		
		data[headerSize] = state.info_png.srgb_intent;
		headerSize += sizeof(state.info_png.srgb_intent);
	}
	else//Only sends cHRM & gAMA if sRGB is not defined
	{
		if(state.info_png.chrm_defined)//Checks for cHRM chunk and if present the data is written to the buffer
		{			
			memcpy(&data[headerSize],"cHRM",4);
			headerSize += 4;
			
			data[headerSize] = state.info_png.chrm_white_x;
			headerSize += sizeof(state.info_png.chrm_white_x);
			
			data[headerSize] = state.info_png.chrm_white_y;
			headerSize += sizeof(state.info_png.chrm_white_y);
			
			data[headerSize] = state.info_png.chrm_red_x;
			headerSize += sizeof(state.info_png.chrm_red_x);
			
			data[headerSize] = state.info_png.chrm_red_y;
			headerSize += sizeof(state.info_png.chrm_red_y);
			
			data[headerSize] = state.info_png.chrm_green_x;
			headerSize += sizeof(state.info_png.chrm_green_x);
			
			data[headerSize] = state.info_png.chrm_green_y;
			headerSize += sizeof(state.info_png.chrm_green_y);
			
			data[headerSize] = state.info_png.chrm_blue_x;
			headerSize += sizeof(state.info_png.chrm_blue_x);
			
			data[headerSize] = state.info_png.chrm_blue_y;
			headerSize += sizeof(state.info_png.chrm_blue_y);
		}
		
		if(state.info_png.gama_defined)//Checks for gAMA chunk and if present the data is written to the buffer
		{			
			memcpy(&data[headerSize],"gAMA",4);
			headerSize += 4;
			
			data[headerSize] = state.info_png.gama_gamma;
			headerSize += sizeof(state.info_png.gama_gamma);
		}
	}
	
	memcpy(&data[headerSize],"IDAT",4);
	headerSize += 4;
	//printf("bytesperpixel %d width: %u height: %u headerSize: %u\n",bytesPerPixel,width,height,headerSize);
	
	Bytef outBuffer[BUFFER_SIZE];
	uLongf outBufferSize;
	
	uint32_t currSize = 0;//Current total size of data that has been sent so far(excluding header)
	uLong decompSize = (findMaxUncompData(BUFFER_SIZE)%bytesPerPixel!=0?findMaxUncompData(BUFFER_SIZE)/bytesPerPixel*bytesPerPixel:findMaxUncompData(BUFFER_SIZE));
	
	while(currSize<imageSize)
	{
		uint16_t offset = 0;
		uint16_t remainingFrameSize = BUFFER_SIZE - headerSize - sizeof(currSize);//Amount of data that can still be packed into frame
		memcpy(&data[headerSize],&currSize,sizeof(currSize));
		while(1)
		{
			outBufferSize = BUFFER_SIZE;
			int error = compress2(outBuffer, &outBufferSize, (Bytef *)&image[currSize], ((decompSize>(imageSize-currSize))?(imageSize-currSize):decompSize), Z_BEST_COMPRESSION);
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
			
			if(remainingFrameSize<(outBufferSize+sizeof(outBufferSize))||((decompSize>(imageSize-currSize))?(imageSize-currSize):decompSize)==0)
			{
				break;
			}
			
			//printf("Comp Len: %lu Uncomp len: %lu\n",outBufferSize, (decompSize>(imageSize-currSize))?(imageSize-currSize):decompSize);
			
			memcpy(&data[headerSize+sizeof(currSize)+offset],&outBufferSize,sizeof(outBufferSize));//Compressed size of data
			memcpy(&data[headerSize+sizeof(currSize)+sizeof(outBufferSize)+offset],outBuffer,outBufferSize);//Compressed data
			currSize += ((decompSize>(imageSize-currSize))?(imageSize-currSize):decompSize);
			remainingFrameSize -= outBufferSize+sizeof(outBufferSize);
			offset += outBufferSize + sizeof(outBufferSize);
		}
		//printf("frame Size: %u	currSize: %u	imageSize: %u\n",BUFFER_SIZE - remainingFrameSize,currSize,imageSize);
		send_vmac(1,0,0,data,BUFFER_SIZE-remainingFrameSize,intname,name_len);
	} 

	lodepng_state_cleanup(&state);
	free(image);
}

/**
 *  mp4Send  - Sends specially formatted MP4 data
 *
 *  Sends 'moov' chunk data twice and 'mdat' and the file header once using the data pointer as a buffer
 *	and intname/name_len as the interest input for send_vmac.
 *	
 *	Allows the rate to be chosen in frame rate adaptation is disabled.
 *	
 *	Arguments :
 *	@fileName : Filename of file to read from.
 *	@data : Pointer to memory to be used as buffer for sending.
 *	@intname : Interest name
 *	@name_len : Length of the interest name
 */
void mp4Send(char fileName[],char *data,char *intname,uint16_t name_len, int rate)
{
	FILE *file = fopen(fileName, "rb");
	
	if (file == NULL) 
    {   
		printf("Error! Could not open file\n"); 
        exit(-1);
    }
	
	char chunkName[5];
	
	uint16_t headerSize = 0;
	memcpy(&data[headerSize],"MP4",3);//Sets beginning of every frame to be MP4
	headerSize += 3;
	
	uint32_t fchunkSize;//mp4 file chunk size
	fread(&fchunkSize,sizeof(fchunkSize),1,file);
	fchunkSize = changeEndian(fchunkSize);

	fread(chunkName,4,1,file);
	chunkName[4] = '\0';
	
	char* headerData = malloc(0);
	uint16_t fileHeaderSize = 0;
	if(strcmp("ftyp",chunkName)==0)
	{
		headerData = realloc(headerData,fchunkSize);
		fileHeaderSize = fchunkSize;
		
		fseek(file,-4-sizeof(fchunkSize),SEEK_CUR);
		fread(headerData,fchunkSize,1,file);
	}
	else
	{
		fseek(file,0,SEEK_SET);
	}		
	
	long int dataStartPos = ftell(file);
	
	uint16_t dataLen;
	int16_t remainingFrameSize = BUFFER_SIZE;
	uint32_t tempSize;
	uLongf outBufferSize;
	uint32_t currSize = 0;//Amount of data sent so far
	
	uint8_t mdatRead = 0;
	uint8_t moovRead = 0;
	uint8_t moovFirst = 1;
	while(mdatRead != 1 || moovRead != 1)
	{
		fseek(file,dataStartPos,SEEK_SET);
		while(fread(&fchunkSize,sizeof(fchunkSize),1,file))
		{
			fread(chunkName,4,1,file);
			fchunkSize = changeEndian(fchunkSize);
			fchunkSize -= sizeof(fchunkSize)+4;
			if(strcmp("mdat",chunkName)==0 && fchunkSize!=0 && mdatRead!=1 && moovRead != 1)
			{
				moovFirst = 0;
			}
			if(strcmp("mdat",chunkName)==0 && fchunkSize!=0 && mdatRead!=1 && moovRead == 1)
			{	
				//printf("mdat found\n");
				tempSize = fchunkSize + sizeof(fchunkSize) + 4;
				tempSize = changeEndian(tempSize);
				
				memcpy(&data[headerSize], &tempSize, sizeof(fchunkSize));
				
				memcpy(&data[headerSize+sizeof(fchunkSize)], "mdat", 4);
				headerSize += 4+sizeof(currSize) + sizeof(fchunkSize);
				
				currSize = 0;
				
				int count = 0;
				while(fchunkSize>currSize)
				{
					remainingFrameSize = BUFFER_SIZE-headerSize;
					memcpy(&data[headerSize-sizeof(currSize)],&currSize,sizeof(currSize));
					//printf("Curr size: %u chunkSize: %u\n",currSize,fchunkSize);
					
					if(fchunkSize-currSize>remainingFrameSize)
					{
						fread(&data[headerSize],remainingFrameSize,1,file);
						dataLen = remainingFrameSize;
					}
					else
					{
						fread(&data[headerSize],fchunkSize-currSize,1,file);
						dataLen = fchunkSize-currSize;
					}
					
					currSize += dataLen;
					remainingFrameSize -= dataLen;
					
					send_vmac(1,rate,0,data,BUFFER_SIZE-remainingFrameSize,intname,name_len);
					count++;
				}
				
				//printf("count: %d",count);

				headerSize -= 4+sizeof(currSize)+sizeof(fchunkSize);
				mdatRead = 1;
				break;
			}
			else if(strcmp("moov",chunkName)==0 && mdatRead != 1 && moovRead != 1)
			{
				//printf("moov found\n");
				
				currSize = 0;
				uint32_t subSeq = 0;
				tempSize = changeEndian(fchunkSize + sizeof(fchunkSize) + 4);
				memcpy(&data[headerSize], &tempSize,sizeof(fchunkSize));
				memcpy(&data[headerSize+sizeof(fchunkSize)],"moov",4);
				headerSize += 4 + sizeof(fchunkSize);
				
				memcpy(&data[headerSize],&moovFirst,sizeof(moovFirst));
				headerSize += sizeof(moovFirst);
				
				memcpy(&data[headerSize],headerData,fileHeaderSize);
				headerSize += fileHeaderSize;
				
				headerSize += sizeof(subSeq);
				
				outBufferSize = compressBound(fchunkSize);
				Bytef* compTemp = malloc(outBufferSize);
				Bytef* temp = malloc(fchunkSize);
				
				fread(temp,fchunkSize,1,file);
				
				int error = compress2(compTemp, &outBufferSize, temp, fchunkSize, Z_BEST_COMPRESSION);
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
				free(temp);			
				
				
				for(int x = 0;x<2;x++)
				{
					subSeq = 0;
					currSize = 0;
					while(outBufferSize>currSize)
					{
						remainingFrameSize = BUFFER_SIZE-headerSize;
						
						uint16_t frameDataLeft = (remainingFrameSize<outBufferSize-currSize?remainingFrameSize:outBufferSize-currSize);
						memcpy(&data[headerSize],&compTemp[currSize],frameDataLeft);
						
						currSize += frameDataLeft;
						remainingFrameSize -= frameDataLeft;
						
						memcpy(&data[headerSize-sizeof(subSeq)],&subSeq,sizeof(subSeq));
						send_vmac(1,rate,0,data,BUFFER_SIZE-remainingFrameSize,intname,name_len);
						subSeq += 1;
					}
				}
				free(compTemp);
				
				headerSize -= 4 + sizeof(fchunkSize) + sizeof(subSeq) + fileHeaderSize + sizeof(moovFirst);
				//printf("Sub seq: %u",subSeq-1);
				moovRead = 1;
			}
			else
			{
				fseek(file,fchunkSize,SEEK_CUR);
			}
		}
	}
	free(headerData);
	fclose(file);
}