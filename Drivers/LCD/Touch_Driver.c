/*****************************************************************************
* | File      	:   Touch_1IN28.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   V1.0
* | Date        :   2022-12-02
* | Info        :   Basic version
*
******************************************************************************/
#include "Touch_Driver.h"

extern Touch_1IN28_XY XY;
/******************************************************************************
function :	read ID 读取ID
parameter:  CST816T : 0xB5
******************************************************************************/
UBYTE Touch_1IN28_WhoAmI()
{

    if (I2C_Read_Byte(0xA7) == 0xB5)
        return true;
    else
        return false;
}

/******************************************************************************
function :	reset touch 复位触摸
parameter:
******************************************************************************/
void Touch_1IN28_Reset()
{
    DEV_Digital_Write(DEV_TP_RST_PIN, 0);
    DEV_Delay_ms(100);
    DEV_Digital_Write(DEV_TP_RST_PIN, 1);
    DEV_Delay_ms(100);
}

/******************************************************************************
function :	Read software version number 读取软件版本号
parameter:
******************************************************************************/
UBYTE Touch_1IN28_Read_Revision()
{
    return I2C_Read_Byte(0xA9);
}

/******************************************************************************
function :	exit sleep mode 退出休眠模式
parameter:
******************************************************************************/
void Touch_1IN28_Stop_Sleep()
{
    I2C_Write_Byte(DisAutoSleep,0x01);
}

/******************************************************************************
function :	Set touch mode 设置触摸模式
parameter:
        mode = 0 gestures mode
        mode = 1 point mode
        mode = 2 mixed mode
******************************************************************************/
void Touch_1IN28_Set_Mode(UBYTE mode)
{
    if (mode == 1)
    {
        I2C_Write_Byte(IrqCtl,0X41);
        I2C_Write_Byte(NorScanPer,0X01);//Normal fast detection cycle unit 10ms
        I2C_Write_Byte(IrqPluseWidth,0x1c); //Interrupt low pulse output width 1.5MS
    }
    else if(mode == 2)
        I2C_Write_Byte(IrqCtl,0X71);
    else
        {
            I2C_Write_Byte(IrqCtl,0X11);
            I2C_Write_Byte(NorScanPer,0X01);
            I2C_Write_Byte(IrqPluseWidth,0x1c);//中断低脉冲输出宽度 1.5MS
            I2C_Write_Byte(MotionMask,EnDClick);//Enable double-tap mode
        }

}

/******************************************************************************
function :	wake up touchscreen 唤醒触摸屏
parameter:
******************************************************************************/
void Touch_1IN28_Wake_up()
{
    DEV_Digital_Write(DEV_TP_RST_PIN, 0);
    DEV_Delay_ms(10);
    DEV_Digital_Write(DEV_TP_RST_PIN, 1);
    DEV_Delay_ms(50);
    I2C_Write_Byte(0xFE,0x01);
}


/******************************************************************************
function :	screen initialization 屏幕初始化
parameter:
******************************************************************************/
UBYTE Touch_1IN28_init(UBYTE mode)
{
    UBYTE bRet,Rev;
    Touch_1IN28_Reset();
		while(1)
				{
					bRet = Touch_1IN28_WhoAmI();

					if (bRet)
					{
							printf("Success:Detected CST816T.\r\n");
							Rev = Touch_1IN28_Read_Revision();
							printf("CST816T Revision = %d\r\n",Rev);
							Touch_1IN28_Stop_Sleep();
						break;
					}
					else
					{
						HAL_Delay(10);
						MX_I2C2_Init();
						printf("Error: Not Detected CST816T.\r\n");
					}
				}
    XY.mode = mode;
    Touch_1IN28_Set_Mode(mode);

    XY.x_point = 0;
    XY.y_point = 0;
    return true;
}

/******************************************************************************
function :	Get the corresponding point coordinates 获取对应的点坐标
parameter:
******************************************************************************/
Touch_1IN28_XY Touch_1IN28_Get_Point(void)
{
    UBYTE data[4];
    I2C_Read_nByte(0x03, data, 4);

    XY.x_point = ((data[0] & 0x0f)<<8) + data[1];
    XY.y_point = ((data[2] & 0x0f)<<8) + data[3];
    
    return XY;
}












