#include "lis3mdl.h"
#include "i2c_dev.h"
#include "m24512.h"
#include "lsm6dsv16x_reg.h"
#include <math.h>
#include <stdio.h>

/*
 * On this board the LIS3MDL hangs off the LSM6DSV16X's auxiliary I2C master
 * (sensor hub), not the MCU's I2C bus. The IMU reads the magnetometer for
 * us and the MCU just reads the result from the IMU's SENSOR_HUB registers.
 * A direct-on-MCU-bus fallback is kept in case a future board wires it that way.
 */

#define REG_WHO_AM_I  0x0F
#define REG_CTRL1     0x20
#define REG_CTRL2     0x21
#define REG_CTRL3     0x22
#define REG_CTRL4     0x23
#define REG_CTRL5     0x24
#define REG_OUT_X_L   0x28
#define AUTO_INC      0x80   /* MSB of sub-address enables auto-increment */

#define WHO_AM_I_VAL  0x3D
#define HUB_TIMEOUT_MS 100U

extern I2C_HandleTypeDef hi2c1;          /* LSM6DSV16X bus */

static i2c_dev_t    s_dev;               /* direct mode   */
static bool         s_via_hub;
static bool         s_present;
static uint8_t      s_addr;              /* 7-bit LIS3MDL address */
static stmdev_ctx_t s_imu;


/* ---- LSM6DSV16X access (same bus/address as the step counter) ----------- */
static int32_t imu_write(void *h, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write((I2C_HandleTypeDef *)h, LSM6DSV16X_I2C_ADD_L, reg,
                             I2C_MEMADD_SIZE_8BIT, (uint8_t *)buf, len, 50) == HAL_OK ? 0 : -1;
}

static int32_t imu_read(void *h, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read((I2C_HandleTypeDef *)h, LSM6DSV16X_I2C_ADD_L, reg,
                            I2C_MEMADD_SIZE_8BIT, buf, len, 50) == HAL_OK ? 0 : -1;
}

static void imu_delay(uint32_t ms) { HAL_Delay(ms); }

/* Wait for the hub to finish a cycle. want_write: wait for WR_ONCE_DONE. */
static bool hub_wait(bool want_write, bool *nack)
{
    lsm6dsv16x_status_master_t st;
    uint32_t t0 = HAL_GetTick();
    *nack = false;
    do {
        HAL_Delay(5);
        if (lsm6dsv16x_sh_status_get(&s_imu, &st) != 0) return false;
        if (st.slave0_nack) { *nack = true; return false; }
        if (want_write ? st.wr_once_done : st.sens_hub_endop) return true;
    } while (HAL_GetTick() - t0 < HUB_TIMEOUT_MS);
    return false;
}

static void hub_clear_status(void)
{
    lsm6dsv16x_status_master_t st;
    lsm6dsv16x_sh_status_get(&s_imu, &st);
}

/* One register read from the external sensor through the hub */
static bool hub_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
    lsm6dsv16x_sh_cfg_read_t cfg = { .slv_add = (uint8_t)(addr7 << 1), .slv_subadd = reg, .slv_len = len };
    bool nack, ok;

    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_DISABLE);
    if (lsm6dsv16x_sh_slv_cfg_read(&s_imu, 0, &cfg) != 0) return false;
    lsm6dsv16x_sh_slave_connected_set(&s_imu, LSM6DSV16X_SLV_0);
    hub_clear_status();
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_ENABLE);
    ok = hub_wait(false, &nack);
    if (ok) ok = lsm6dsv16x_sh_read_data_raw_get(&s_imu, buf, len) == 0;
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_DISABLE);
    return ok;
}

