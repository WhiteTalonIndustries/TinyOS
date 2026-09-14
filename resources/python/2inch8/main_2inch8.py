from machine import Pin,SPI,PWM
import framebuf
import time
import os

PICO_RP2040 = True
PICO_RP2350 = not PICO_RP2040

LCD_DC   = 8
LCD_CS   = 9
LCD_SCK  = 10
LCD_MOSI = 11
LCD_MISO = 12
LCD_BL   = 13
LCD_RST  = 15
TP_CS    = 16
TP_IRQ   = 17

class LCD_2inch8(framebuf.FrameBuffer):

    def __init__(self):
        self.RED   =   0x07E0
        self.GREEN =   0x001f
        self.BLUE  =   0xf800
        self.WHITE =   0xffff
        self.BLACK =   0x0000
        
        self.width = 320
        self.height = 240
            
        self.cs = Pin(LCD_CS,Pin.OUT)
        self.rst = Pin(LCD_RST,Pin.OUT)
        self.dc = Pin(LCD_DC,Pin.OUT)
        
        self.tp_cs =Pin(TP_CS,Pin.OUT)
        self.irq = Pin(TP_IRQ,Pin.IN)
        
        self.cs(1)
        self.dc(1)
        self.rst(1)
        self.tp_cs(1)
        self.spi = SPI(1,60_000_000,sck=Pin(LCD_SCK),mosi=Pin(LCD_MOSI),miso=Pin(LCD_MISO))
              
        self.buffer = bytearray(self.height * self.width * 2)
        super().__init__(self.buffer, self.width, self.height, framebuf.RGB565)
        self.init_display()

        
    def write_cmd(self, cmd):
        self.cs(1)
        self.dc(0)
        self.cs(0)
        self.spi.write(bytearray([cmd]))
        self.cs(1)

    def write_data(self, buf):
        self.cs(1)
        self.dc(1)
        self.cs(0)
        #self.spi.write(bytearray([0X00]))
        self.spi.write(bytearray([buf]))
        self.cs(1)


    def init_display(self):
        """Initialize dispaly"""  
        self.rst(1)
        time.sleep_ms(5)
        self.rst(0)
        time.sleep_ms(10)
        self.rst(1)
        time.sleep_ms(5)
        
        self.write_cmd(0x11)
        time.sleep_ms(100)
        
        self.write_cmd(0x36)
        self.write_data(0x60)
        
        self.write_cmd(0x3a)
        self.write_data(0x55)
        self.write_cmd(0xb2)
        self.write_data(0x0c)
        self.write_data(0x0c)
        self.write_data(0x00)
        self.write_data(0x33)
        self.write_data(0x33)
        self.write_cmd(0xb7)
        self.write_data(0x35)
        self.write_cmd(0xbb)
        self.write_data(0x28)
        self.write_cmd(0xc0)
        self.write_data(0x3c)
        self.write_cmd(0xc2)
        self.write_data(0x01)
        self.write_cmd(0xc3)
        self.write_data(0x0b)
        self.write_cmd(0xc4)
        self.write_data(0x20)
        self.write_cmd(0xc6)
        self.write_data(0x0f)
        self.write_cmd(0xD0)
        self.write_data(0xa4)
        self.write_data(0xa1)
        self.write_cmd(0xe0)
        self.write_data(0xd0)
        self.write_data(0x01)
        self.write_data(0x08)
        self.write_data(0x0f)
        self.write_data(0x11)
        self.write_data(0x2a)
        self.write_data(0x36)
        self.write_data(0x55)
        self.write_data(0x44)
        self.write_data(0x3a)
        self.write_data(0x0b)
        self.write_data(0x06)
        self.write_data(0x11)
        self.write_data(0x20)
        self.write_cmd(0xe1)
        self.write_data(0xd0)
        self.write_data(0x02)
        self.write_data(0x07)
        self.write_data(0x0a)
        self.write_data(0x0b)
        self.write_data(0x18)
        self.write_data(0x34)
        self.write_data(0x43)
        self.write_data(0x4a)
        self.write_data(0x2b)
        self.write_data(0x1b)
        self.write_data(0x1c)
        self.write_data(0x22)
        self.write_data(0x1f)
        self.write_cmd(0x55)
        self.write_data(0xB0)
        self.write_cmd(0x29)

    def show_up(self):

        self.write_cmd(0x2A)
        self.write_data(0x00)
        self.write_data(0x00)
        self.write_data(0x01)
        self.write_data(0x3f)
         
        self.write_cmd(0x2B)
        self.write_data(0x00)
        self.write_data(0x00)
        self.write_data(0x00)
        self.write_data(0xef)
                
        self.write_cmd(0x2C)
        
        self.cs(1)
        self.dc(1)
        self.cs(0)
        self.spi.write(self.buffer)
        self.cs(1)
        
    def bl_ctrl(self,duty):
        pwm = PWM(Pin(LCD_BL))
        pwm.freq(1000)
        if(duty>=100):
            pwm.duty_u16(65535)
        else:
            pwm.duty_u16(655*duty)

    def touch_get(self): 
        if self.irq() == 0:
            self.spi = SPI(1,5_000_000,sck=Pin(LCD_SCK),mosi=Pin(LCD_MOSI),miso=Pin(LCD_MISO))
            self.tp_cs(0)
            X_Point = 0
            Y_Point = 0
            for i in range(0,3):
                self.spi.write(bytearray([0XD0]))
                Read_date = self.spi.read(2)
                time.sleep_us(10)
                X_Point=X_Point+(((Read_date[0]<<8)+Read_date[1])>>3)
                
                self.spi.write(bytearray([0X90]))
                Read_date = self.spi.read(2)
                Y_Point=Y_Point+(((Read_date[0]<<8)+Read_date[1])>>3)

            X_Point=X_Point/3
            Y_Point=Y_Point/3
            
            self.tp_cs(1) 
            self.spi = SPI(1,30_000_000,sck=Pin(LCD_SCK),mosi=Pin(LCD_MOSI),miso=Pin(LCD_MISO))
            Result_list = [X_Point,Y_Point]
            #print(Result_list)
            return(Result_list)
        
