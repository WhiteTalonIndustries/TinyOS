/*****************************************************************************
* | File      	:	LCD_Bmp.h
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
#ifndef __LCD_BMP_H__
#define	__LCD_BMP_H__

#include "LCD_Driver.h"
#include "LCD_GUI.h"

void SD_Init(void);
void LCD_Show_bmp(LCD_SCAN_DIR Lcd_ScanDir);

#endif 