/* One register write to the external sensor through the hub */
static bool hub_write(uint8_t addr7, uint8_t reg, uint8_t val)
{
    lsm6dsv16x_sh_cfg_write_t cfg = { .slv0_add = (uint8_t)(addr7 << 1), .slv0_subadd = reg, .slv0_data = val };
    bool nack, ok;

    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_DISABLE);
    lsm6dsv16x_sh_write_mode_set(&s_imu, LSM6DSV16X_ONLY_FIRST_CYCLE);
    if (lsm6dsv16x_sh_cfg_write(&s_imu, &cfg) != 0) return false;
    lsm6dsv16x_sh_slave_connected_set(&s_imu, LSM6DSV16X_SLV_0);
    hub_clear_status();
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_ENABLE);
    ok = hub_wait(true, &nack);
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_DISABLE);
    return ok;
}

static const char *s_fail = "not started";

static bool mag_read(uint8_t reg, uint8_t *val)
{
    return s_via_hub ? hub_read(s_addr, reg, val, 1) : I2CDev_ReadByte(&s_dev, reg, val);
}

/* Write and confirm by reading back (the hub's write-done flag is not
 * reliable right after the LIS3MDL reboots, so don't trust it alone). */
static bool mag_write(uint8_t reg, uint8_t val)
{
    uint8_t rb = 0;
    for (int tries = 0; tries < 4; tries++) {
        if (s_via_hub) hub_write(s_addr, reg, val);
        else           I2CDev_WriteByte(&s_dev, reg, val);
        if (mag_read(reg, &rb) && rb == val) return true;
        HAL_Delay(10);
    }
    return false;
}

static bool configure_mag(void)
{
    /* Soft reset (self-clearing bits, so no read-back), then let it reboot */
    if (s_via_hub) hub_write(s_addr, REG_CTRL2, 0x0C);
    else           I2CDev_WriteByte(&s_dev, REG_CTRL2, 0x0C);
    HAL_Delay(20);

    if (!mag_write(REG_CTRL1, 0x38)) { s_fail = "CTRL1 write failed"; return false; } /* XY med-perf, 40 Hz */
    if (!mag_write(REG_CTRL2, 0x00)) { s_fail = "CTRL2 write failed"; return false; } /* +-4 gauss         */
    if (!mag_write(REG_CTRL4, 0x04)) { s_fail = "CTRL4 write failed"; return false; } /* Z med-perf        */
    if (!mag_write(REG_CTRL5, 0x40)) { s_fail = "CTRL5 write failed"; return false; } /* block data update */
    if (!mag_write(REG_CTRL3, 0x00)) { s_fail = "CTRL3 write failed"; return false; } /* continuous        */
    return true;
}

static bool init_via_hub(void)
{
    static const uint8_t addrs[] = { 0x1C, 0x1E };
    uint8_t whoami = 0, id = 0;

    s_imu.write_reg = imu_write;
    s_imu.read_reg  = imu_read;
    s_imu.mdelay    = imu_delay;
    s_imu.handle    = &hi2c1;

    if (lsm6dsv16x_device_id_get(&s_imu, &whoami) != 0 || whoami != LSM6DSV16X_ID) {
        s_fail = "IMU not answering";
        return false;
    }

    /* A previous run (e.g. before a debugger reset) may have left the hub
     * running; stop it before reconfiguring */
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_DISABLE);
    HAL_Delay(10);

    /* Accelerometer must be running (step counter sets 120 Hz): the hub
     * runs on accelerometer data-ready. */
    lsm6dsv16x_sh_pass_through_set(&s_imu, PROPERTY_DISABLE);
    lsm6dsv16x_sh_master_interface_pull_up_set(&s_imu, PROPERTY_ENABLE);  /* SDx/SCx pull-ups */
    lsm6dsv16x_sh_data_rate_set(&s_imu, LSM6DSV16X_SH_30Hz);

    s_addr = 0;
    for (int tries = 0; tries < 3 && !s_addr; tries++) {
        for (unsigned i = 0; i < sizeof(addrs); i++) {
            if (hub_read(addrs[i], REG_WHO_AM_I, &id, 1) && id == WHO_AM_I_VAL) {
                s_addr = addrs[i];
                break;
            }
        }
        if (!s_addr) HAL_Delay(20);
    }
    if (!s_addr) { s_fail = "no LIS3MDL reply via hub (0x1C/0x1E)"; return false; }

    s_via_hub = true;
    if (!configure_mag()) return false;

    /* Leave the hub reading OUT_X_L..OUT_Z_H (6 bytes) every cycle */
    lsm6dsv16x_sh_cfg_read_t cfg = { .slv_add = (uint8_t)(s_addr << 1),
                                     .slv_subadd = REG_OUT_X_L | AUTO_INC, .slv_len = 6 };
    if (lsm6dsv16x_sh_slv_cfg_read(&s_imu, 0, &cfg) != 0) { s_fail = "hub read setup failed"; return false; }
    lsm6dsv16x_sh_slave_connected_set(&s_imu, LSM6DSV16X_SLV_0);
    lsm6dsv16x_sh_master_set(&s_imu, PROPERTY_ENABLE);
    return true;
}

