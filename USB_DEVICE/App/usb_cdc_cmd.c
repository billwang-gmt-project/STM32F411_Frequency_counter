/**
 * @file    usb_cdc_cmd.c
 * @brief   CDC serial console command parser for the frequency counter.
 *
 * Accumulates incoming bytes into a line buffer. On CR or LF the
 * completed line is parsed and a human-readable response is built.
 * All register access goes through regmap.h so this module never
 * touches hardware directly.
 */

#include "usb_cdc_cmd.h"
#include "cdc_fifo.h"
#include "regmap.h"
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

/** Case-insensitive prefix match. Returns pointer past the prefix or NULL. */
static const char *match_prefix(const char *line, const char *prefix)
{
    while (*prefix) {
        char a = *line;
        char b = *prefix;
        /* tolower without pulling in ctype */
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return NULL;
        line++;
        prefix++;
    }
    return line;
}

/* ------------------------------------------------------------------ */
/*  Command handlers — each returns bytes written into resp[]          */
/* ------------------------------------------------------------------ */

static uint16_t cmd_freq(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_FREQ, snap, 4);
    RegMap_Unlock();
    uint32_t hz = read_u32(snap);
    return (uint16_t)snprintf((char *)resp, max, "Frequency: %lu Hz\r\n",
                              (unsigned long)hz);
}

static uint16_t cmd_duty(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_DUTY, snap, 4);
    RegMap_Unlock();
    uint32_t cp = read_u32(snap);
    uint32_t whole = cp / 100;
    uint32_t frac  = cp % 100;
    return (uint16_t)snprintf((char *)resp, max, "Duty: %lu.%02lu%%\r\n",
                              (unsigned long)whole, (unsigned long)frac);
}

static uint16_t cmd_period(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_PERIOD, snap, 4);
    RegMap_Unlock();
    uint32_t t = read_u32(snap);
    return (uint16_t)snprintf((char *)resp, max, "Period: %lu ticks\r\n",
                              (unsigned long)t);
}

static uint16_t cmd_pulse(uint8_t *resp, uint16_t max)
{
    uint8_t snap[4];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_PULSE, snap, 4);
    RegMap_Unlock();
    uint32_t p = read_u32(snap);
    return (uint16_t)snprintf((char *)resp, max, "Pulse: %lu ticks\r\n",
                              (unsigned long)p);
}

static uint16_t cmd_edge(uint8_t *resp, uint16_t max)
{
    uint8_t snap[1];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_EDGE, snap, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Edge: %s\r\n",
                              snap[0] ? "falling" : "rising");
}

static uint16_t cmd_capture(uint8_t *resp, uint16_t max)
{
    uint8_t snap[1];
    RegMap_Lock();
    RegMap_BuildSnapshot(REG_CAPTURE_CTRL, snap, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Capture: %s\r\n",
                              snap[0] ? "on" : "off");
}

static uint16_t cmd_status(uint8_t *resp, uint16_t max)
{
    /* Read first 16 bytes: PERIOD(4) + FREQ(4) + DUTY(4) + PULSE(4) */
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

    uint32_t d_whole = duty / 100;
    uint32_t d_frac  = duty % 100;

    return (uint16_t)snprintf((char *)resp, max,
        "Capture: %s\r\n"
        "Frequency: %lu Hz\r\n"
        "Duty: %lu.%02lu%%\r\n"
        "Period: %lu ticks\r\n"
        "Pulse: %lu ticks\r\n",
        cap_snap[0] ? "on" : "off",
        (unsigned long)freq,
        (unsigned long)d_whole, (unsigned long)d_frac,
        (unsigned long)period,
        (unsigned long)pulse);
}

/* ------------------------------------------------------------------ */
/*  "set" sub-commands                                                 */
/* ------------------------------------------------------------------ */

static uint16_t cmd_set_edge(const char *arg, uint8_t *resp, uint16_t max)
{
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 1) {
        return (uint16_t)snprintf((char *)resp, max,
            "Error: edge must be 0 (rising) or 1 (falling)\r\n");
    }
    uint8_t v = (uint8_t)val;
    RegMap_Lock();
    RegMap_Write(REG_EDGE, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Edge: %s\r\n",
                              v ? "falling" : "rising");
}

static uint16_t cmd_set_tim_psc(const char *arg, uint8_t *resp, uint16_t max)
{
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 65535) {
        return (uint16_t)snprintf((char *)resp, max,
            "Error: tim_psc must be 0-65535\r\n");
    }
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(REG_TIM_PSC, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Timer PSC: %lu\r\n",
                              (unsigned long)val);
}

