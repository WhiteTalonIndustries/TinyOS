/**
  ******************************************************************************
  * @file    fatfs_storage.c
  * @author  MCD Application Team
  * @version V1.2.0
  * @date    11-April-2014
  * @brief   This file includes the Storage (FatsFs) driver 
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "MMC_SD.h"
#include "ff.h"
#include "diskio.h"
#include "fatfs_storage.h"

#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include <string.h>



/** @addtogroup STM32_Nucleo_Demo
* @{
*/

/** @defgroup STORAGE
* @brief This file includes the Storage (FatFs) driver for the STM32 Nucleo demo
* @{
*/

/** @defgroup STORAGE_Private_Types
* @{
*/
/**
* @}
*/

/** @defgroup STORAGE_Private_Defines
* @{
*/
/**
* @}
*/

/** @defgroup STORAGE_Private_Macros
* @{
*/
/**
* @}
*/

/** @defgroup STORAGE_Private_Variables
* @{
*/

#define RGB24TORGB16(R,G,B) ((R>>3)<<11)|((G>>2)<<5)|(B>>3)
#define PIXEL(__M)  ((((__M) + 31 ) >> 5) << 2)//对于24位真彩色 每一行的像素宽度必须是4的倍数  否则补0补齐

extern LCD_DIS sLCD_DIS;

uint8_t aBuffer[1440];/* 480 * 3 = 1440 */
FILINFO MyFileInfo;
DIR MyDirectory;
FIL MyFile;
UINT BytesWritten;
UINT BytesRead;
uint16_t pic[76800];
extern uint8_t id;
/**
* @}
*/


/** @defgroup STORAGE_Private_FunctionPrototypes
* @{
*/
/**
* @}
*/

/** @defgroup STORAGE_Private_Functions
* @{
*/




/**
* @brief  Open a file and copy its content to a buffer
* @param  DirName: the Directory name to open
* @param  FileName: the file name to open
* @param  BufferAddress: A pointer to a buffer to copy the file to
* @param  FileLen: the File length
* @retval err: Error status (0=> success, 1=> fail)
*/

