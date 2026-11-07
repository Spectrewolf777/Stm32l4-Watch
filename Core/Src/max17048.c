#include "max17048.h"
#include "i2c_dev.h"

#define REG_VCELL    0x02   /* 78.125 uV / LSB          */
#define REG_SOC      0x04   /* 1/256 % / LSB            */
#define REG_VERSION  0x08
#define REG_HIBRT    0x0A
#define REG_CONFIG   0x0C   /* [15:8] RCOMP, [7] SLEEP, [6] ALSC, [5] ALRT, [4:0] ATHD */
#define REG_CRATE    0x16   /* 0.208 %/hr / LSB, signed */
#define REG_STATUS   0x1A   /* [14] EnVR, [13:8] SC HD VR VL VH RI */

#define CONFIG_ALRT  0x0020
#define STATUS_ENVR  0x4000

static i2c_dev_t s_dev;
static uint8_t   s_version;

static bool write16(uint8_t reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return I2CDev_Write(&s_dev, reg, b, 2);
}

static bool read16(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (!I2CDev_Read(&s_dev, reg, b, 2)) return false;
    *v = (uint16_t)((b[0] << 8) | b[1]);   /* big-endian */
    return true;
}

bool MAX17048_Init(void)
{
    static const uint8_t addr[] = { 0x36 };
    uint16_t ver;

    if (!I2CDev_Find(&s_dev, addr, sizeof(addr))) return false;
    if (!read16(REG_VERSION, &ver)) { s_dev.bus = NULL; return false; }
    s_version = (uint8_t)ver;

    /* Default HIBRT (0x8030) lets the gauge auto-hibernate when the battery
     * is idle, which is what we want on a watch.
     * Low-battery alert on ALRT pin (PA6): ATHD = 32 - percent. */
    uint16_t cfg;
    if (read16(REG_CONFIG, &cfg)) {
        cfg = (uint16_t)((cfg & 0xFF00) | (32U - MAX17048_LOW_BAT_PERCENT));  /* also clears ALRT */
        write16(REG_CONFIG, cfg);
    }
    write16(REG_STATUS, 0);   /* clear reset-indicator etc. */
    return true;
}

bool MAX17048_Present(void) { return s_dev.bus != NULL; }
uint8_t MAX17048_Version(void) { return s_version; }

bool MAX17048_Read(max17048_data_t *out)
{
    uint16_t vcell, soc, crate;

    if (!read16(REG_VCELL, &vcell) || !read16(REG_SOC, &soc)) return false;

    out->mv = (uint16_t)(((uint32_t)vcell * 78125UL) / 1000000UL);
    uint16_t pct = soc >> 8;
    out->percent = pct > 100 ? 100 : (uint8_t)pct;

    if (read16(REG_CRATE, &crate)) {
        /* 0.208 %/hr per LSB -> x10 = *2.08 */
        out->rate_x10 = (int16_t)(((int32_t)(int16_t)crate * 208) / 100);
    } else {
        out->rate_x10 = 0;
    }
    out->charging = out->rate_x10 > 5;   /* > +0.5 %/hr */
    return true;
}

uint8_t MAX17048_ServiceAlert(void)
{
    uint16_t status = 0, cfg = 0;
    if (!s_dev.bus || !read16(REG_STATUS, &status)) return 0;

    write16(REG_STATUS, status & STATUS_ENVR);           /* clear flags        */
    if (read16(REG_CONFIG, &cfg))
        write16(REG_CONFIG, cfg & (uint16_t)~CONFIG_ALRT); /* release ALRT pin  */
    return (uint8_t)((status >> 8) & 0x3F);
}
