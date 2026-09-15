#include "DEV_Config.h"
#include "MMC_SD.h"			   
					   					   
unsigned char  SD_Type=0;  //version of the sd card

//data: data to be written to sd card.
//return: data read from sd card.
unsigned char SD_SPI_ReadWriteByte(unsigned char CMD)
{
	return SPI4W_Write_Byte(CMD);
//	return SPI_Read_Byte();
}	  

//set spi in low speed mode.
void SD_SPI_SpeedLow(void)
{
//	SPI1->CR1&=0XFFC7; 
//	SPI1->CR1|=SPI_BaudRatePrescaler_256;
//	SPI_Cmd(SPI1,ENABLE); 
}


//set spi in high speed mode.
void SD_SPI_SpeedHigh(void)
{
//	SPI1->CR1&=0XFFC7; 
//	SPI1->CR1|=SPI_BaudRatePrescaler_32;
//	SPI_Cmd(SPI1,ENABLE); 
}


//released spi bus
void SD_DisSelect(void)
{
	DEV_Digital_Write(SD_CS_PIN,1);
 	SD_SPI_ReadWriteByte(0xff);//providing extra 8 clocks  
}

//pick sd card and waiting until until it's ready
//return: 0: succed 1: failure
unsigned char SD_Select(void)
{
	DEV_Digital_Write(SD_CS_PIN,0);
	if(SD_WaitReady()==0)return 0; 
	SD_DisSelect();
	return 1;
}

//waiting for sd card until it's ready
unsigned char SD_WaitReady(void)
{
	unsigned int t=0;
	do{
		if(SD_SPI_ReadWriteByte(0XFF) == 0XFF)
			return 0;
		t++;		  	
	}while(t<0XFFFFFF);
	return 1;
}

//waiting for response from sd card.
//Response: expect from sd card.
//return: succeed for 0, fail for other else 
//return: 0 for success, other for failure.   
unsigned char SD_GetResponse(unsigned char Response)
{
	unsigned short Count=0xFFFF;	   						  
	while ((SD_SPI_ReadWriteByte(0XFF) != Response) && Count)
		Count--; 	  
	if (Count==0)
		return MSD_RESPONSE_FAILURE;  
	else 
		return MSD_RESPONSE_NO_ERROR;
}

//read a buffer from sd card.
//*buf: pointer to a buffer.
//len: length of the buffer.
//return: 0 for success, other for failure.   
unsigned char SD_RecvData(unsigned char *buf,unsigned short len)
{			  	  
	if(SD_GetResponse(0xFE))
		return 1;//waiting for start command send back from sd card.
    while(len--){//receiving data...
        *buf = SD_SPI_ReadWriteByte(0xFF);
        buf++;
    }

    //send 2 dummy write (dummy CRC)
    SD_SPI_ReadWriteByte(0xFF);
    SD_SPI_ReadWriteByte(0xFF);									  					    
    return 0;
}

//write a buffer containing 512 bytes to sd card.
//buf: data buffer
//cmd: command
//return: 0 for success, other for failure.   
unsigned char SD_SendBlock(unsigned char*buf,unsigned char cmd)
{	
	unsigned short t;		  	  
	if(SD_WaitReady())return 1;
	SD_SPI_ReadWriteByte(cmd);
	if(cmd!=0XFD){
		for(t=0;t<512;t++)
			SD_SPI_ReadWriteByte(buf[t]);
	    SD_SPI_ReadWriteByte(0xFF);//ignoring CRC
	    SD_SPI_ReadWriteByte(0xFF);
		t = SD_SPI_ReadWriteByte(0xFF);
		if((t&0x1F)!=0x05)
			return 2;								  					    
	}						 									  					    
    return 0;
}

//send a command to sd card 
//cmd£∫command
//arg: parameter
//crc: crc
//return: response sent back from sd card.
unsigned char SD_SendCmd(unsigned char cmd, unsigned int arg, unsigned char crc)
{
    unsigned char r1;	
	unsigned char Retry=0; 
	SD_DisSelect();
	if(SD_Select())return 0XFF;

    SD_SPI_ReadWriteByte(cmd | 0x40);
    SD_SPI_ReadWriteByte(arg >> 24);
    SD_SPI_ReadWriteByte(arg >> 16);
    SD_SPI_ReadWriteByte(arg >> 8);
    SD_SPI_ReadWriteByte(arg);	  
    SD_SPI_ReadWriteByte(crc); 
	if(cmd == CMD12)SD_SPI_ReadWriteByte(0xff); //Skip a stuff byte when stop reading
	Retry = 0X1F;
	do{
		r1 = SD_SPI_ReadWriteByte(0xFF);
	}while((r1&0X80) && Retry--);	 

    return r1;
}


//obtain CID including manufacturer informationfrom sd card  
//*cid_dat: pointer to the buffer storing CID, at least 16 bytes.
//return: 0 no error  1 error
unsigned char SD_GetCID(unsigned char *cid_data)
{
    unsigned char r1;	   

    r1=SD_SendCmd(CMD10,0,0x01);
    if(r1 == 0x00){
		r1=SD_RecvData(cid_data,16);	 
    }
	SD_DisSelect();
	if(r1)return 1;
	else return 0;
}																				  

//obtain CSD including storage and speed.
//*csd_data : pointer to the buffer storing CSD, at least 16 bytes.
//return: 0 no error  1 error
unsigned char SD_GetCSD(unsigned char *csd_data)
{
    unsigned char r1;	 
    r1 = SD_SendCmd(CMD9,0,0x01);//∑¢CMD9√¸¡Ó£¨∂¡CSD send CMD9 in order to get CSD
    if(r1 == 0)	{
    	r1=SD_RecvData(csd_data, 16);
    }
	SD_DisSelect();
	if(r1)return 1;
	else return 0;
}  