static uint16_t cmd_set_ic_psc(const char *arg, uint8_t *resp, uint16_t max)
{
    unsigned long val = strtoul(arg, NULL, 0);
    if (val > 3) {
        return (uint16_t)snprintf((char *)resp, max,
            "Error: ic_psc must be 0-3\r\n");
    }
    uint8_t v = (uint8_t)val;
    RegMap_Lock();
    RegMap_Write(REG_IC_PSC, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "IC PSC: %lu\r\n",
                              (unsigned long)val);
}

static uint16_t cmd_set_capture(const char *arg, uint8_t *resp, uint16_t max)
{
    uint8_t v;
    if (match_prefix(arg, "on") && (arg[2] == '\0' || arg[2] == ' '))
        v = 1;
    else if (match_prefix(arg, "off") && (arg[3] == '\0' || arg[3] == ' '))
        v = 0;
    else {
        unsigned long val = strtoul(arg, NULL, 0);
        if (val > 1)
            return (uint16_t)snprintf((char *)resp, max,
                "Error: capture must be on/off or 0/1\r\n");
        v = (uint8_t)val;
    }
    RegMap_Lock();
    RegMap_Write(REG_CAPTURE_CTRL, &v, 1);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Capture: %s\r\n",
                              v ? "on" : "off");
}

static uint16_t cmd_set_trig_width(const char *arg, uint8_t *resp, uint16_t max)
{
    unsigned long val = strtoul(arg, NULL, 0);
    if (val < 1 || val > 1000) {
        return (uint16_t)snprintf((char *)resp, max,
            "Error: trig_width must be 1-1000\r\n");
    }
    uint8_t buf[2];
    write_u16(buf, (uint16_t)val);
    RegMap_Lock();
    RegMap_Write(REG_TRIG_WIDTH, buf, 2);
    RegMap_Unlock();
    return (uint16_t)snprintf((char *)resp, max, "Trigger width: %lu us\r\n",
                              (unsigned long)val);
}

/* Generic LED period/duty handler.
 * reg_period: register address for the 2-byte period
 * reg_duty:   register address for the 1-byte duty
 * name:       display name (e.g. "LED", "LED_G", "LED_R") */
static uint16_t cmd_set_led_generic(const char *subcmd, const char *name,
                                    uint8_t reg_period, uint8_t reg_duty,
                                    uint8_t *resp, uint16_t max)
{
    const char *p;

    p = match_prefix(subcmd, "period");
    if (p) {
        p = skip_spaces(p);
        unsigned long val = strtoul(p, NULL, 0);
        if (val > 65535) {
            return (uint16_t)snprintf((char *)resp, max,
                "Error: period must be 0-65535\r\n");
        }
        uint8_t buf[2];
        write_u16(buf, (uint16_t)val);
        RegMap_Lock();
        RegMap_Write(reg_period, buf, 2);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max,
            "%s period: %lu ms\r\n", name, (unsigned long)val);
    }

    p = match_prefix(subcmd, "duty");
    if (p) {
        p = skip_spaces(p);
        unsigned long val = strtoul(p, NULL, 0);
        if (val > 100) {
            return (uint16_t)snprintf((char *)resp, max,
                "Error: duty must be 0-100\r\n");
        }
        uint8_t v = (uint8_t)val;
        RegMap_Lock();
        RegMap_Write(reg_duty, &v, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max,
            "%s duty: %lu%%\r\n", name, (unsigned long)val);
    }

    return (uint16_t)snprintf((char *)resp, max,
        "Error: expected 'period <ms>' or 'duty <0-100>'\r\n");
}

/* Generic PWM freq/duty/enable handler.
 * reg_freq_l, reg_freq_h, reg_duty, reg_ctrl are the register addresses. */
