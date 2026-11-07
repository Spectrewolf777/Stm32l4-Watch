#include "bmp388.h"
#include "i2c_dev.h"
#include <math.h>

#define REG_CHIP_ID   0x00
#define REG_DATA      0x04   /* press xlsb..msb, temp xlsb..msb */
#define REG_PWR_CTRL  0x1B
#define REG_OSR       0x1C
#define REG_ODR       0x1D
#define REG_CONFIG    0x1F
#define REG_CALIB     0x31   /* 21 bytes */
#define REG_CMD       0x7E

#define CHIP_ID       0x50

static i2c_dev_t s_dev;
static uint32_t  s_sea_level_pa = 101325;

/* Calibration coefficients, already scaled (datasheet section 9.1) */
static double t1, t2, t3;
static double p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;

static bool load_calibration(void)
{
    uint8_t c[21];
    if (!I2CDev_Read(&s_dev, REG_CALIB, c, sizeof(c))) return false;

#define U16(i) ((uint16_t)(c[i] | (c[(i) + 1] << 8)))
#define S16(i) ((int16_t)U16(i))
#define S8(i)  ((int8_t)c[i])
    t1  = (double)U16(0)  * 256.0;                       /* / 2^-8  */
    t2  = (double)U16(2)  / 1073741824.0;                /* / 2^30  */
    t3  = (double)S8(4)   / 281474976710656.0;           /* / 2^48  */
    p1  = ((double)S16(5) - 16384.0) / 1048576.0;        /* / 2^20  */
    p2  = ((double)S16(7) - 16384.0) / 536870912.0;      /* / 2^29  */
    p3  = (double)S8(9)   / 4294967296.0;                /* / 2^32  */
    p4  = (double)S8(10)  / 137438953472.0;              /* / 2^37  */
    p5  = (double)U16(11) * 8.0;                         /* / 2^-3  */
    p6  = (double)U16(13) / 64.0;                        /* / 2^6   */
    p7  = (double)S8(15)  / 256.0;                       /* / 2^8   */
    p8  = (double)S8(16)  / 32768.0;                     /* / 2^15  */
    p9  = (double)S16(17) / 281474976710656.0;           /* / 2^48  */
    p10 = (double)S8(19)  / 281474976710656.0;           /* / 2^48  */
    p11 = (double)S8(20)  / 36893488147419103232.0;      /* / 2^65  */
#undef U16
#undef S16
#undef S8
    return true;
}

bool BMP388_Init(void)
{
    static const uint8_t addr[] = { 0x76, 0x77 };
    uint8_t id = 0;

    if (!I2CDev_Find(&s_dev, addr, sizeof(addr))) return false;
    if (!I2CDev_ReadByte(&s_dev, REG_CHIP_ID, &id) || id != CHIP_ID) {
        s_dev.bus = NULL;
        return false;
    }

    I2CDev_WriteByte(&s_dev, REG_CMD, 0xB6);         /* soft reset */
    HAL_Delay(5);

    if (!load_calibration()) { s_dev.bus = NULL; return false; }

    I2CDev_WriteByte(&s_dev, REG_OSR, 0x03);         /* osr_p x8, osr_t x1 */
    I2CDev_WriteByte(&s_dev, REG_ODR, 0x07);         /* 200/2^7 = 1.56 Hz  */
    I2CDev_WriteByte(&s_dev, REG_CONFIG, 0x04);      /* IIR coef 3         */
    I2CDev_WriteByte(&s_dev, REG_PWR_CTRL, 0x33);    /* press+temp, normal */
    return true;
}

bool BMP388_Present(void) { return s_dev.bus != NULL; }

bool BMP388_Read(uint32_t *pressure_pa, int16_t *temp_x10)
{
    uint8_t d[6];
    if (!I2CDev_Read(&s_dev, REG_DATA, d, 6)) return false;

    double up = (double)((uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16));
    double ut = (double)((uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16));
    if (up == 0 || ut == 0) return false;            /* no conversion yet */

    /* Temperature */
    double pd1 = ut - t1;
    double pd2 = pd1 * t2;
    double t   = pd2 + pd1 * pd1 * t3;

    /* Pressure */
    double o1 = p5 + p6 * t + p7 * t * t + p8 * t * t * t;
    double o2 = up * (p1 + p2 * t + p3 * t * t + p4 * t * t * t);
    double o3 = up * up * (p9 + p10 * t) + up * up * up * p11;
    double p  = o1 + o2 + o3;

    *pressure_pa = (uint32_t)(p + 0.5);
    *temp_x10    = (int16_t)(t * 10.0);
    return true;
}

void BMP388_SetSeaLevel(uint32_t pa) { if (pa > 80000 && pa < 120000) s_sea_level_pa = pa; }

int32_t BMP388_AltitudeM(uint32_t pressure_pa)
{
    float r = (float)pressure_pa / (float)s_sea_level_pa;
    return (int32_t)(44330.0f * (1.0f - powf(r, 0.1903f)));
}
