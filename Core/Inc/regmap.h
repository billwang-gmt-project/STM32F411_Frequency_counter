#ifndef REGMAP_H
#define REGMAP_H

#include <stdint.h>

/* Register map size */
#define REG_MAP_SIZE  0x58

/* Initialize the register access layer (creates mutex) */
void RegMap_Init(void);

/* Build a snapshot of the register map into caller's buffer.
 * Returns number of bytes available from start_reg to end of map.
 * Thread-safe (reads volatile globals atomically on Cortex-M4). */
uint8_t RegMap_BuildSnapshot(uint8_t start_reg, uint8_t *buf, uint8_t buf_size);

/* Process a register write. Returns 0 on success, nonzero on error.
 * data points to the value byte(s), data_len is 1 or 2.
 * Caller in task context MUST hold the regmap mutex.
 * I2C ISR calls this directly (no mutex needed -- ISR is self-serializing). */
uint8_t RegMap_Write(uint8_t reg_addr, const uint8_t *data, uint8_t data_len);

/* Acquire/release the register write mutex.
 * Only for task context (CDC/HID). Do NOT call from ISR. */
void RegMap_Lock(void);
void RegMap_Unlock(void);

#endif /* REGMAP_H */
