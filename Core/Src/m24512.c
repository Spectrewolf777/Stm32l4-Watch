#include "m24512.h"
#include "i2c_dev.h"
#include <string.h>

#define WRITE_CYCLE_TIMEOUT_MS  10U
#define SETTINGS_ADDR           0x0000
#define SETTINGS_MAGIC          0x57A7C402UL   /* v2 */
#define SETTINGS_MAGIC_V1       0x57A7C401UL

static i2c_dev_t s_dev;

bool M24512_Init(void)
{
    static const uint8_t addr[] = { 0x50, 0x51, 0x53, 0x54, 0x55, 0x56, 0x57 };
    return I2CDev_Find(&s_dev, addr, sizeof(addr));
}

bool    M24512_Present(void) { return s_dev.bus != NULL; }
uint8_t M24512_Address(void) { return s_dev.addr; }

static bool wait_ready(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < WRITE_CYCLE_TIMEOUT_MS) {
        if (HAL_I2C_IsDeviceReady(s_dev.bus, (uint16_t)(s_dev.addr << 1), 1, 2) == HAL_OK)
            return true;
    }
    return false;
}

bool M24512_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len)
{
    if (!s_dev.bus || (uint32_t)mem_addr + len > M24512_SIZE) return false;
    return HAL_I2C_Mem_Read(s_dev.bus, (uint16_t)(s_dev.addr << 1), mem_addr,
                            I2C_MEMADD_SIZE_16BIT, buf, len, 100) == HAL_OK;
}

bool M24512_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len)
{
    if (!s_dev.bus || (uint32_t)mem_addr + len > M24512_SIZE) return false;

    while (len) {
        uint16_t room  = M24512_PAGE_SIZE - (mem_addr % M24512_PAGE_SIZE);
        uint16_t chunk = len < room ? len : room;

        if (HAL_I2C_Mem_Write(s_dev.bus, (uint16_t)(s_dev.addr << 1), mem_addr,
                              I2C_MEMADD_SIZE_16BIT, (uint8_t *)buf, chunk, 100) != HAL_OK)
            return false;
        if (!wait_ready()) return false;     /* ~5 ms internal write cycle */

        mem_addr += chunk;
        buf      += chunk;
        len      -= chunk;
    }
    return true;
}

/* ---- Settings ----------------------------------------------------------- */
typedef struct {
    uint32_t         magic;
    watch_settings_t s;
    uint16_t         crc;
} settings_blob_t;

static watch_settings_t s_settings;

static uint16_t crc16(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void set_defaults(void)
{
    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.mag_scale[0] = s_settings.mag_scale[1] = s_settings.mag_scale[2] = 1000;
    s_settings.sea_level_pa     = 101325;
    s_settings.screen_timeout_s = 20;
    s_settings.haptics_on       = 1;
}

/* v1 layout, so settings saved by older firmware still load */
typedef struct {
    int16_t  mag_offset[3];
    int16_t  mag_scale[3];
    uint32_t sea_level_pa;
    uint16_t screen_timeout_s;
    uint8_t  haptics_on;
} settings_v1_t;
typedef struct { uint32_t magic; settings_v1_t s; uint16_t crc; } settings_blob_v1_t;

static bool load_v1(void)
{
    settings_blob_v1_t b;
    if (!M24512_Read(SETTINGS_ADDR, (uint8_t *)&b, sizeof(b))) return false;
    if (b.magic != SETTINGS_MAGIC_V1) return false;
    if (b.crc != crc16((const uint8_t *)&b.s, sizeof(b.s))) return false;
    memcpy(s_settings.mag_offset, b.s.mag_offset, sizeof(b.s.mag_offset));
    memcpy(s_settings.mag_scale,  b.s.mag_scale,  sizeof(b.s.mag_scale));
    s_settings.sea_level_pa     = b.s.sea_level_pa;
    s_settings.screen_timeout_s = b.s.screen_timeout_s;
    s_settings.haptics_on       = b.s.haptics_on;
    return true;
}

void Settings_Load(void)
{
    settings_blob_t b;
    set_defaults();
    if (!M24512_Read(SETTINGS_ADDR, (uint8_t *)&b, sizeof(b))) return;
    if (b.magic != SETTINGS_MAGIC) {
        if (load_v1()) {                      /* upgrade to v2 in place */
            if (s_settings.screen_timeout_s == 10) s_settings.screen_timeout_s = 20;
            Settings_Save();
        }
        if (s_settings.screen_timeout_s < 3) s_settings.screen_timeout_s = 3;
        return;
    }
    if (b.crc != crc16((const uint8_t *)&b.s, sizeof(b.s))) return;
    s_settings = b.s;
    if (s_settings.screen_timeout_s < 3) s_settings.screen_timeout_s = 3;
    if (s_settings.screen_timeout_s == 10) {      /* old default -> new 20 s */
        s_settings.screen_timeout_s = 20;
        Settings_Save();
    }
}

bool Settings_Save(void)
{
    settings_blob_t b;
    b.magic = SETTINGS_MAGIC;
    b.s     = s_settings;
    b.crc   = crc16((const uint8_t *)&b.s, sizeof(b.s));
    return M24512_Write(SETTINGS_ADDR, (const uint8_t *)&b, sizeof(b));
}

watch_settings_t *Settings_Get(void) { return &s_settings; }
