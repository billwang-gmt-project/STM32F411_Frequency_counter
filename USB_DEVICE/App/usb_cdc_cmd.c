/**
 * @file    usb_cdc_cmd.c
 * @brief   SCPI command parser for the frequency counter CDC console.
 *
 * Implements IEEE 488.2 common commands (*IDN?, *SAV, *RST) and a
 * SCPI-style subsystem hierarchy (MEASure, CAPture, SOURce, LED,
 * TRIGger, SYSTem).  Both long and abbreviated keyword forms are
 * accepted, case-insensitive.
 *
 * All register access goes through regmap.h so this module never
 * touches hardware directly.
 */

#include "usb_cdc_cmd.h"
#include "cdc_fifo.h"
#include "regmap.h"
#include "config.h"
#include "main.h"          /* NVIC_SystemReset() via CMSIS core_cm4.h */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Register addresses (mirror of main.c / CLAUDE.md definitions)     */
/* ------------------------------------------------------------------ */
#define REG_PERIOD       0x00
#define REG_FREQ         0x04
#define REG_DUTY         0x08
#define REG_PULSE        0x0C
#define REG_EDGE         0x10
#define REG_TIM_PSC      0x11
#define REG_IC_PSC       0x13
#define REG_CAPTURE_CTRL 0x14

#define REG_LED_PERIOD   0x20
#define REG_LED_DUTY     0x22
#define REG_LED_G_PERIOD 0x23
#define REG_LED_G_DUTY   0x25
#define REG_LED_R_PERIOD 0x26
#define REG_LED_R_DUTY   0x28

#define REG_SAVE_CFG     0x30
#define SAVE_CFG_KEY     0x5A

#define REG_PWM1_FREQ_L  0x40
#define REG_PWM1_FREQ_H  0x42
#define REG_PWM1_DUTY    0x44
#define REG_PWM1_CTRL    0x46

#define REG_PWM2_FREQ_L  0x4B
#define REG_PWM2_FREQ_H  0x4D
#define REG_PWM2_DUTY    0x4F
#define REG_PWM2_CTRL    0x51

#define REG_TRIG_WIDTH   0x56

#define REG_NICKNAME     0x60
#define NICKNAME_MAX_LEN 16

/* ------------------------------------------------------------------ */
/*  Line buffer                                                        */
/* ------------------------------------------------------------------ */
#define LINE_BUF_SIZE  128

static char     s_line[LINE_BUF_SIZE];
static uint16_t s_line_pos;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** Read a little-endian uint32 from a byte buffer. */
static uint32_t read_u32(const uint8_t *buf)
{
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/** Write a little-endian uint16 into a byte buffer. */
static void write_u16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)(val >> 8);
}

/** Skip leading whitespace and return pointer to first non-space. */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/** tolower without ctype */
static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* ------------------------------------------------------------------ */
/*  SCPI keyword matcher                                               */
/* ------------------------------------------------------------------ */

/**
 * Match a SCPI keyword with abbreviation support.
 *
 * @param line     Input string (consumed from this point)
 * @param full     Full keyword in lowercase (e.g. "frequency")
 * @param min_len  Minimum chars for the abbreviated form (e.g. 4 for FREQuency)
 * @return         Pointer past the matched keyword, or NULL on mismatch.
 *
 * Accepts any prefix of `full` that is >= min_len chars, case-insensitive.
 * The character after the match must be a delimiter (: ? space NUL).
 */
static const char *scpi_match_kw(const char *line, const char *full, uint8_t min_len)
{
    uint8_t i = 0;
    while (full[i] && to_lower(line[i]) == full[i])
        i++;

    /* Must have matched at least min_len chars */
    if (i < min_len) return NULL;

    /* If we stopped before the end of `full`, the input must have stopped
     * at a delimiter — otherwise it's a partial non-matching word. */
    char next = line[i];
    if (full[i] != '\0') {
        /* Partial match — next char must be delimiter */
        if (next != ':' && next != '?' && next != ' ' && next != '\0')
            return NULL;
    }

    return &line[i];
}

/** Skip a colon separator. Returns pointer past ':', or NULL if not ':'. */
static const char *skip_colon(const char *p)
{
    return (*p == ':') ? p + 1 : NULL;
}