static bool init_direct(void)
{
    static const uint8_t addr[] = { 0x1C, 0x1E };
    uint8_t id = 0;

    if (!I2CDev_Find(&s_dev, addr, sizeof(addr))) return false;
    if (!I2CDev_ReadByte(&s_dev, REG_WHO_AM_I, &id) || id != WHO_AM_I_VAL) {
        s_dev.bus = NULL;
        return false;
    }
    s_addr = s_dev.addr;
    s_via_hub = false;
    return configure_mag();
}

bool LIS3MDL_Init(void)
{
    const char *hub_fail;
    s_present = init_via_hub();
    if (!s_present) {
        hub_fail = s_fail;
        s_present = init_direct();
        if (!s_present) s_fail = hub_fail;   /* report the hub reason */
    }
    if (s_present) s_fail = "ok";
    return s_present;
}

bool LIS3MDL_Present(void) { return s_present; }

const char *LIS3MDL_Info(void)
{
    static char buf[64];
    if (!s_present) { snprintf(buf, sizeof buf, "not found: %s", s_fail); return buf; }
    snprintf(buf, sizeof buf, "OK at 0x%02X %s", s_addr, s_via_hub ? "(via IMU sensor hub)" : "(direct I2C)");
    return buf;
}

bool LIS3MDL_ReadRaw(int16_t xyz[3])
{
    uint8_t b[6];
    if (!s_present) return false;
    if (s_via_hub) {
        if (lsm6dsv16x_sh_read_data_raw_get(&s_imu, b, 6) != 0) return false;
    } else {
        if (!I2CDev_Read(&s_dev, REG_OUT_X_L | AUTO_INC, b, 6)) return false;
    }
    for (int i = 0; i < 3; i++) xyz[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
    return true;
}

/* ========================================================================
 * Heading
 *
 * Two modes:
 *  - Tilt compensated (after a calibration that included tilting/flipping):
 *    uses the IMU accelerometer for "down", so the heading stays right when
 *    the wrist isn't level. Set North is a single tap.
 *  - Flat 2D (calibration was a flat spin only): only right when held level;
 *    Set North needs a second "turn to east" tap to learn turning direction.
 * ======================================================================== */

static bool    s_cal;
static int16_t s_min[3], s_max[3];

/* accel/mag pairs collected while calibrating, used to find how the IMU is
 * rotated relative to the magnetometer on the PCB */
#define CAL_SAMPLES 96
static int16_t s_cal_acc[CAL_SAMPLES][3], s_cal_mag[CAL_SAMPLES][3];
static uint8_t s_cal_n, s_cal_tick;

static bool read_accel(float a[3])
{
    int16_t r[3];
    if (!s_via_hub && s_imu.handle == NULL) return false;
    if (lsm6dsv16x_acceleration_raw_get(&s_imu, r) != 0) return false;
    a[0] = r[0]; a[1] = r[1]; a[2] = r[2];
    return (a[0] != 0 || a[1] != 0 || a[2] != 0);
}

/* 8 candidate IMU->magnetometer axis maps: 4 rotations about Z, each with or
 * without a 180-degree flip about X (chips on opposite PCB sides). */
static void map_accel(uint8_t k, const float in[3], float out[3])
{
    float x = in[0], y = in[1], z = in[2], t;
    if (k & 4) { y = -y; z = -z; }
    for (uint8_t r = 0; r < (k & 3); r++) { t = x; x = -y; y = t; }
    out[0] = x; out[1] = y; out[2] = z;
}

static void mag_corrected(const int16_t r[3], float m[3])
{
    watch_settings_t *cfg = Settings_Get();
    for (int i = 0; i < 3; i++)
        m[i] = (float)(r[i] - cfg->mag_offset[i]) * (float)cfg->mag_scale[i] / 1000.0f;
}

static float wrap360(float d)
{
    while (d < 0)       d += 360.0f;
    while (d >= 360.0f) d -= 360.0f;
    return d;
}

static float wrap180(float d)
{
    d = wrap360(d);
    return d > 180.0f ? d - 360.0f : d;
}

static bool tilt_mode(void) { return Settings_Get()->compass_tilt_map != 0; }

static bool fusion_heading(float *deg);

/* Raw heading (before Set North) in degrees, mode dependent */
static bool raw_heading(float *deg)
{
    int16_t r[3];
    float m[3];
    if (fusion_heading(deg)) return true;      /* gyro fusion running: best */
    if (!LIS3MDL_ReadRaw(r)) return false;
    mag_corrected(r, m);

    if (tilt_mode()) {
        float a[3], up[3], d[3], e[3], n[3];
        if (!read_accel(a)) return false;
        map_accel((uint8_t)(Settings_Get()->compass_tilt_map - 1), a, up);
        d[0] = -up[0]; d[1] = -up[1]; d[2] = -up[2];          /* down */
        /* east = down x mag, north = east x down */
        e[0] = d[1] * m[2] - d[2] * m[1];
        e[1] = d[2] * m[0] - d[0] * m[2];
        e[2] = d[0] * m[1] - d[1] * m[0];
        n[0] = e[1] * d[2] - e[2] * d[1];
        n[1] = e[2] * d[0] - e[0] * d[2];
        n[2] = e[0] * d[1] - e[1] * d[0];
        /* heading of the magnetometer X axis (lies in the PCB plane);
         * Set North turns this into "12 o'clock" */
        float en = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        float nn = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (en < 1e-3f || nn < 1e-3f) return false;
        *deg = wrap360(atan2f(e[0] / en, n[0] / nn) * 57.29578f);
        return true;
    }

    if (fabsf(m[0]) + fabsf(m[1]) < 1.0f) return false;
    *deg = wrap360(atan2f(m[1], m[0]) * 57.29578f);
    return true;
}

/* Averaged raw heading for Set North: ~0.6 s of readings */
static bool raw_heading_avg(float *deg)
{
    float sx = 0, sy = 0, h;
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (raw_heading(&h)) { sx += cosf(h * 0.0174533f); sy += sinf(h * 0.0174533f); n++; }
        uint32_t t0 = HAL_GetTick();        /* keep the gyro fusion running */
        while (HAL_GetTick() - t0 < 70U) Compass_FusionTask();
    }
    if (n < 4) return false;
    *deg = wrap360(atan2f(sy, sx) * 57.29578f);
    return true;
}

