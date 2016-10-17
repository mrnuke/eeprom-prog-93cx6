# eeprom-prog-93cx6
spidev-based EEPROM programmer for 93Cxx serial EEPROMs

## About

eeprom-93cx6 is a utility for manipulating the contents of Microwire SPI serial
EEPROMs.

## Device geometry

Since 93Cxx EEPROMS do not have a support ID command, the geometry and
communication parameters must be known ahead of time. This is complicated by the
fact that SPI commands use a different number of bits the address field,
depending, on the device geometry.
For example, a 93C66 will use 9 address bits in a 512x8 configuration, and only
8 bits in a 256x16 configuration. However, the number of address bits is not
necessarily related to the number of words. A 93C56, although half the size,
uses the same number of address bits.

To simplify choosing the parameters, eeprom-93cx6 understands a number of
different profiles. This is specified with the '--eeprom-type' option. For x16
devices, the '--x16' option must also be specified.

If a profile is not available for the EEPROM, its size (in bytes) and number of
address bits can be specified with the '--eeprom-size' and '--addr-bits'
options. Note that either the eeprom type or size and address bits can be
specified, but not both.

## Usage

*  -D, --spi-device <dev> Specify SPI device\n
*  -t, --eeprom-type    Specify EEPROM type/part number\n
*  --x16                Specify if EEPROM is an x16 configuration\n
*  -r, --read <file>    Save contents of EEPROM to 'file'\n
*  -w, --write <file>   Write contents of 'file' to EEPROM\n
*  --burst-read         (advanced) Read EEPROM in single read command\n
*  -e, --erase          Erase EEPROM\n
*  -b, --addr-bits <nr> Specify number of address bits in command header\n
*  -s, --eeprom-size <nr> Specify size of EEPROM in bytes\n
*  -h, --help           Display this help menu\n

### Examples:

Read a 93c66 in 256x16 configuration:

    eeprom-93cx6 -D /dev/spidev2.0 -r eeprom.bin -t 93c66 --x16

Erase a 256x16 (512 byte) eeprom with 8 command address bits:

    eeprom-93cx6 -D /dev/spidev2.0 -e -b8 -s 512 --x16

