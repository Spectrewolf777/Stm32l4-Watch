#include "mic.h"
#include "dfsdm.h"

#define FOSR             125U              /* 2 MHz / 125 = 16 kHz        */
#define DMA_HALF         512U              /* samples per DMA half (32 ms) */
#define RING_SAMPLES     16384U            /* 1.02 s buffer, power of two */
#define RING_MASK        (RING_SAMPLES - 1U)
#define STARTUP_DISCARD  (MIC_SAMPLE_RATE / 10U)   /* drop first 100 ms   */

static int32_t  s_dma[2 * DMA_HALF];
static int16_t  s_ring[RING_SAMPLES];
static volatile uint32_t s_wr, s_rd, s_overruns;
static volatile bool     s_running;
static volatile uint16_t s_peak;
static uint32_t s_discard;
static int32_t  s_dc_x, s_dc_y;

static void pdm_clock(bool on)
{
    if (on) DFSDM1_Channel0->CHCFGR1 |=  DFSDM_CHCFGR1_DFSDMEN;
    else    DFSDM1_Channel0->CHCFGR1 &= ~DFSDM_CHCFGR1_DFSDMEN;
}

void Mic_Init(void)
{
    DFSDM_Filter_TypeDef  *flt = hdfsdm1_filter0.Instance;
    DFSDM_Channel_TypeDef *ch  = hdfsdm1_channel0.Instance;

    /* CubeMX generated FastSinc / x1; set Sinc3 / x125 / integrator x1.
     * FLTFCR can only be written with the filter disabled. */
    flt->FLTCR1 &= ~DFSDM_FLTCR1_DFEN;
    flt->FLTFCR  = (3U << DFSDM_FLTFCR_FORD_Pos) |
                   ((FOSR - 1U) << DFSDM_FLTFCR_FOSR_Pos) |
                   (0U << DFSDM_FLTFCR_IOSR_Pos);
    flt->FLTCR1 |= DFSDM_FLTCR1_DFEN;
    hdfsdm1_filter0.Init.FilterParam.SincOrder    = DFSDM_FILTER_SINC3_ORDER;
    hdfsdm1_filter0.Init.FilterParam.Oversampling = FOSR;

    /* Clock edge the mic drives data on (SITP: 00 rising, 01 falling) */
    ch->CHCFGR1 &= ~DFSDM_CHCFGR1_CHEN;
    ch->CHCFGR1  = (ch->CHCFGR1 & ~(3U << DFSDM_CHCFGR1_SITP_Pos)) |
                   ((MIC_DATA_ON_FALLING_EDGE ? 1U : 0U) << DFSDM_CHCFGR1_SITP_Pos);
    ch->CHCFGR1 |= DFSDM_CHCFGR1_CHEN;

    pdm_clock(false);   /* mic off until recording */
}

bool Mic_Start(void)
{
    if (s_running) return true;

    s_wr = s_rd = 0;
    s_overruns = 0;
    s_discard  = STARTUP_DISCARD;
    s_dc_x = s_dc_y = 0;
    s_peak = 0;

    pdm_clock(true);
    s_running = true;
    if (HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0, s_dma, 2 * DMA_HALF) != HAL_OK) {
        s_running = false;
        pdm_clock(false);
        return false;
    }
    return true;
}

void Mic_Stop(void)
{
    if (!s_running) return;
    HAL_DFSDM_FilterRegularStop_DMA(&hdfsdm1_filter0);
    s_running = false;
    pdm_clock(false);
}

bool     Mic_IsRunning(void) { return s_running; }
uint32_t Mic_Available(void) { return s_wr - s_rd; }
uint32_t Mic_Overruns(void)  { return s_overruns; }

uint16_t Mic_Level(void)
{
    uint16_t p = s_peak;
    s_peak = (uint16_t)(p - (p >> 3));   /* decay so the meter falls back */
    return p;
}

uint32_t Mic_Read(int16_t *dst, uint32_t max_samples)
{
    uint32_t n = s_wr - s_rd;
    if (n > max_samples) n = max_samples;
    for (uint32_t i = 0; i < n; i++) dst[i] = s_ring[(s_rd + i) & RING_MASK];
    s_rd += n;
    return n;
}

/* ISR context: convert one DMA half to 16-bit PCM and push to the ring */
static void process(const int32_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = src[i] >> 8;                       /* 24-bit result field */

        /* DC blocker: y = x - x[-1] + 0.995 * y[-1] */
        int32_t y = x - s_dc_x + (int32_t)(((int64_t)s_dc_y * 1019) >> 10);
        s_dc_x = x;
        s_dc_y = y;

        if (s_discard) { s_discard--; continue; }

        int32_t v = y >> MIC_GAIN_SHIFT;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;

        uint16_t a = (uint16_t)(v < 0 ? -v : v);
        if (a > s_peak) s_peak = a;

        if (s_wr - s_rd >= RING_SAMPLES) { s_overruns++; continue; }
        s_ring[s_wr & RING_MASK] = (int16_t)v;
        s_wr++;
    }
}

void HAL_DFSDM_FilterRegConvHalfCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if (hdfsdm_filter == &hdfsdm1_filter0 && s_running) process(&s_dma[0], DMA_HALF);
}

void HAL_DFSDM_FilterRegConvCpltCallback(DFSDM_Filter_HandleTypeDef *hdfsdm_filter)
{
    if (hdfsdm_filter == &hdfsdm1_filter0 && s_running) process(&s_dma[DMA_HALF], DMA_HALF);
}
