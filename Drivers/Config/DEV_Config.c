/*****************************************************************************
* | File      	:   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master
*                and enhance portability
*----------------
* |	This version:   V1.0
* | Date        :   2018-11-22
* | Info        :

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "DEV_Config.h"
#include "i2c.h"
//#include "tim.h"

uint8_t I2C_ADDR;
/********************************************************************************
function:	Delay function
note:
	Driver_Delay_ms(xms) : Delay x ms
********************************************************************************/
void DEV_delay_ms(uint16_t xms )
{
	HAL_Delay(xms);
}


void DEV_SPI_WRite(UBYTE _dat)
{
	HAL_SPI_Transmit(&hspi2, (uint8_t *)&_dat, 1, 500);
}


/******************************************************************************
function:	I2C Function initialization and transfer
parameter:
Info:
******************************************************************************/
void DEV_I2C_Init(uint8_t Add)
{
	I2C_ADDR =  Add;
}

void I2C_Write_Byte(uint8_t Cmd, uint8_t value)
{

	UBYTE Buf[1] = {0};
	Buf[0] = value;
	HAL_I2C_Mem_Write(&hi2c2, I2C_ADDR, Cmd, I2C_MEMADD_SIZE_8BIT, Buf, 1, 0x20);

}

int I2C_Read_Byte(uint8_t Cmd)
{

	UBYTE Buf[1]={0};
	HAL_I2C_Mem_Read(&hi2c2, I2C_ADDR+1, Cmd, I2C_MEMADD_SIZE_8BIT, Buf, 1, 0x20);
	return Buf[0];

}

int I2C_Read_Word(uint8_t Cmd)
{

	UBYTE Buf[2]={0, 0};
	HAL_I2C_Mem_Read(&hi2c2, I2C_ADDR+1, Cmd, I2C_MEMADD_SIZE_8BIT, Buf, 2, 0x20);
	return ((Buf[1] << 8) | (Buf[0] & 0xff));

}

void I2C_Read_nByte(UBYTE Cmd,UBYTE *Buf,UBYTE num)
{
	HAL_I2C_Mem_Read(&hi2c2, I2C_ADDR+1, Cmd, I2C_MEMADD_SIZE_8BIT, Buf, num, 0x20);

}

int DEV_Module_Init(void)
{
    DEV_Digital_Write(DEV_DC_PIN, 1);
    DEV_Digital_Write(DEV_CS_PIN, 1);
    DEV_Digital_Write(DEV_RST_PIN, 1);
		DEV_Digital_Write(DEV_INT_PIN, 1);
    DEV_Digital_Write(DEV_TP_RST_PIN, 1);
    //HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3);

		DEV_I2C_Init(0x15 << 1);
		return 0;
}

void DEV_Module_Exit(void)
{
    DEV_Digital_Write(DEV_DC_PIN, 0);
    DEV_Digital_Write(DEV_CS_PIN, 0);
    //close
    DEV_Digital_Write(DEV_RST_PIN, 0);
		DEV_Digital_Write(DEV_INT_PIN, 0);
    DEV_Digital_Write(DEV_TP_RST_PIN, 0);
    //HAL_TIM_PWM_Stop(&htim5,TIM_CHANNEL_3);
}