/* Smoothed heading so the needle doesn't jitter */
static float s_fx, s_fy;
static bool  s_fvalid;

int16_t Compass_Heading(void)
{
    watch_settings_t *cfg = Settings_Get();
    float raw, h;

    if (!raw_heading(&raw)) return -1;

    if (cfg->compass_dir != 0)
        h = wrap360((float)cfg->compass_dir * (raw - (float)cfg->compass_north_x10 / 10.0f));
    else
        h = wrap360(raw + (float)COMPASS_OFFSET_DEG);   /* not aligned yet */

    float x = cosf(h * 0.0174533f), y = sinf(h * 0.0174533f);
    if (Compass_FusionActive()) {               /* gyro already smooth + fast */
        s_fx = x; s_fy = y; s_fvalid = true;
        return (int16_t)h;
    }
    if (!s_fvalid) { s_fx = x; s_fy = y; s_fvalid = true; }
    s_fx += 0.35f * (x - s_fx);
    s_fy += 0.35f * (y - s_fy);
    return (int16_t)wrap360(atan2f(s_fy, s_fx) * 57.29578f);
}

bool Compass_IsAligned(void)   { return Settings_Get()->compass_dir != 0; }
bool Compass_TiltCompOn(void)  { return tilt_mode(); }

/* ========================================================================
 * Gyro + accelerometer + magnetometer fusion (Mahony AHRS)
 *
 * The gyro tracks fast turns instantly but drifts; the accelerometer (down)
 * and magnetometer (north) slowly pull it back. The accelerometer is ignored
 * while the arm is accelerating (|a| far from 1 g). All vectors are put in
 * the magnetometer's frame using the axis map found during calibration.
 * The gyro only runs while the compass page is showing.
 * ======================================================================== */