/** Check if the remaining line is a query ('?' optionally followed by space/NUL). */
static int is_query(const char *p)
{
    return (*p == '?');
}

/** Extract argument after whitespace. Returns pointer to first non-space char. */
static const char *get_arg(const char *p)
{
    return skip_spaces(p);
}

/* ------------------------------------------------------------------ */
/*  SCPI error response                                                */
/* ------------------------------------------------------------------ */

static uint16_t scpi_error(uint8_t *resp, uint16_t max)
{
    return (uint16_t)snprintf((char *)resp, max,
        "-100,\"Command error\"\r\n");
}

/* ------------------------------------------------------------------ */
/*  IEEE 488.2 common commands                                         */
/* ------------------------------------------------------------------ */

static uint16_t cmd_idn(uint8_t *resp, uint16_t max)
{
    uint32_t uid0 = *(uint32_t *)0x1FFF7A10U;
    uint32_t uid1 = *(uint32_t *)0x1FFF7A14U;
    uint32_t uid2 = *(uint32_t *)0x1FFF7A18U;
    uint32_t sn0 = uid0 + uid2;
    uint32_t sn1 = uid1;

    return (uint16_t)snprintf((char *)resp, max,
        "%s,%s,%08lX%08lX,%08lX\r\n",
        CFG_MANUFACTURER, CFG_MODEL,
        (unsigned long)sn0, (unsigned long)sn1,
        (unsigned long)CFG_FW_VERSION);
}

static uint16_t cmd_sav(uint8_t *resp, uint16_t max)
{
    uint8_t key = SAVE_CFG_KEY;
    RegMap_Lock();
    RegMap_Write(REG_SAVE_CFG, &key, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "OK\r\n");
}

static uint16_t cmd_rst(uint8_t *resp, uint16_t max)
{
    (void)resp; (void)max;
    NVIC_SystemReset();
    /* Never reached */
    return 0;
}

/* ------------------------------------------------------------------ */
/*  MEASure subsystem                                                  */
/* ------------------------------------------------------------------ */

static uint16_t cmd_meas_freq(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_FREQ, snap, 4);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n",
                              (unsigned long)read_u32(snap));
}

static uint16_t cmd_meas_duty(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_DUTY, snap, 4);
    RegMap_Unlock();
    uint32_t cp = read_u32(snap);
    return (uint16_t)snprintf((char *)resp, max, "%lu.%02lu\r\n",
                              (unsigned long)(cp / 100),
                              (unsigned long)(cp % 100));
}

static uint16_t cmd_meas_period(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_PERIOD, snap, 4);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n",
                              (unsigned long)read_u32(snap));
}

static uint16_t cmd_meas_pulse(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_PULSE, snap, 4);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n",
                              (unsigned long)read_u32(snap));
}

static uint16_t cmd_meas_all(uint8_t *resp, uint16_t max)
{
    uint8_t snap[16];
    uint8_t cap_snap[1];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_PERIOD, snap, 16);
    RegMap_BuildSnapshot(REG_CAPTURE_CTRL, cap_snap, 1);
    RegMap_Unlock();

    uint32_t period = read_u32(&snap[0]);
    uint32_t freq   = read_u32(&snap[4]);
    uint32_t duty   = read_u32(&snap[8]);
    uint32_t pulse  = read_u32(&snap[12]);

    return (uint16_t)snprintf((char *)resp, max,
        "%s,%lu,%lu.%02lu,%lu,%lu\r\n",
        cap_snap[0] ? "ON" : "OFF",
        (unsigned long)freq,
        (unsigned long)(duty / 100), (unsigned long)(duty % 100),
        (unsigned long)period,
        (unsigned long)pulse);
}

