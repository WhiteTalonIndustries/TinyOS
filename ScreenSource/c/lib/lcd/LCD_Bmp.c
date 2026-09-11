/*****************************************************************************
* | File      	:	LCD_Bmp.c
* | Author      :   Waveshare team
* | Function    :	Show SDcard BMP picto LCD 
* | Info        :
*   Image scanning
*      Please use progressive scanning to generate images or fonts
*----------------
* |	This version:   V1.0
* | Date        :   2018-01-11
* | Info        :   Basic version
*
******************************************************************************/
#include "LCD_Bmp.h"
#include "DEV_Config.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "ff.h"
#include "fatfs_storage.h"

char* pDirectoryFiles[ MAX_BMP_FILES ];
uint8_t str[20];
FRESULT f_res;
FATFS microSDFatFs;
unsigned char file_name[25][11];

void SD_Init(void){
	
	DEV_Digital_Write(SD_CS_PIN,1);
	DEV_Digital_Write(LCD_CS_PIN,1);
	DEV_Digital_Write(TP_CS_PIN,1);

	int counter = 0;
    //Check the mounted device
	f_res = f_mount(&microSDFatFs,(TCHAR const*)"/",1);
	if(f_res!=FR_OK){
		printf("SD card mount file system failed ,error code :(%d)\r\n",f_res);
//		GUI_DisString_EN(0,20,"FATFS_NOT_MOUNTED",&Font16,LCD_BACKGROUND,RED);
//		while(1);
	}else{
		printf("SD card mount file system success!! \r\n");
		for (counter = 0; counter < MAX_BMP_FILES; counter++){
			pDirectoryFiles[counter] = file_name[counter]; 
		}	
	}
}

/********************************************************************************
function:	Display the BMP picture in the SD card
parameter: 
		Bmp_ScanDir :   Displays the LCD scanning mode of the BMP picture
		Lcd_ScanDir :   LCD normal display scan
********************************************************************************/
void LCD_Show_bmp(LCD_SCAN_DIR Lcd_ScanDir){
	uint32_t bmplen = 0x00;
    uint32_t checkstatus = 0x00;
    uint32_t filesnumbers = 0x00;
    uint32_t bmpcounter = 0x00;
    DIR directory;
    FRESULT res;
	LCD_SCAN_DIR Bmp_ScanDir = Lcd_ScanDir;
	// L2R_U2D  = 0,	//0°
    // D2U_L2R  ,      //90°
    // R2L_D2U  ,      //180°
    // U2D_R2L  ,      //270°  
	if (Lcd_ScanDir == L2R_U2D)
	{
		Bmp_ScanDir = R2L_D2U;
	}
	else if (Lcd_ScanDir == R2L_D2U)
	{
		Bmp_ScanDir = L2R_U2D;
	}
	else if (Lcd_ScanDir == D2U_L2R)
	{
		Bmp_ScanDir = U2D_R2L;
	}
	else
	{
		Bmp_ScanDir = D2U_L2R;
	}
	
    /* Open directory */
	LCD_Clear(LCD_BACKGROUND);
    res = f_opendir(&directory, "/");
    if((res != FR_OK)){
		if(res == FR_NO_FILESYSTEM){
			/* Display message: SD card not FAT formated */
			GUI_DisString_EN(0, 32, "SD_CARD_NOT_FORMATTED", &Font24,LCD_BACKGROUND,BLUE);  
		}else{
			/* Display message: Fail to open directory */
			GUI_DisString_EN(0, 48, "SD_CARD_OPEN_FAIL", &Font24,LCD_BACKGROUND,BLUE);           
		}
    }else{
		//printf("file open \r\n");
	}
	
    /* Get number of bitmap files */
    filesnumbers = Storage_GetDirectoryBitmapFiles ("/", pDirectoryFiles);   
	
    /* Set bitmap counter to display first image */
    bmpcounter = 1;

	int showtime;
    for(showtime = 0; showtime < filesnumbers ; showtime++){
        sprintf((char*)str, "%-11.11s", pDirectoryFiles[bmpcounter -1]);
        
        checkstatus = Storage_CheckBitmapFile((const char*)str, &bmplen);
        
        if(checkstatus == 0){
			//Display the scan of the picture
			BMP_SetGramScanWay(Bmp_ScanDir);
			/* Open the image and display the picture */
			Storage_OpenReadFile(0, 0, (const char*)str);
        }else if (checkstatus == 1){
			/* Display message: SD card does not exist */
			//Restore the default scan
			LCD_SetGramScanWay(Lcd_ScanDir);
			GUI_DisString_EN(0, 64, "SD_CARD_NOT_FOUND", &Font24,LCD_BACKGROUND,BLUE);
        }else {
			/* Display message: File not supported */
			//Restore the default scan
			LCD_SetGramScanWay(Lcd_ScanDir);
            GUI_DisString_EN(0, 80, "SD_CARD_FILE_NOT_SUPPORTED", &Font24,LCD_BACKGROUND,BLUE);
        }

        bmpcounter ++;
        if(bmpcounter > filesnumbers){
			bmpcounter = 1;
			break;
        }

	}
	// LCD_Clear(LCD_BACKGROUND);
	//Restore the default scan
	DEV_Digital_Write(SD_CS_PIN,1);	
}
