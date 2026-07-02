#include "image.h"
#include "LCD_Test.h"
#include "LCD_1in28.h"
#include "Touch_Driver.h"
#include "DEV_Config.h"
#include <stdlib.h>  // Added for abs() function

extern I2C_HandleTypeDef hi2c2;

volatile Touch_1IN28_XY XY;
volatile UBYTE flag = 0, flgh = 0;
UWORD x, y, l = 0;

// Variables to calculate Software Gestures
uint16_t start_x = 0, start_y = 0;
uint16_t last_x = 0, last_y = 0;
uint8_t is_touching = 0;
uint32_t touch_time = 0;

/*
 * Upgraded Read_Touch: Calculates Gestures manually!
 */
void Read_Touch(void) {
    uint8_t data[6];

    if (HAL_I2C_Mem_Read(&hi2c2, 0x2A, 0x01, I2C_MEMADD_SIZE_8BIT, data, 6, 100) == HAL_OK) {
        uint8_t points = data[1];
        uint16_t curr_x = ((data[2] & 0x0F) << 8) | data[3];
        uint16_t curr_y = ((data[4] & 0x0F) << 8) | data[5];

        XY.Gesture = 0x00; // Default to no gesture

        if (points == 1) {
            if (is_touching == 0) {
                // Finger just touched down
                start_x = curr_x;
                start_y = curr_y;
                is_touching = 1;
                touch_time = HAL_GetTick(); // Record start time
            }
            // Keep updating the current position while dragging
            last_x = curr_x;
            last_y = curr_y;
            XY.x_point = curr_x;
            XY.y_point = curr_y;
        }
        else if (points == 0 && is_touching == 1) {
            // Finger just lifted! Time to calculate the gesture.
            is_touching = 0;

            int16_t dx = last_x - start_x;
            int16_t dy = last_y - start_y;
            uint32_t duration = HAL_GetTick() - touch_time;

            // If the swipe distance is more than 30 pixels
            if (abs(dx) > 30 || abs(dy) > 30) {
                if (abs(dx) > abs(dy)) {
                    // Horizontal Swipe
                    XY.Gesture = (dx > 0) ? RIGHT : LEFT;
                } else {
                    // Vertical Swipe
                    XY.Gesture = (dy > 0) ? Down : UP;
                }
            } else {
                // If the finger didn't move much, it's a tap or press
                if (duration > 800) {
                    XY.Gesture = LONG_PRESS; // Held for 800ms
                } else {
                    XY.Gesture = DOUBLE_CLICK; // Fast tap
                }
            }
        }
    }
}