static uint16_t dispatch_meas(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r;

    r = scpi_match_kw(p, "frequency", 4); /* FREQ */
    if (r && is_query(r)) return cmd_meas_freq(resp, max);

    r = scpi_match_kw(p, "duty", 4);      /* DUTY */
    if (r && is_query(r)) return cmd_meas_duty(resp, max);

    r = scpi_match_kw(p, "period", 3);    /* PER */
    if (r && is_query(r)) return cmd_meas_period(resp, max);

    r = scpi_match_kw(p, "pulse", 4);     /* PULS */
    if (r && is_query(r)) return cmd_meas_pulse(resp, max);

    r = scpi_match_kw(p, "all", 3);       /* ALL */
    if (r && is_query(r)) return cmd_meas_all(resp, max);

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  CAPture subsystem                                                  */
/* ------------------------------------------------------------------ */

static uint16_t cmd_capt_edge(const char *p, uint8_t *resp, uint16_t max)
{
    if (is_query(p)) {
        uint8_t snap[1];
        RegMap_Lock();
        RegMap_BuildSnapshot(REG_EDGE, snap, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", snap[0]);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 1) return scpi_error(resp, max);
    uint8_t v = (uint8_t)val;
    RegMap_Lock();
    RegMap_Write(REG_EDGE, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%u\r\n", v);
}

static uint16_t cmd_capt_enable(const char *p, uint8_t *resp, uint16_t max)
{
    if (is_query(p)) {
        uint8_t snap[1];
        RegMap_Lock();
        RegMap_BuildSnapshot(REG_CAPTURE_CTRL, snap, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "%s\r\n",
                                  snap[0] ? "ON" : "OFF");
    }
    const char *arg = get_arg(p);
    uint8_t v;
    if (to_lower(arg[0]) == 'o' && to_lower(arg[1]) == 'n' &&
        (arg[2] == '\0' || arg[2] == ' '))
        v = 1;
    else if (to_lower(arg[0]) == 'o' && to_lower(arg[1]) == 'f' &&
             to_lower(arg[2]) == 'f' && (arg[3] == '\0' || arg[3] == ' '))
        v = 0;
    else {
        unsigned long val = strtoul(arg, NULL, 0);
        if (val > 1) return scpi_error(resp, max);
        v = (uint8_t)val;
    }
    RegMap_Lock();
    RegMap_Write(REG_CAPTURE_CTRL, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%s\r\n",
                              v ? "ON" : "OFF");
}

static uint16_t cmd_capt_tim_psc(const char *p, uint8_t *resp, uint16_t max)
{
    if (is_query(p)) {
        uint8_t snap[2];
        RegMap_Lock();
        RegMap_BuildSnapshot(REG_TIM_PSC, snap, 2);
        RegMap_Unlock();
        uint16_t val = (uint16_t)snap[0] | ((uint16_t)snap[1] << 8);
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", val);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 65535) return scpi_error(resp, max);
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(REG_TIM_PSC, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n", val);
}

static uint16_t cmd_capt_ic_psc(const char *p, uint8_t *resp, uint16_t max)
{
    if (is_query(p)) {
        uint8_t snap[1];
        RegMap_Lock();
        RegMap_BuildSnapshot(REG_IC_PSC, snap, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", snap[0]);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 3) return scpi_error(resp, max);
    uint8_t v = (uint8_t)val;
    RegMap_Lock();
    RegMap_Write(REG_IC_PSC, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%u\r\n", v);
}

static uint16_t dispatch_capt(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r;

    r = scpi_match_kw(p, "edge", 4);     /* EDGE */
    if (r) return cmd_capt_edge(r, resp, max);

    r = scpi_match_kw(p, "enable", 4);   /* ENAB */
    if (r) return cmd_capt_enable(r, resp, max);

    /* TIM:PSC — two-level */
    r = scpi_match_kw(p, "tim", 3);      /* TIM */
    if (r) {
        const char *c = skip_colon(r);
        if (c) {
            const char *r2 = scpi_match_kw(c, "psc", 3); /* PSC */
            if (r2) return cmd_capt_tim_psc(r2, resp, max);
        }
        return scpi_error(resp, max);
    }

    /* IC:PSC — two-level */
    r = scpi_match_kw(p, "ic", 2);       /* IC */
    if (r) {
        const char *c = skip_colon(r);
        if (c) {
            const char *r2 = scpi_match_kw(c, "psc", 3); /* PSC */
            if (r2) return cmd_capt_ic_psc(r2, resp, max);
        }
        return scpi_error(resp, max);
    }

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  SOURce subsystem (PWM outputs)                                     */
/* ------------------------------------------------------------------ */

static uint16_t cmd_pwm_freq(const char *p, uint8_t *resp, uint16_t max,
                              uint8_t reg_l, uint8_t reg_h)
{
    if (is_query(p)) {
        uint8_t snap_l[2], snap_h[2];
        RegMap_Lock();
        RegMap_BuildSnapshot(reg_l, snap_l, 2);
        RegMap_BuildSnapshot(reg_h, snap_h, 2);
        RegMap_Unlock();
        uint32_t hz = (uint16_t)(snap_l[0] | (snap_l[1] << 8))
                    | ((uint32_t)(snap_h[0] | (snap_h[1] << 8)) << 16);
        return (uint16_t)snprintf((char *)resp, max, "%lu\r\n",
                                  (unsigned long)hz);
    }
    const char *arg = get_arg(p);
    unsigned long hz = strtoul(arg, NULL, 0);
    uint16_t lo = (uint16_t)(hz & 0xFFFF);
    uint16_t hi = (uint16_t)((hz >> 16) & 0xFFFF);
    uint8_t buf[2];
    RegMap_Lock();
    write_u16(buf, lo);
    RegMap_Write(reg_l, buf, 2);
    write_u16(buf, hi);
    RegMap_Write(reg_h, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n",
                              (unsigned long)hz);
}

static uint16_t cmd_pwm_duty(const char *p, uint8_t *resp, uint16_t max,
                              uint8_t reg)
{
    if (is_query(p)) {
        uint8_t snap[2];
        RegMap_Lock();
        RegMap_BuildSnapshot(reg, snap, 2);
        RegMap_Unlock();
        uint16_t val = (uint16_t)snap[0] | ((uint16_t)snap[1] << 8);
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", val);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 10000) return scpi_error(resp, max);
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(reg, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n", val);
}

static uint16_t cmd_pwm_enable(const char *p, uint8_t *resp, uint16_t max,
                                uint8_t reg)
{
    if (is_query(p)) {
        uint8_t snap[1];
        RegMap_Lock();
        RegMap_BuildSnapshot(reg, snap, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n",
                                  snap[0] & 0x01);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    uint8_t v = val ? 1 : 0;
    RegMap_Lock();
    RegMap_Write(reg, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%u\r\n", v);
}

static uint16_t dispatch_pwm(const char *p, uint8_t *resp, uint16_t max,
                              uint8_t reg_fl, uint8_t reg_fh,
                              uint8_t reg_duty, uint8_t reg_ctrl)
{
    const char *r;

    r = scpi_match_kw(p, "frequency", 4); /* FREQ */
    if (r) return cmd_pwm_freq(r, resp, max, reg_fl, reg_fh);

    r = scpi_match_kw(p, "duty", 4);      /* DUTY */
    if (r) return cmd_pwm_duty(r, resp, max, reg_duty);

    r = scpi_match_kw(p, "enable", 4);    /* ENAB */
    if (r) return cmd_pwm_enable(r, resp, max, reg_ctrl);

    return scpi_error(resp, max);
}

static uint16_t dispatch_source(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r, *c;

    r = scpi_match_kw(p, "pwm1", 4);     /* PWM1 */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_pwm(c, resp, max,
                    REG_PWM1_FREQ_L, REG_PWM1_FREQ_H,
                    REG_PWM1_DUTY, REG_PWM1_CTRL);
        return scpi_error(resp, max);
    }

    r = scpi_match_kw(p, "pwm2", 4);     /* PWM2 */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_pwm(c, resp, max,
                    REG_PWM2_FREQ_L, REG_PWM2_FREQ_H,
                    REG_PWM2_DUTY, REG_PWM2_CTRL);
        return scpi_error(resp, max);
    }

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  LED subsystem                                                      */
/* ------------------------------------------------------------------ */

static uint16_t cmd_led_period(const char *p, uint8_t *resp, uint16_t max,
                                uint8_t reg)
{
    if (is_query(p)) {
        uint8_t snap[2];
        RegMap_Lock();
        RegMap_BuildSnapshot(reg, snap, 2);
        RegMap_Unlock();
        uint16_t val = (uint16_t)snap[0] | ((uint16_t)snap[1] << 8);
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", val);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val == 0 || val > 65535) return scpi_error(resp, max);
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(reg, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n", val);
}

static uint16_t cmd_led_duty(const char *p, uint8_t *resp, uint16_t max,
                              uint8_t reg)
{
    if (is_query(p)) {
        uint8_t snap[1];
        RegMap_Lock();
        RegMap_BuildSnapshot(reg, snap, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", snap[0]);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 100) return scpi_error(resp, max);
    uint8_t v = (uint8_t)val;
    RegMap_Lock();
    RegMap_Write(reg, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%u\r\n", v);
}

static uint16_t dispatch_led_channel(const char *p, uint8_t *resp, uint16_t max,
                                      uint8_t reg_per, uint8_t reg_duty)
{
    const char *r;

    r = scpi_match_kw(p, "period", 3);   /* PER */
    if (r) return cmd_led_period(r, resp, max, reg_per);

    r = scpi_match_kw(p, "duty", 4);     /* DUTY */
    if (r) return cmd_led_duty(r, resp, max, reg_duty);

    return scpi_error(resp, max);
}

static uint16_t dispatch_led(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r, *c;

    /* LED:G: ... (must check before PER/DUTY to avoid prefix clash) */
    r = scpi_match_kw(p, "g", 1);        /* G */
    if (r && *r == ':') {
        c = skip_colon(r);
        if (c) return dispatch_led_channel(c, resp, max,
                    REG_LED_G_PERIOD, REG_LED_G_DUTY);
    }

    /* LED:R: ... */
    r = scpi_match_kw(p, "r", 1);        /* R */
    if (r && *r == ':') {
        c = skip_colon(r);
        if (c) return dispatch_led_channel(c, resp, max,
                    REG_LED_R_PERIOD, REG_LED_R_DUTY);
    }

    /* LED:PER / LED:DUTY — default (status LED) */
    return dispatch_led_channel(p, resp, max,
                REG_LED_PERIOD, REG_LED_DUTY);
}

/* ------------------------------------------------------------------ */
/*  TRIGger subsystem                                                  */
/* ------------------------------------------------------------------ */

static uint16_t cmd_trig_width(const char *p, uint8_t *resp, uint16_t max)
{
    if (is_query(p)) {
        uint8_t snap[2];
        RegMap_Lock();
        RegMap_BuildSnapshot(REG_TRIG_WIDTH, snap, 2);
        RegMap_Unlock();
        uint16_t val = (uint16_t)snap[0] | ((uint16_t)snap[1] << 8);
        return (uint16_t)snprintf((char *)resp, max, "%u\r\n", val);
    }
    const char *arg = get_arg(p);
    unsigned long val = strtoul(arg, NULL, 0);
    if (val < 1 || val > 1000) return scpi_error(resp, max);
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(REG_TRIG_WIDTH, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "%lu\r\n", val);
}

static uint16_t dispatch_trig(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r;

    r = scpi_match_kw(p, "width", 4);    /* WIDT */
    if (r) return cmd_trig_width(r, resp, max);

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  SYSTem subsystem — NAME handlers                                   */
/* ------------------------------------------------------------------ */

static uint16_t cmd_syst_name(const char *p, uint8_t *resp, uint16_t max)
{
    extern char g_nickname[];
    if (is_query(p))
    {
        return (uint16_t)snprintf((char *)resp, max, "\"%s\"\r\n", g_nickname);
    }
    else
    {
        const char *arg = skip_spaces(p);
        if (*arg == '\0') return scpi_error(resp, max);

        /* Strip optional quotes */
        const char *start = arg;
        uint8_t len = 0;
        if (*start == '"') {
            start++;
            while (start[len] != '"' && start[len] != '\0' && len < NICKNAME_MAX_LEN)
                len++;
        } else {
            while (start[len] != '\0' && start[len] != '\r' &&
                   start[len] != '\n' && len < NICKNAME_MAX_LEN)
                len++;
            /* Strip trailing whitespace from unquoted input */
            while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'))
                len--;
        }
        if (len == 0) return scpi_error(resp, max);

        /* Write via regmap (handles zero-padding) */
        RegMap_Lock();
        RegMap_Write(REG_NICKNAME, (const uint8_t *)start, len);
        RegMap_Unlock();

        return (uint16_t)snprintf((char *)resp, max, "\"%s\"\r\n", g_nickname);
    }
}

static uint16_t cmd_syst_name_default(uint8_t *resp, uint16_t max)
{
    extern char g_nickname[];
    uint32_t uid0 = *(uint32_t *)0x1FFF7A10U;
    uint32_t uid1 = *(uint32_t *)0x1FFF7A14U;
    uint32_t uid2 = *(uint32_t *)0x1FFF7A18U;
    char nick[NICKNAME_MAX_LEN + 1];
    snprintf(nick, sizeof(nick), "%08lX%08lX",
             (unsigned long)(uid0 + uid2), (unsigned long)uid1);

    RegMap_Lock();
    RegMap_Write(REG_NICKNAME, (const uint8_t *)nick, NICKNAME_MAX_LEN);
    RegMap_Unlock();

    return (uint16_t)snprintf((char *)resp, max, "\"%s\"\r\n", g_nickname);
}

/* ------------------------------------------------------------------ */
/*  SYSTem subsystem                                                   */
/* ------------------------------------------------------------------ */

static const char HELP_TEXT[] =
    "SCPI Command Reference:\r\n"
    "  *IDN?                        - Device identification\r\n"
    "  *SAV                         - Save config to flash\r\n"
    "  *RST                         - Reset MCU\r\n"
    "  MEASure:FREQuency?           - Read frequency (Hz)\r\n"
    "  MEASure:DUTY?                - Read duty cycle (0.01%% units)\r\n"
    "  MEASure:PERiod?              - Read period (ticks)\r\n"
    "  MEASure:PULSe?               - Read pulse width (ticks)\r\n"
    "  MEASure:ALL?                 - Read all measurements\r\n"
    "  CAPture:EDGE[?] [0|1]       - Capture edge (0=rise,1=fall)\r\n"
    "  CAPture:ENABle[?] [ON|OFF]  - Enable/disable capture\r\n"
    "  CAPture:TIM:PSC[?] [0-65535]- Timer prescaler\r\n"
    "  CAPture:IC:PSC[?] [0-3]     - IC prescaler\r\n"
    "  SOURce:PWM1:FREQuency[?] [Hz]  - PWM1 frequency\r\n"
    "  SOURce:PWM1:DUTY[?] [0-10000]  - PWM1 duty (0.01%%)\r\n"
    "  SOURce:PWM1:ENABle[?] [0|1]    - PWM1 enable/apply\r\n"
    "  SOURce:PWM2:FREQuency[?] [Hz]  - PWM2 frequency\r\n"
    "  SOURce:PWM2:DUTY[?] [0-10000]  - PWM2 duty (0.01%%)\r\n"
    "  SOURce:PWM2:ENABle[?] [0|1]    - PWM2 enable/apply\r\n"
    "  LED:PERiod[?] [ms]          - Status LED period\r\n"
    "  LED:DUTY[?] [0-100]         - Status LED duty\r\n"
    "  LED:G:PERiod[?] [ms]        - Green LED period\r\n"
    "  LED:G:DUTY[?] [0-100]       - Green LED duty\r\n"
    "  LED:R:PERiod[?] [ms]        - Red LED period\r\n"
    "  LED:R:DUTY[?] [0-100]       - Red LED duty\r\n"
    "  TRIGger:WIDTh[?] [1-1000]   - Trigger pulse width (us)\r\n"
    "  SYSTem:NAME[?] [\"string\"]    - Device nickname (max 16 chars)\r\n"
    "  SYSTem:NAME:DEFault          - Reset nickname to serial number\r\n"
    "  SYSTem:VERSion?              - Firmware version\r\n"
    "  SYSTem:HELP?                 - Show this help\r\n"
    "\r\n"
    "Uppercase = mandatory abbreviation. Case-insensitive.\r\n";

static uint16_t dispatch_system(const char *p, uint8_t *resp, uint16_t max)
{
    const char *r;

    r = scpi_match_kw(p, "version", 4);  /* VERS */
    if (r && is_query(r))
        return (uint16_t)snprintf((char *)resp, max, "%08lX\r\n",
                                  (unsigned long)CFG_FW_VERSION);

    r = scpi_match_kw(p, "name", 4);     /* NAME */
    if (r) {
        const char *c2 = skip_colon(r);
        if (c2) {
            /* SYSTem:NAME:DEFault */
            const char *d = scpi_match_kw(c2, "default", 3);  /* DEF */
            if (d) return cmd_syst_name_default(resp, max);
            return scpi_error(resp, max);
        }
        return cmd_syst_name(r, resp, max);
    }

    r = scpi_match_kw(p, "help", 4);     /* HELP */
    if (r && is_query(r)) {
        uint16_t len = (uint16_t)strlen(HELP_TEXT);
        if (len >= max) len = max - 1;
        memcpy(resp, HELP_TEXT, len);
        return len;
    }

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  Main line dispatcher                                               */
/* ------------------------------------------------------------------ */

static uint16_t parse_line(const char *line, uint8_t *resp, uint16_t max)
{
    const char *p, *r, *c;

    p = skip_spaces(line);
    if (*p == '\0') return 0;

    /* ---- IEEE 488.2 common commands (prefix *) ---- */
    if (*p == '*') {
        p++;
        r = scpi_match_kw(p, "idn", 3);
        if (r && is_query(r)) return cmd_idn(resp, max);

        r = scpi_match_kw(p, "sav", 3);
        if (r && (*r == '\0' || *r == ' ')) return cmd_sav(resp, max);

        r = scpi_match_kw(p, "rst", 3);
        if (r && (*r == '\0' || *r == ' ')) return cmd_rst(resp, max);

        return scpi_error(resp, max);
    }

    /* ---- SCPI subsystem dispatch ---- */

    /* MEASure: */
    r = scpi_match_kw(p, "measure", 4);  /* MEAS */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_meas(c, resp, max);
        return scpi_error(resp, max);
    }

    /* CAPture: */
    r = scpi_match_kw(p, "capture", 4);  /* CAPT */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_capt(c, resp, max);
        return scpi_error(resp, max);
    }

    /* SOURce: */
    r = scpi_match_kw(p, "source", 4);   /* SOUR */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_source(c, resp, max);
        return scpi_error(resp, max);
    }

    /* LED: */
    r = scpi_match_kw(p, "led", 3);      /* LED */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_led(c, resp, max);
        return scpi_error(resp, max);
    }

    /* TRIGger: */
    r = scpi_match_kw(p, "trigger", 4);  /* TRIG */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_trig(c, resp, max);
        return scpi_error(resp, max);
    }

    /* SYSTem: */
    r = scpi_match_kw(p, "system", 4);   /* SYST */
    if (r) {
        c = skip_colon(r);
        if (c) return dispatch_system(c, resp, max);
        return scpi_error(resp, max);
    }

    return scpi_error(resp, max);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

uint16_t CDC_ProcessRxData(const uint8_t *data, uint16_t len,
                           uint8_t *response_buf, uint16_t response_max)
{
    uint16_t total = 0;

    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];

        if (c == '\r' || c == '\n') {
            if (s_line_pos == 0)
                continue;

            s_line[s_line_pos] = '\0';
            s_line_pos = 0;

            if (total + 2 <= response_max) {
                response_buf[total++] = '\r';
                response_buf[total++] = '\n';
            }

            uint16_t remaining = response_max - total;
            uint16_t n = parse_line(s_line, response_buf + total, remaining);
            total += n;
        } else {
            if (s_line_pos < LINE_BUF_SIZE - 1) {
                s_line[s_line_pos++] = c;
            }
        }
    }

    return total;
}

/* ------------------------------------------------------------------ */
/*  FIFO-based API                                                     */
/* ------------------------------------------------------------------ */

void CDC_ParseLine(const char *line)
{
    static uint8_t resp[2048];
    uint16_t n = parse_line(line, resp, sizeof(resp));
    if (n > 0)
    {
        CDC_TxPush(resp, n);
    }
}