uint32_t Storage_OpenReadFile(uint8_t Xpoz, uint16_t Ypoz, const char* BmpName)
{
    uint16_t i, j, k, h;
    
    uint32_t index = 0, size = 0, width = 0, height = 0;
    uint32_t bmpaddress, bit_pixel = 0;
    FIL file1; 

    f_open(&file1, BmpName, FA_READ);	
    f_read(&file1, aBuffer, 30, &BytesRead);

	bmpaddress = (uint32_t)aBuffer;

	/* Read bitmap size */
	size = *(uint16_t *) (bmpaddress + 2);
	size |= (*(uint16_t *) (bmpaddress + 4)) << 16;
//	printf("file size =  %d \r\n",size);
	/* Get bitmap data address offset */
	index = *(uint16_t *) (bmpaddress + 10);
	index |= (*(uint16_t *) (bmpaddress + 12)) << 16;
	// printf("file index =  %d \r\n",index);
	/* Read bitmap width */
	width = *(uint16_t *) (bmpaddress + 18);
	width |= (*(uint16_t *) (bmpaddress + 20)) << 16;
	// printf("file width =  %d \r\n",width);
	/* Read bitmap height */
	height = *(uint16_t *) (bmpaddress + 22);
	height |= (*(uint16_t *) (bmpaddress + 24)) << 16;
	// printf("file height =  %d \r\n",height);
	/* Read bit/pixel */
	bit_pixel = *(uint16_t *) (bmpaddress + 28);  
//	printf("bit_pixel = %d \r\n",bit_pixel);
	f_close (&file1);

    if (24 != bit_pixel) {
        return 0;
    }

	if (width != sLCD_DIS.LCD_Dis_Column || height != sLCD_DIS.LCD_Dis_Page) {
		// printf("width != sLCD_DIS.LCD_Dis_Column \r\n");
		// printf("file width =  %d \r\n",width);
		// printf("file height =  %d \r\n",height);
		// printf("sLCD_DIS.LCD_Dis_Column =  %d \r\n",sLCD_DIS.LCD_Dis_Column);
		// printf("sLCD_DIS.LCD_Dis_Page =  %d \r\n",sLCD_DIS.LCD_Dis_Page);
		return 1;
	}
	
    /* Synchronize f_read right in front of the image data */
    f_open(&file1, (TCHAR const*)BmpName, FA_READ);
    f_read(&file1, aBuffer, index, &BytesRead);
    
    if(LCD_2_8 == id){
		if(sLCD_DIS.LCD_Dis_Page > sLCD_DIS.LCD_Dis_Column)
		{
			for (i = 0; i < height; i ++) {
			  f_read(&file1, aBuffer, PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
			  f_read(&file1, aBuffer + (PIXEL(width * bit_pixel) >> 1), PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
			  for (j = 0; j < width; j ++) {
				  k = j * 3; 
				  pic[i*width+j] = (uint16_t)(((aBuffer[k + 2] >> 3) << 11 ) | ((aBuffer[k + 1] >> 2) << 5) | (aBuffer[k] >> 3));
			  }
			}
		}
		else
		{
			for (i = 0; i < height ; i ++) {
			  f_read(&file1, aBuffer, PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
			  f_read(&file1, aBuffer + (PIXEL(width * bit_pixel) >> 1), PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
			  for (j = 0; j < width; j ++) {
				  k = j * 3; 
				  pic[i*width+j] = (uint16_t)(((aBuffer[k + 2] >> 3) << 11 ) | ((aBuffer[k + 1] >> 2) << 5) | (aBuffer[k] >> 3));
			  }
			}	
		}	
		/* LCD_SetCursor if dont write here ,it will display innormal*/
		LCD_SetCursor(0, 0);
		DEV_Digital_Write(LCD_DC_PIN, 1);
		DEV_Digital_Write(LCD_CS_PIN, 0);
		spi_set_baudrate(SPI_PORT,30*1000*1000);
		for(index=0;index<76800;index++){
			  SPI4W_Write_Byte((pic[index] >> 8) & 0xFF);
			  SPI4W_Write_Byte(pic[index] & 0xFF);
		}
		DEV_Digital_Write(LCD_CS_PIN, 1);
    }else{
		LCD_SetCursor(0, 0);
		spi_set_baudrate(SPI_PORT,30*1000*1000);
		if(sLCD_DIS.LCD_Dis_Page > sLCD_DIS.LCD_Dis_Column)
		{
			for(i = 0; i < height; i ++){
				f_read(&file1, aBuffer, PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
				f_read(&file1, aBuffer+(PIXEL(width * bit_pixel) >> 1), PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
				for (j = 0; j < width; j ++) {
					k = j * 3;
					pic[j] = (uint16_t)(((aBuffer[k+2]>>3)<<11)|((aBuffer[k+1]>>2)<<5)|(aBuffer[k]>>3));
					LCD_WriteData(pic[j]);
				}
			}
		}
		else
		{
			for(i = 0; i < height; i ++){
				f_read(&file1, aBuffer, PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
				f_read(&file1, aBuffer+(PIXEL(width * bit_pixel) >> 1), PIXEL(width * bit_pixel) >> 1, (UINT *)&BytesRead);
				for (j = 0; j < width; j ++) {
					k = j * 3;
					pic[j] = (uint16_t)(((aBuffer[k+2]>>3)<<11)|((aBuffer[k+1]>>2)<<5)|(aBuffer[k]>>3));
					LCD_WriteData(pic[j]);
				}
			}
		}
    }
    f_close(&file1);
    spi_set_baudrate(SPI_PORT,3000*1000);
	Driver_Delay_ms(1500);
    return 1;
}


/**
* @brief  Copy file BmpName1 to BmpName2 
* @param  BmpName1: the source file name
* @param  BmpName2: the destination file name
* @retval err: Error status (0=> success, 1=> fail)
*/
uint32_t Storage_CopyFile(const char* BmpName1, const char* BmpName2)
{
  uint32_t index = 0;
  FIL file1, file2;
  
  /* Open an Existent BMP file system */
  f_open(&file1, BmpName1, FA_READ);
  /* Create a new BMP file system */
  f_open(&file2, BmpName2, FA_CREATE_ALWAYS | FA_WRITE);
  
  do 
  {
    f_read(&file1, aBuffer, _MAX_SS, &BytesRead);
    f_write(&file2, aBuffer, _MAX_SS, &BytesWritten);  
    index+= _MAX_SS;
    
  } while(index < file1.fsize);
  
  f_close(&file1);
  f_close(&file2);
  
  return 1;
}

/**
* @brief  Opens a file and copies its content to a buffer.
* @param  DirName: the Directory name to open
* @param  FileName: the file name to open
* @param  BufferAddress: A pointer to a buffer to copy the file to
* @param  FileLen: File length
* @retval err: Error status (0=> success, 1=> fail)
*/
uint32_t Storage_CheckBitmapFile(const char* BmpName, uint32_t *FileLen)
{
    uint32_t err = 0;
    if(f_open(&MyFile, BmpName, FA_READ) != FR_OK){
        err = 2;
    }
   f_close(&MyFile); 
  return err;
}

/**
* @brief  List up to 25 file on the root directory with extension .BMP
* @param  DirName: Directory name
* @param  Files: Buffer to contain read files
* @retval The number of the found files
*/
uint32_t Storage_GetDirectoryBitmapFiles(const char* DirName, char* Files[])
{
	uint32_t i = 0, j = 0;
	FRESULT res;

	res = f_opendir(&MyDirectory, DirName);
	if(res == FR_OK){
		i = strlen(DirName);
		for (;;){
			res = f_readdir(&MyDirectory, &MyFileInfo);
			if(res != FR_OK || MyFileInfo.fname[0] == 0) break;
			if(MyFileInfo.fname[0] == '.') continue;
			if(!(MyFileInfo.fattrib & AM_DIR)){
				do{
					i++;
				}while (MyFileInfo.fname[i] != 0x2E);
				if(j < MAX_BMP_FILES){
					if((MyFileInfo.fname[i + 1] == 'B') && (MyFileInfo.fname[i + 2] == 'M') && (MyFileInfo.fname[i + 3] == 'P')){	
						sprintf(Files[j], "%-11.11s", MyFileInfo.fname);
						j++;
					}
				}
				i = 0;
			}
		}
	}
	return j;
}

/**
  * @brief  Compares two buffers.
  * @param  pBuffer1, pBuffer2: buffers to be compared
  * @param  BufferLength: buffer's length
  * @retval  0: pBuffer1 identical to pBuffer2
  *          1: pBuffer1 differs from pBuffer2
  */
uint8_t Buffercmp(uint8_t* pBuffer1, uint8_t* pBuffer2, uint16_t BufferLength)
{
  uint8_t ret = 1;
  while (BufferLength--)
  {
    if(*pBuffer1 != *pBuffer2)
    {
      ret = 0;
    }
    
    pBuffer1++;
    pBuffer2++;
  }
  
  return ret;
}

/**
* @}
*/

/**
* @}
*/

/**
* @}
*/

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
