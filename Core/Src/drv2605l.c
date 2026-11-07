#include "drv2605l.h"
#include "i2c_dev.h"

#define REG_STATUS     0x00
#define REG_MODE       0x01
#define REG_LIBRARY    0x03
#define REG_WAVESEQ1   0x04
#define REG_WAVESEQ2   0x05
#define REG_GO         0x0C
#define REG_FEEDBACK   0x1A
#define REG_CONTROL3   0x1D

#define MODE_INTERNAL_TRIGGER 0x00
#define MODE_STANDBY          0x40

#define DRV2605_IN_PORT       GPIOB
#define DRV2605_IN_PIN        GPIO_PIN_4

static i2c_dev_t s_dev;
static bool      s_playing;

bool DRV2605L_Init(void)
{
    static const uint8_t addr[] = { 0x5A };
    uint8_t status = 0, fb = 0;
    GPIO_InitTypeDef gpio = {0};

    /* PB4 = DRV2605 IN/TRIG: unused in internal-trigger mode, hold it low.
     * (Set up here because it isn't configured in CubeMX.) */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(DRV2605_IN_PORT, DRV2605_IN_PIN, GPIO_PIN_RESET);
    gpio.Pin   = DRV2605_IN_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DRV2605_IN_PORT, &gpio);

    /* PB5 = EN (100k pull-down on the board): drive high to leave shutdown */
    HAL_GPIO_WritePin(DRV2605_EN_GPIO_Port, DRV2605_EN_Pin, GPIO_PIN_SET);
    HAL_Delay(2);

    if (!I2CDev_Find(&s_dev, addr, sizeof(addr))) {
        HAL_GPIO_WritePin(DRV2605_EN_GPIO_Port, DRV2605_EN_Pin, GPIO_PIN_RESET);
        return false;
    }

    /* Device ID in STATUS[7:5]: 7 = DRV2605L, 3 = DRV2605 */
    if (!I2CDev_ReadByte(&s_dev, REG_STATUS, &status) ||
        ((status >> 5) != 7 && (status >> 5) != 3)) {
        s_dev.bus = NULL;
        return false;
    }

    I2CDev_WriteByte(&s_dev, REG_MODE, MODE_INTERNAL_TRIGGER);  /* leave standby */

    I2CDev_ReadByte(&s_dev, REG_FEEDBACK, &fb);
#if DRV2605L_USE_LRA
    I2CDev_WriteByte(&s_dev, REG_FEEDBACK, fb | 0x80);         /* N_ERM_LRA = LRA */
    I2CDev_WriteByte(&s_dev, REG_LIBRARY, 6);                  /* LRA library     */
#else
    I2CDev_WriteByte(&s_dev, REG_FEEDBACK, fb & 0x7F);         /* ERM             */
    I2CDev_WriteByte(&s_dev, REG_LIBRARY, 1);                  /* ERM library A   */
    {
        uint8_t c3 = 0;
        I2CDev_ReadByte(&s_dev, REG_CONTROL3, &c3);
        I2CDev_WriteByte(&s_dev, REG_CONTROL3, c3 | 0x20);     /* ERM open loop   */
    }
#endif

    I2CDev_WriteByte(&s_dev, REG_MODE, MODE_STANDBY);          /* low power idle  */
    return true;
}

bool DRV2605L_Present(void) { return s_dev.bus != NULL; }

bool DRV2605L_Play(uint8_t effect)
{
    if (!s_dev.bus) return false;
    bool ok = I2CDev_WriteByte(&s_dev, REG_MODE, MODE_INTERNAL_TRIGGER);
    ok &= I2CDev_WriteByte(&s_dev, REG_WAVESEQ1, effect);
    ok &= I2CDev_WriteByte(&s_dev, REG_WAVESEQ2, 0);           /* end of sequence */
    ok &= I2CDev_WriteByte(&s_dev, REG_GO, 1);
    s_playing = ok;
    return ok;
}

bool DRV2605L_PlaySequence(const uint8_t *seq, uint8_t n)
{
    if (!s_dev.bus || n == 0) return false;
    if (n > 8) n = 8;
    bool ok = I2CDev_WriteByte(&s_dev, REG_MODE, MODE_INTERNAL_TRIGGER);
    for (uint8_t i = 0; i < n; i++) ok &= I2CDev_WriteByte(&s_dev, (uint8_t)(REG_WAVESEQ1 + i), seq[i]);
    if (n < 8) ok &= I2CDev_WriteByte(&s_dev, (uint8_t)(REG_WAVESEQ1 + n), 0);   /* end */
    ok &= I2CDev_WriteByte(&s_dev, REG_GO, 1);
    s_playing = ok;
    return ok;
}

void DRV2605L_Service(void)
{
    uint8_t go = 1;
    if (!s_playing) return;
    if (I2CDev_ReadByte(&s_dev, REG_GO, &go) && go == 0) {
        I2CDev_WriteByte(&s_dev, REG_MODE, MODE_STANDBY);   /* back to low power */
        s_playing = false;
    }
}

void DRV2605L_Stop(void)
{
    if (!s_dev.bus) return;
    I2CDev_WriteByte(&s_dev, REG_GO, 0);
    I2CDev_WriteByte(&s_dev, REG_MODE, MODE_STANDBY);
    s_playing = false;
}