static uint16_t cmd_set_pwm_generic(const char *subcmd, const char *name,
                                    uint8_t reg_freq_l, uint8_t reg_freq_h,
                                    uint8_t reg_duty, uint8_t reg_ctrl,
                                    uint8_t *resp, uint16_t max)
{
    const char *p;

    p = match_prefix(subcmd, "freq");
    if (p) {
        p = skip_spaces(p);
        unsigned long hz = strtoul(p, NULL, 0);
        uint16_t lo = (uint16_t)(hz & 0xFFFF);
        uint16_t hi = (uint16_t)((hz >> 16) & 0xFFFF);
        uint8_t buf[2];
        RegMap_Lock();
        write_u16(buf, lo);
        RegMap_Write(reg_freq_l, buf, 2);
        write_u16(buf, hi);
        RegMap_Write(reg_freq_h, buf, 2);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max,
            "%s freq staged: %lu Hz\r\n", name, (unsigned long)hz);
    }

    p = match_prefix(subcmd, "duty");
    if (p) {
        p = skip_spaces(p);
        unsigned long val = strtoul(p, NULL, 0);
        if (val > 10000) {
            return (uint16_t)snprintf((char *)resp, max,
                "Error: duty must be 0-10000 (0.01%% units)\r\n");
        }
        uint8_t buf[2];
        write_u16(buf, (uint16_t)val);
        RegMap_Lock();
        RegMap_Write(reg_duty, buf, 2);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max,
            "%s duty staged: %lu\r\n", name, (unsigned long)val);
    }

    p = match_prefix(subcmd, "enable");
    if (p) {
        p = skip_spaces(p);
        unsigned long val = strtoul(p, NULL, 0);
        uint8_t v = val ? 1 : 0;
        RegMap_Lock();
        RegMap_Write(reg_ctrl, &v, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max,
            "%s %s\r\n", name, v ? "enabled" : "disabled");
    }

    return (uint16_t)snprintf((char *)resp, max,
        "Error: expected 'freq <hz>', 'duty <0-10000>', or 'enable <0|1>'\r\n");
}

/* ------------------------------------------------------------------ */
/*  Help text                                                          */
/* ------------------------------------------------------------------ */

static const char HELP_TEXT[] =
    "Commands:\r\n"
    "  freq                    - Read frequency (Hz)\r\n"
    "  duty                    - Read duty cycle (%%)\r\n"
    "  period                  - Read period (ticks)\r\n"
    "  pulse                   - Read pulse width (ticks)\r\n"
    "  status                  - Read all measurements\r\n"
    "  edge                    - Read capture edge\r\n"
    "  capture                 - Read capture state (on/off)\r\n"
    "  set edge <0|1>          - Set capture edge (0=rising, 1=falling)\r\n"
    "  set capture <on|off>    - Enable/disable input capture\r\n"
    "  set tim_psc <0-65535>   - Set timer prescaler\r\n"
    "  set ic_psc <0-3>        - Set input capture prescaler\r\n"
    "  set pwm1 freq <hz>      - Stage PWM1 frequency\r\n"
    "  set pwm1 duty <0-10000> - Stage PWM1 duty (0.01%% units)\r\n"
    "  set pwm1 enable <0|1>   - Apply staged PWM1 config\r\n"
    "  set pwm2 freq <hz>      - Stage PWM2 frequency\r\n"
    "  set pwm2 duty <0-10000> - Stage PWM2 duty (0.01%% units)\r\n"
    "  set pwm2 enable <0|1>   - Apply staged PWM2 config\r\n"
    "  set led period <ms>     - Set status LED blink period\r\n"
    "  set led duty <0-100>    - Set status LED on-duty (%%)\r\n"
    "  set led_g period <ms>   - Set green LED blink period\r\n"
    "  set led_g duty <0-100>  - Set green LED on-duty (%%)\r\n"
    "  set led_r period <ms>   - Set red LED blink period\r\n"
    "  set led_r duty <0-100>  - Set red LED on-duty (%%)\r\n"
    "  set trig_width <1-1000> - Set trigger pulse width (us)\r\n"
    "  save                    - Save config to flash\r\n"
    "  help                    - Show this help\r\n";

/* ------------------------------------------------------------------ */
/*  Main line dispatcher                                               */
/* ------------------------------------------------------------------ */

/**
 * Parse a completed, NUL-terminated command line and write the
 * response into resp[].  Returns number of bytes written.
 */