#define FUS_PERIOD_US      8000U        /* ~120 Hz, matches gyro ODR      */
#define FUS_MAG_PERIOD_MS  33U          /* hub refreshes the mag at 30 Hz */
#define FUS_TWO_KP_FAST    6.0f         /* first 2 s: converge quickly    */
#define FUS_TWO_KP         0.8f         /* then mostly trust the gyro     */
#define FUS_TWO_KI         0.05f        /* learns gyro bias               */
#define GYRO_RAD_PER_LSB   (0.035f * 0.0174533f)   /* 1000 dps range */
#define ACC_G_PER_LSB      0.000061f               /* 2 g range      */

static bool     s_fus_on;
static uint32_t s_fus_start_ms, s_fus_last_cyc, s_fus_mag_ms, s_fus_updates;
static float    q0 = 1, q1, q2, q3, s_ibx, s_iby, s_ibz;
static float    s_fus_m[3];
static bool     s_fus_m_ok;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void Compass_FusionReset(void)
{
    q0 = 1; q1 = q2 = q3 = 0;
    s_ibx = s_iby = s_ibz = 0;
    s_fus_updates = 0;
    s_fus_m_ok = false;
    s_fus_start_ms = HAL_GetTick();
    s_fus_last_cyc = DWT->CYCCNT;
}

void Compass_FusionEnable(bool on)
{
    if (on == s_fus_on) return;
    if (on && (!s_present || !s_via_hub || !tilt_mode())) return;   /* needs axis map */

    if (on) {
        lsm6dsv16x_gy_full_scale_set(&s_imu, LSM6DSV16X_1000dps);
        lsm6dsv16x_gy_data_rate_set(&s_imu, LSM6DSV16X_ODR_AT_120Hz);
        dwt_init();
        Compass_FusionReset();
    } else {
        lsm6dsv16x_gy_data_rate_set(&s_imu, LSM6DSV16X_ODR_OFF);   /* save power */
    }
    s_fus_on = on;
}

bool Compass_FusionActive(void)
{
    /* Heading is trusted once the filter has had ~1 s to converge */
    return s_fus_on && s_fus_updates > 120;
}