void CST816S_Force_Awake_And_Enable_Gestures(void) {
    uint8_t val;
    val = 0xFF; // Disable Sleep
    HAL_I2C_Mem_Write(&hi2c2, 0x2A, 0xFE, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);

    val = 0x71; // Enable all interrupts so we can track the X/Y drag
    HAL_I2C_Mem_Write(&hi2c2, 0x2A, 0xFA, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);

    val = 0x07; // Enable Motion Mask
    HAL_I2C_Mem_Write(&hi2c2, 0x2A, 0xEC, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    DEV_Delay_ms(50);
}


void LCD_1in28_test()
{
	printf("LCD_1IN28_test Demo\r\n");
	DEV_Module_Init();
  
	printf("LCD_1IN28_ Init and Clear...\r\n");
	LCD_1IN28_SetBackLight(1000);
	LCD_1IN28_Init(VERTICAL);

	XY.mode = 0;
	if(Touch_1IN28_init(XY.mode) == true)
        printf("Init OK!\r\n");
    else
        printf("Init NO!\r\n");

    CST816S_Force_Awake_And_Enable_Gestures();

	printf("Paint_NewImage\r\n");
	Paint_NewImage(LCD_1IN28_WIDTH,LCD_1IN28_HEIGHT, 0, BLACK);
	
	Paint_SetClearFuntion(LCD_1IN28_Clear);
	Paint_SetDisplayFuntion(LCD_1IN28_DrawPaint);
	Paint_Clear(WHITE);
		
	Paint_DrawString_EN(35, 90, "Gesture test", &Font20, BLACK, WHITE);
	Paint_DrawString_EN(10, 120, "Complete as prompted", &Font16, BLACK, WHITE);
	DEV_Delay_ms(500);

	/* ================= GESTURE TESTING ================= */
	
	Paint_ClearWindows(10,90,230,137,WHITE);  
	Paint_DrawString_EN(105, 100, "Up", &Font24, 0X647C, WHITE);
	printf("gesture about to start. Swipe UP and lift finger!\r\n");

	while(XY.Gesture != UP)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	Paint_ClearWindows(105,100,160,155,WHITE);  
	Paint_DrawString_EN(85, 100, "Down", &Font24, 0X647C, WHITE);
	while(XY.Gesture != Down)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	Paint_ClearWindows(85, 100,160,155,WHITE);
	Paint_DrawString_EN(85, 100, "Left", &Font24, 0X647C, WHITE);
	while(XY.Gesture != LEFT)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	Paint_ClearWindows(85, 100,160,155,WHITE);
	Paint_DrawString_EN(80, 100, "Right", &Font24, 0X647C, WHITE);
	while(XY.Gesture != RIGHT)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	Paint_ClearWindows(80, 100,165,160,WHITE);
	Paint_DrawString_EN(47, 100, "Long Press", &Font20, 0X647C, WHITE);
	while(XY.Gesture != LONG_PRESS)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	Paint_ClearWindows(47, 100,200,155,WHITE);
	Paint_DrawString_EN(35, 100, "Double Click", &Font20, 0X647C, WHITE);
	while(XY.Gesture != DOUBLE_CLICK)
	{
		if (flag == TOUCH_IRQ)
		{
			Read_Touch();
            if (XY.Gesture != 0x00) {
			    printf("Interrupt Fired! Gesture calculated: 0x%02X\r\n", XY.Gesture);
            }
			flag = TOUCH_DRAW;
		}
		DEV_Delay_ms(10);
	}

	/* ================= DRAWING MODE ================= */

	XY.mode = 1;
	Touch_1IN28_init(XY.mode);
	
	Paint_ClearWindows(10,90,230,137,WHITE);  
	Paint_DrawRectangle(0, 0, 35, 208,RED,DOT_PIXEL_1X1,DRAW_FILL_FULL);
	Paint_DrawRectangle(0, 0, 208, 35,GREEN,DOT_PIXEL_1X1,DRAW_FILL_FULL);
	Paint_DrawRectangle(206, 0, 240, 240,BLUE,DOT_PIXEL_1X1,DRAW_FILL_FULL);
	Paint_DrawRectangle(0, 206, 240, 240,GRAY,DOT_PIXEL_1X1,DRAW_FILL_FULL);

	while (1)
	{
		if (flag == TOUCH_IRQ)
		{
			if(flgh == TOUCH_INIT && XY.x_point != TOUCH_INIT)
			{
				flgh = 1;
				x = XY.x_point;
				y = XY.y_point;
			}
			if ((XY.x_point > 35 && XY.x_point < 205) && (XY.y_point > 35 && XY.y_point < 208))
			{
				flgh = TOUCH_DRAW;
			}
			else
			{
				if ((XY.x_point > 0 && XY.x_point < 33) && (XY.y_point > 0 && XY.y_point < 208))
						XY.color = RED;

				if ((XY.x_point > 0 && XY.x_point < 208) && (XY.y_point > 0 && XY.y_point < 33))
						XY.color = GREEN;

				if ((XY.x_point > 208 && XY.x_point < 240) && (XY.y_point > 0 && XY.y_point < 240))
						XY.color = BLUE;

				if ((XY.x_point > 0 && XY.x_point < 240) && (XY.y_point > 208 && XY.y_point < 240))
						Paint_ClearWindows(35,35,205,205,WHITE);

				flgh = TOUCH_NO_DRAW;
				flag = TOUCH_DRAW;
			}

			if (flgh == TOUCH_DRAW)
			{
				XY.x_point = (XY.x_point > 37)? XY.x_point : 37;
				XY.y_point = (XY.y_point > 37)? XY.y_point : 37;

				XY.x_point = (XY.x_point < 205)? XY.x_point : 205;
				XY.y_point = (XY.y_point < 203)? XY.y_point : 203;

				if (l<48000)
				{
					flag = TOUCH_DRAW;
					Paint_DrawLine(x,y,XY.x_point, XY.y_point, XY.color, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
					l=0;
				}
				else
				{
					flag = TOUCH_DRAW;
					Paint_DrawPoint(XY.x_point, XY.y_point, XY.color, DOT_PIXEL_2X2, DOT_FILL_AROUND);
					l = 0;
				}
				x = XY.x_point;
				y = XY.y_point;
			}
		}
		l++;
		if (l>50000)
		{
			l=49000;
		}
    }
}
/*
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if(GPIO_Pin == TP_INT_Pin)
	{
		if(XY.mode == 1)
		{
			flag = TOUCH_IRQ;
			XY = Touch_1IN28_Get_Point();
		}
		else
		{
			flag = TOUCH_IRQ;
		}
	}
}
*/