static uint16_t parse_line(const char *line, uint8_t *resp, uint16_t max)
{
    const char *p;

    /* Skip leading whitespace */
    line = skip_spaces(line);

    /* Empty line — ignore */
    if (*line == '\0')
        return 0;

    /* ---- Read commands ---- */

    if (match_prefix(line, "status") &&
        (line[6] == '\0' || line[6] == ' '))
        return cmd_status(resp, max);

    if (match_prefix(line, "freq") &&
        (line[4] == '\0' || line[4] == ' '))
        return cmd_freq(resp, max);

    if (match_prefix(line, "duty") &&
        (line[4] == '\0' || line[4] == ' '))
        return cmd_duty(resp, max);

    if (match_prefix(line, "period") &&
        (line[6] == '\0' || line[6] == ' '))
        return cmd_period(resp, max);

    if (match_prefix(line, "pulse") &&
        (line[5] == '\0' || line[5] == ' '))
        return cmd_pulse(resp, max);

    if (match_prefix(line, "edge") &&
        (line[4] == '\0' || line[4] == ' '))
        return cmd_edge(resp, max);

    if (match_prefix(line, "capture") &&
        (line[7] == '\0' || line[7] == ' '))
        return cmd_capture(resp, max);

    /* ---- save ---- */

    if (match_prefix(line, "save") &&
        (line[4] == '\0' || line[4] == ' ')) {
        uint8_t key = SAVE_CFG_KEY;
        RegMap_Lock();
        RegMap_Write(REG_SAVE_CFG, &key, 1);
        RegMap_Unlock();
        return (uint16_t)snprintf((char *)resp, max, "Config saved\r\n");
    }

    /* ---- help ---- */

    if (match_prefix(line, "help") &&
        (line[4] == '\0' || line[4] == ' ')) {
        uint16_t len = (uint16_t)strlen(HELP_TEXT);
        if (len >= max) len = max - 1;
        memcpy(resp, HELP_TEXT, len);
        return len;
    }

    /* ---- set commands ---- */

    p = match_prefix(line, "set ");
    if (p) {
        p = skip_spaces(p);
        const char *sub;

        /* set edge */
        sub = match_prefix(p, "edge ");
        if (sub)
            return cmd_set_edge(skip_spaces(sub), resp, max);

        /* set tim_psc */
        sub = match_prefix(p, "tim_psc ");
        if (sub)
            return cmd_set_tim_psc(skip_spaces(sub), resp, max);

        /* set ic_psc */
        sub = match_prefix(p, "ic_psc ");
        if (sub)
            return cmd_set_ic_psc(skip_spaces(sub), resp, max);

        /* set capture */
        sub = match_prefix(p, "capture ");
        if (sub)
            return cmd_set_capture(skip_spaces(sub), resp, max);

        /* set trig_width */
        sub = match_prefix(p, "trig_width ");
        if (sub)
            return cmd_set_trig_width(skip_spaces(sub), resp, max);

        /* set pwm1 ... */
        sub = match_prefix(p, "pwm1 ");
        if (sub)
            return cmd_set_pwm_generic(skip_spaces(sub), "PWM1",
                        REG_PWM1_FREQ_L, REG_PWM1_FREQ_H,
                        REG_PWM1_DUTY, REG_PWM1_CTRL,
                        resp, max);

        /* set pwm2 ... */
        sub = match_prefix(p, "pwm2 ");
        if (sub)
            return cmd_set_pwm_generic(skip_spaces(sub), "PWM2",
                        REG_PWM2_FREQ_L, REG_PWM2_FREQ_H,
                        REG_PWM2_DUTY, REG_PWM2_CTRL,
                        resp, max);

        /* set led_g ... (must check before "led" to avoid prefix clash) */
        sub = match_prefix(p, "led_g ");
        if (sub)
            return cmd_set_led_generic(skip_spaces(sub), "LED_G",
                        REG_LED_G_PERIOD, REG_LED_G_DUTY,
                        resp, max);

        /* set led_r ... */
        sub = match_prefix(p, "led_r ");
        if (sub)
            return cmd_set_led_generic(skip_spaces(sub), "LED_R",
                        REG_LED_R_PERIOD, REG_LED_R_DUTY,
                        resp, max);

        /* set led ... (checked after led_g / led_r) */
        sub = match_prefix(p, "led ");
        if (sub)
            return cmd_set_led_generic(skip_spaces(sub), "LED",
                        REG_LED_PERIOD, REG_LED_DUTY,
                        resp, max);

        return (uint16_t)snprintf((char *)resp, max,
            "Unknown set target. Type 'help'\r\n");
    }

    /* ---- Unknown ---- */
    return (uint16_t)snprintf((char *)resp, max,
        "Unknown command. Type 'help'\r\n");
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
            /* Ignore empty lines caused by \r\n pairs */
            if (s_line_pos == 0)
                continue;

            /* NUL-terminate the accumulated line */
            s_line[s_line_pos] = '\0';
            s_line_pos = 0;

            /* Echo a blank line before the response for readability */
            if (total + 2 <= response_max) {
                response_buf[total++] = '\r';
                response_buf[total++] = '\n';
            }

            /* Parse and append response */
            uint16_t remaining = response_max - total;
            uint16_t n = parse_line(s_line, response_buf + total, remaining);
            total += n;
        } else {
            /* Accumulate printable characters, silently drop overflow */
            if (s_line_pos < LINE_BUF_SIZE - 1) {
                s_line[s_line_pos++] = c;
            }
        }
    }

    return total;
}

/* ------------------------------------------------------------------ */
/*  New FIFO-based API                                                 */
/* ------------------------------------------------------------------ */

void CDC_ParseLine(const char *line)
{
    static uint8_t resp[256];
    uint16_t n = parse_line(line, resp, sizeof(resp));
    if (n > 0)
    {
        CDC_TxPush(resp, n);
    }
}