//obtian the totals of sectors of sd card.
//return: 0 error, other else for storage of sd card.
//numbers of bytes of each sector must be 512, otherwise fail to initialization.  
uint32_t SD_GetSectorCount(void)
{
    unsigned char csd[16];
    unsigned int Capacity;  
    unsigned char n;
	unsigned short csize;  					    
	
    if(SD_GetCSD(csd)!=0) return 0;	    
    //calculation for SDHC below
    if((csd[0]&0xC0)==0x40)	 //V2.00
    {	
		csize = csd[9] + ((unsigned short)csd[8] << 8) + 1;
		Capacity = (unsigned int)csize << 10;  //totals of sectors 		   
    }else//V1.XX
    {	
		n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
		csize = (csd[8] >> 6) + ((unsigned short)csd[7] << 2) + ((unsigned short)(csd[6] & 3) << 10) + 1;
		Capacity= (unsigned int)csize << (n - 9); 
    }
    return Capacity;
}

//initialize sd card 
unsigned char SD_Initialize(void)
{
    unsigned char r1;      
    unsigned short retry;  
    unsigned char buf[4];  
	unsigned short i;
   	
	DEV_Digital_Write(SD_CS_PIN,1);
 	SD_SPI_SpeedLow();	
 	for(i=0;i<10;i++)SD_SPI_ReadWriteByte(0XFF);
	retry=20;
	do
	{
		r1=SD_SendCmd(CMD0,0,0x95);//enter to idle state
	}while((r1!=0X01) && retry--);
 	SD_Type=0;
    
	if(r1==0X01)
	{
		if(SD_SendCmd(CMD8,0x1AA,0x87)==1)//SD V2.0
		{
			for(i=0;i<4;i++)buf[i]=SD_SPI_ReadWriteByte(0XFF);	//Get trailing return value of R7 resp
			if(buf[2]==0X01&&buf[3]==0XAA)//is it support of 2.7~3.6V 
			{
				retry=0XFFFE;
				do
				{
					SD_SendCmd(CMD55,0,0X01);	
					r1=SD_SendCmd(CMD41,0x40000000,0X01);
				}while(r1&&retry--);
				if(retry&&SD_SendCmd(CMD58,0,0X01)==0) //start to identify the SD2.0 version of sd card.
				{
					for(i=0;i<4;i++)buf[i]=SD_SPI_ReadWriteByte(0XFF);//get OCR
					if(buf[0]&0x40)SD_Type=SD_TYPE_V2HC;    //check CCS
					else SD_Type=SD_TYPE_V2;   
				}
			}
		}else//SD V1.x/ MMC	V3
		{
			SD_SendCmd(CMD55,0,0X01);		
			r1=SD_SendCmd(CMD41,0,0X01);	
			if(r1<=1)
			{		
				SD_Type=SD_TYPE_V1;
				retry=0XFFFE;
				do //exit idle state
				{
					SD_SendCmd(CMD55,0,0X01);	
					r1=SD_SendCmd(CMD41,0,0X01);
				}while(r1&&retry--);
			}else
			{
				SD_Type=SD_TYPE_MMC;//MMC V3
				retry=0XFFFE;
				do 
				{											    
					r1=SD_SendCmd(CMD1,0,0X01);
				}while(r1&&retry--);  
			}
			if(retry==0||SD_SendCmd(CMD16,512,0X01)!=0)SD_Type=SD_TYPE_ERR;
		}
	}
	SD_DisSelect();
	SD_SPI_SpeedHigh();
	if(SD_Type)return 0;
	else if(r1)return r1; 	   
	return 0xaa;
}


//read SD card
//buf: data buffer
//sector: sector
//cnt: totals of sectors]
//return: 0 ok, other for failure
uint8_t SD_ReadDisk(uint8_t *buf,uint32_t sector,uint8_t cnt)
{
	unsigned char r1;
	if(SD_Type!=SD_TYPE_V2HC)sector <<= 9;
	if(cnt==1)
	{
		r1=SD_SendCmd(CMD17,sector,0X01);
		if(r1==0)
		{
			r1=SD_RecvData(buf,512);   
		}
	}else
	{
		r1=SD_SendCmd(CMD18,sector,0X01);
		do
		{
			r1=SD_RecvData(buf,512);
			buf+=512;  
		}while(--cnt && r1==0); 	
		SD_SendCmd(CMD12,0,0X01);	
	}   
	SD_DisSelect();
	return r1;//
}


//write sd card 
//buf: data buffer
//sector: start sector
//cnt: totals of sectors]
//return: 0 ok, other for failure
uint8_t SD_WriteDisk(uint8_t *buf,uint32_t sector,uint8_t cnt)
{
	unsigned char r1;
	if(SD_Type!=SD_TYPE_V2HC)sector *= 512;
	if(cnt==1)
	{
		r1=SD_SendCmd(CMD24,sector,0X01);
		if(r1==0)
		{
			r1=SD_SendBlock(buf,0xFE); 
		}
	}else
	{
		if(SD_Type!=SD_TYPE_MMC)
		{
			SD_SendCmd(CMD55,0,0X01);	
			SD_SendCmd(CMD23,cnt,0X01);	
		}
 		r1=SD_SendCmd(CMD25,sector,0X01);
		if(r1==0)
		{
			do
			{
				r1=SD_SendBlock(buf,0xFC); 
				buf+=512;  
			}while(--cnt && r1==0);
			r1=SD_SendBlock(0,0xFD);
		}
	}   
	SD_DisSelect();
	return r1;
}	   
























	  