static void mahony_update(float gx, float gy, float gz, const float a_in[3],
                          const float m_in[3], float dt, bool use_acc, bool use_mag)
{
    float ax = a_in[0], ay = a_in[1], az = a_in[2];
    float mx = m_in[0], my = m_in[1], mz = m_in[2];
    float n, ex = 0, ey = 0, ez = 0;
    float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
    float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
    float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

    if (use_acc) {
        n = sqrtf(ax * ax + ay * ay + az * az);
        ax /= n; ay /= n; az /= n;
        /* estimated "up" in sensor frame */
        float vx = q1q3 - q0q2, vy = q0q1 + q2q3, vz = q0q0 - 0.5f + q3q3;
        ex += ay * vz - az * vy;
        ey += az * vx - ax * vz;
        ez += ax * vy - ay * vx;
    }
    if (use_mag) {
        n = sqrtf(mx * mx + my * my + mz * mz);
        mx /= n; my /= n; mz /= n;
        /* earth-frame field (horizontal bx, vertical bz) */
        float hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
        float hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
        float bx = sqrtf(hx * hx + hy * hy);
        float bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));
        /* estimated field direction in sensor frame */
        float wx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
        float wy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
        float wz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);
        ex += my * wz - mz * wy;
        ey += mz * wx - mx * wz;
        ez += mx * wy - my * wx;
    }

    bool  settling = (HAL_GetTick() - s_fus_start_ms) < 2000U;
    float two_kp   = settling ? FUS_TWO_KP_FAST : FUS_TWO_KP;
    if (!settling) {
        s_ibx += FUS_TWO_KI * ex * dt;
        s_iby += FUS_TWO_KI * ey * dt;
        s_ibz += FUS_TWO_KI * ez * dt;
    }
    gx += two_kp * ex + s_ibx;
    gy += two_kp * ey + s_iby;
    gz += two_kp * ez + s_ibz;

    gx *= 0.5f * dt; gy *= 0.5f * dt; gz *= 0.5f * dt;
    float qa = q0, qb = q1, qc = q2;
    q0 += -qb * gx - qc * gy - q3 * gz;
    q1 +=  qa * gx + qc * gz - q3 * gy;
    q2 +=  qa * gy - qb * gz + q3 * gx;
    q3 +=  qa * gz + qb * gy - qc * gx;
    n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 /= n; q1 /= n; q2 /= n; q3 /= n;
}

/* Call every main-loop pass; does nothing unless the compass page is open */
void Compass_FusionTask(void)
{
    uint8_t b[12];
    int16_t raw[3];
    float g[3], a[3], gm[3], am[3];

    if (!s_fus_on) return;

    uint32_t now = DWT->CYCCNT;
    uint32_t us  = (now - s_fus_last_cyc) / (SystemCoreClock / 1000000U);
    if (us < FUS_PERIOD_US) return;
    s_fus_last_cyc = now;
    float dt = (float)us * 1e-6f;
    if (dt > 0.1f) dt = 0.1f;            /* main loop stalled (flash write etc) */

    /* gyro (0x22..0x27) + accel (0x28..0x2D) in one burst */
    if (s_imu.read_reg(s_imu.handle, LSM6DSV16X_OUTX_L_G, b, 12) != 0) return;
    for (int i = 0; i < 3; i++) {
        g[i] = (float)(int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) * GYRO_RAD_PER_LSB;
        a[i] = (float)(int16_t)(b[6 + 2 * i] | (b[7 + 2 * i] << 8));
    }

    if (HAL_GetTick() - s_fus_mag_ms >= FUS_MAG_PERIOD_MS) {
        s_fus_mag_ms = HAL_GetTick();
        if (LIS3MDL_ReadRaw(raw)) { mag_corrected(raw, s_fus_m); s_fus_m_ok = true; }
    }

    uint8_t k = (uint8_t)(Settings_Get()->compass_tilt_map - 1);
    map_accel(k, g, gm);                  /* gyro shares the accel's axes */
    map_accel(k, a, am);

    float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) * ACC_G_PER_LSB;
    bool use_acc = an > 0.85f && an < 1.15f;          /* ignore arm swings */
    mahony_update(gm[0], gm[1], gm[2], am, s_fus_m, dt, use_acc, s_fus_m_ok);
    s_fus_updates++;
}

/* Heading of the magnetometer X axis from the fused orientation.
 * Earth frame is North-West-Up, so East = -West. */
static bool fusion_heading(float *deg)
{
    if (!Compass_FusionActive()) return false;
    float n = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
    float w = 2.0f * (q1 * q2 + q0 * q3);
    *deg = wrap360(atan2f(-w, n) * 57.29578f);
    return true;
}

/* ---- Set North ----------------------------------------------------------- */
static uint8_t s_align_step;     /* 0 idle, 1 waiting for east (flat mode) */
static float   s_align_north;

bool Compass_AlignActive(void) { return s_align_step != 0; }
bool Compass_AlignNeedsEast(void) { return !tilt_mode(); }