if __name__=='__main__':

    LCD = LCD_2inch8()
    LCD.bl_ctrl(100)
    LCD.fill(LCD.BLACK)
    LCD.show_up()
    #color BRG     
    while True:
        LCD.fill(LCD.WHITE)
        LCD.fill_rect(60,5,200,30,LCD.RED)
        LCD.text("Raspberry Pi Pico",90,17,LCD.WHITE)
        display_color = 0x001F
        LCD.text("2.8' IPS LCD TEST",90,57,LCD.BLACK)
        for i in range(0,12):      
            LCD.fill_rect(i*20+30,100,30,50,(display_color))
            display_color = display_color << 1
        get = LCD.touch_get()
        if get != None:
            if PICO_RP2040:
                X_Point = 320 - int((get[1]-430)*320/3270)
                Y_Point = int((get[0]-430)*240/3270)
            elif PICO_RP2350:
                X_Point = 320-int((get[1]-4500)*320/3360)
                Y_Point = int((get[0]-4480)*240/3500)
            if(X_Point>320):
                X_Point = 320
            elif X_Point<0:
                X_Point = 0
            if(Y_Point>240):
                Y_Point = 240
            elif Y_Point<0:
                Y_Point = 0
#             print(X_Point,Y_Point)
            if(Y_Point>180):
                if(X_Point<85):
                    LCD.fill_rect(15,180,65,50,LCD.RED)
                    LCD.text("Button0",20,200,LCD.WHITE)
                    
                elif(X_Point<165):
                    LCD.fill_rect(90,180,65,50,LCD.RED)
                    LCD.text("Button1",95,200,LCD.WHITE)

                elif(X_Point<240):
                    LCD.fill_rect(170,180,65,50,LCD.RED)
                    LCD.text("Button2",175,200,LCD.WHITE)          
                else:               
                    LCD.fill_rect(245,180,65,50,LCD.RED)
                    LCD.text("Button3",250,200,LCD.WHITE)
            else :
               LCD.text("Button0",20,200,LCD.BLACK)
               LCD.text("Button1",95,200,LCD.BLACK)
               LCD.text("Button2",175,200,LCD.BLACK)
               LCD.text("Button3",250,200,LCD.BLACK) 
      
        else :
           LCD.text("Button0",20,200,LCD.BLACK)
           LCD.text("Button1",95,200,LCD.BLACK)
           LCD.text("Button2",175,200,LCD.BLACK)
           LCD.text("Button3",250,200,LCD.BLACK)         
        LCD.show_up()  
        time.sleep(0.1)
               




