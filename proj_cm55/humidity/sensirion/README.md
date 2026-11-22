# Sensirion embedded-i2c-sht4x (vendored)

Unmodified copy of the core driver files from
<https://github.com/Sensirion/embedded-i2c-sht4x> (release 1.1.0,
commit `1b6d7148a704618ff5fdedf3b13247f5cdc3f5e5`), BSD-3-Clause, see *LICENSE*.

Files taken: `sensirion_common.[ch]`, `sensirion_config.h`, `sensirion_i2c.[ch]`,
`sensirion_i2c_hal.h`, `sht4x_i2c.[ch]`.

Not taken on purpose (ModusToolbox compiles every `.c` it finds in the project):

- `sensirion_i2c_hal.c` (empty stub): replaced by *../sht40_i2c_hal_psoc.c*,
  which implements `sensirion_i2c_hal.h` on the PSOC Edge PDL SCB I2C driver.
- `example-usage/`, `sample-implementations/`, `tests/`.

To update: copy the same files from a newer release over these ones; do not
edit them in place.