bool Compass_AlignNorth(void)
{
    watch_settings_t *cfg = Settings_Get();
    if (!raw_heading_avg(&s_align_north)) { s_align_step = 0; return false; }

    if (tilt_mode()) {
        /* Tilt-compensated heading already turns the right way: one tap */
        cfg->compass_dir       = 1;
        cfg->compass_north_x10 = (int16_t)(s_align_north * 10.0f);
        s_align_step = 0;
        s_fvalid = false;
        Settings_Save();
        return true;
    }
    s_align_step = 1;
    return true;
}

compass_align_result_t Compass_AlignEast(void)
{
    float east, d;
    watch_settings_t *cfg = Settings_Get();

    if (s_align_step != 1) return ALIGN_FAILED;
    s_align_step = 0;
    if (!raw_heading_avg(&east)) return ALIGN_FAILED;

    d = wrap180(east - s_align_north);
    if (fabsf(d) < 45.0f || fabsf(d) > 135.0f) return ALIGN_NOT_90;

    cfg->compass_dir       = (d > 0) ? 1 : -1;   /* east must read +90 */
    cfg->compass_north_x10 = (int16_t)(s_align_north * 10.0f);
    s_fvalid = false;
    Settings_Save();
    return ALIGN_OK;
}

void Compass_AlignCancel(void) { s_align_step = 0; }

/* ---- Calibration -------------------------------------------------------- */

/* Earth's field is ~0.25-0.65 gauss; at +-4 G (6842 LSB/G) turning an axis
 * through the field swings it by roughly 3000-9000 LSB. */
#define CAL_MIN_RANGE  2000

void Compass_CalStart(void)
{
    for (int i = 0; i < 3; i++) { s_min[i] = INT16_MAX; s_max[i] = INT16_MIN; }
    s_cal_n = 0;
    s_cal_tick = 0;
    s_cal = true;
}

bool Compass_CalRunning(void) { return s_cal; }

static int32_t axis_range(int i)
{
    return (s_max[i] > s_min[i]) ? (int32_t)s_max[i] - s_min[i] : 0;
}

void Compass_Service(void)
{
    int16_t r[3];
    float a[3];
    if (!s_cal || !LIS3MDL_ReadRaw(r)) return;
    for (int i = 0; i < 3; i++) {
        if (r[i] < s_min[i]) s_min[i] = r[i];
        if (r[i] > s_max[i]) s_max[i] = r[i];
    }

    /* Every ~200 ms keep an accel/mag pair if the watch's tilt has changed
     * noticeably since the last kept one (we want varied orientations). */
    if (++s_cal_tick < 4 || s_cal_n >= CAL_SAMPLES || !read_accel(a)) return;
    s_cal_tick = 0;
    if (s_cal_n > 0) {
        const int16_t *p = s_cal_acc[s_cal_n - 1];
        float dot = a[0] * p[0] + a[1] * p[1] + a[2] * p[2];
        float na  = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        float np  = sqrtf((float)p[0] * p[0] + (float)p[1] * p[1] + (float)p[2] * p[2]);
        if (na < 1 || np < 1 || dot / (na * np) > 0.985f) return;   /* < ~10 deg change */
    }
    for (int i = 0; i < 3; i++) { s_cal_acc[s_cal_n][i] = (int16_t)a[i]; s_cal_mag[s_cal_n][i] = r[i]; }
    s_cal_n++;
}

uint8_t Compass_CalProgress(void)       /* flat spin: X and Y */
{
    if (!s_cal) return 0;
    int32_t w = axis_range(0) < axis_range(1) ? axis_range(0) : axis_range(1);
    w = w * 100 / CAL_MIN_RANGE;
    return (uint8_t)(w > 100 ? 100 : w);
}

uint8_t Compass_CalTiltProgress(void)   /* flipping/tilting: Z + varied poses */
{
    if (!s_cal) return 0;
    int32_t z = axis_range(2) * 100 / CAL_MIN_RANGE;
    int32_t s = (int32_t)s_cal_n * 100 / 30;
    int32_t w = z < s ? z : s;
    return (uint8_t)(w > 100 ? 100 : w);
}

/* Find the IMU->mag axis map: with the right one, the angle between gravity
 * and the magnetic field is the same in every pose. Returns 1..8 or 0. */
static uint8_t find_tilt_map(void)
{
    float best = 1e9f, second = 1e9f;
    uint8_t best_k = 0;

    if (s_cal_n < 20) return 0;
    for (uint8_t k = 0; k < (COMPASS_SAME_PCB_SIDE ? 4 : 8); k++) {
        float sum = 0, sum2 = 0;
        for (uint8_t i = 0; i < s_cal_n; i++) {
            float a[3] = { s_cal_acc[i][0], s_cal_acc[i][1], s_cal_acc[i][2] }, u[3], m[3];
            map_accel(k, a, u);
            mag_corrected(s_cal_mag[i], m);
            float nu = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            float nm = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            float d  = (u[0] * m[0] + u[1] * m[1] + u[2] * m[2]) / (nu * nm);
            sum += d; sum2 += d * d;
        }
        float var = sum2 / s_cal_n - (sum / s_cal_n) * (sum / s_cal_n);
        if (var < best) { second = best; best = var; best_k = k; }
        else if (var < second) second = var;
    }
    /* Must be clearly better than the runner-up, and consistent */
    if (best > 0.02f || best * 3.0f > second) return 0;
    return (uint8_t)(best_k + 1);
}

compass_cal_result_t Compass_CalStop(bool save)
{
    watch_settings_t *cfg = Settings_Get();
    int32_t range[3], avg;

    if (!s_cal) return CAL_FAILED;
    s_cal = false;
    if (!save) return CAL_FAILED;

    for (int i = 0; i < 3; i++) range[i] = axis_range(i);
    if (range[0] < CAL_MIN_RANGE || range[1] < CAL_MIN_RANGE) return CAL_FAILED;
    bool full3d = range[2] >= CAL_MIN_RANGE;

    avg = full3d ? (range[0] + range[1] + range[2]) / 3 : (range[0] + range[1]) / 2;
    for (int i = 0; i < 3; i++) {
        if (i == 2 && !full3d) { cfg->mag_offset[2] = 0; cfg->mag_scale[2] = 1000; continue; }
        cfg->mag_offset[i] = (int16_t)(((int32_t)s_max[i] + s_min[i]) / 2);
        cfg->mag_scale[i]  = (int16_t)((avg * 1000) / range[i]);
    }

    /* New offsets change the raw heading, so north must be set again */
    cfg->compass_tilt_map = full3d ? find_tilt_map() : 0;
    cfg->compass_dir      = 0;
    Compass_FusionReset();
    s_fvalid = false;
    Settings_Save();
    return cfg->compass_tilt_map ? CAL_OK_TILT : CAL_OK_FLAT;
}

/* Diagnostics for the "-mag" serial command */
void Compass_PrintDebug(void)
{
    watch_settings_t *cfg = Settings_Get();
    int16_t r[3] = {0};
    float a[3] = {0}, raw = -1;
    bool mok = LIS3MDL_ReadRaw(r), aok = read_accel(a), hok = raw_heading(&raw);

    printf("\r\n[MAG] %s\r\n", LIS3MDL_Info());
    printf("[MAG] raw mag %s: %d %d %d   offsets %d %d %d\r\n", mok ? "ok" : "FAIL",
           r[0], r[1], r[2], cfg->mag_offset[0], cfg->mag_offset[1], cfg->mag_offset[2]);
    printf("[MAG] accel %s: %d %d %d\r\n", aok ? "ok" : "FAIL", (int)a[0], (int)a[1], (int)a[2]);
    printf("[MAG] mode=%s map=%u  north set=%s dir=%d north=%d.%d  raw=%d  heading=%d\r\n",
           Compass_FusionActive() ? "gyro-fusion" : tilt_mode() ? "tilt-comp" : "flat",
           cfg->compass_tilt_map,
           cfg->compass_dir ? "yes" : "no", cfg->compass_dir,
           cfg->compass_north_x10 / 10, cfg->compass_north_x10 % 10,
           hok ? (int)raw : -1, Compass_Heading());
    fflush(stdout);
}
