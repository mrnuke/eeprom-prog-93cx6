/*
 * eeprom-93cxx - a spidev based utility for flashing serial EEPROMs
 *
 * Copyright (C) 2016 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <fcntl.h>
#include <getopt.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>


#define OPCODE_READ		(0x2)
#define OPCODE_WRITE		(0x1)
#define OPCODE_EWEN		(0x0)
#define  SUBCODE_EWEN		(3)
#define  SUBCODE_ERAL		(2)
#define  SUBCODE_EWDS		(0)

enum eeprom_action {
	NONE,
	EEPROM_READ,
	EEPROM_ERASE,
	EEPROM_WRITE,
};

enum eeprom_flags {
	EEPROM_X8	= 0x01,
	EEPROM_X16	= 0x02,
	EEPROM_ORG	= (EEPROM_X8 | EEPROM_X16)
};

struct eeprom {
	const char *name;
	int spi_fd;
	uint16_t size;
	uint8_t addr_bits;
	uint8_t flags;
	bool is_x16;
};

struct eeprom_cfg {
	const char *filename;
	const char *spidev;
	struct eeprom *eeprom;
	enum eeprom_action action;
	bool burst_read;
};

static const struct eeprom eeprom_types_list[] = { {
	.name = "93c66",
	.size = 512,
	.addr_bits = 9,
	.flags = EEPROM_ORG,
}, {
	.name = "93c56",
	.size = 256,
	.addr_bits = 8,
	.flags = EEPROM_ORG,
}, {
	.name = "93c46",
	.size = 128,
	.addr_bits = 7,
	.flags = EEPROM_ORG,
}, {
	.name = "93c06",
	.size = 32,
	.addr_bits = 6,
	.flags = EEPROM_X16,
}, {
	/* .size = 0 terminates the list */
	.size = 0,
}
};

static const struct eeprom *eeprom_find(const char *type)
{
	const struct eeprom *eeprom = eeprom_types_list;
	while (eeprom->size) {
		if (!strcasecmp(eeprom->name, type))
			return eeprom;
		eeprom++;
	}

	return NULL;
}

static int eeprom_run(const struct eeprom_cfg *);
static int sanitize_input(const struct eeprom_cfg *);

const char help[] =
"  -D, --spi-device <dev> Specify SPI device\n"
"  -t, --eeprom-type    Specify EEPROM type/part number\n"
"  --x16                Specify if EEPROM is an x16 configuration\n"
"  -r, --read <file>    Save contents of EEPROM to 'file'\n"
"  -w, --write <file>   Write contents of 'file' to EEPROM\n"
"  --burst-read         (advanced) Read EEPROM in single read command\n"
"  -e, --erase          Erase EEPROM\n"
"  -b, --addr-bits <nr> Specify number of address bits in command header\n"
"  -s, --eeprom-size <nr> Specify size of EEPROM in bytes\n"
"  -h, --help           Display this help menu\n"
"Examples:\n"
"  %s -D /dev/spidev2.0 -r eeprom.bin -t 93c66 --x16\n"
"  %s -D /dev/spidev2.0 -e -b8 -s 512 --x16\n";

static void print_help(const char *program_name)
{
	printf(help, program_name, program_name);
}

int main(int argc, char *argv[])
{
	const char *eeprom_type = NULL;
	const struct eeprom *eepromy;
	int opt, x16 = 0, burst = 0, option_index = 0;
	bool parameter_specified = false, type_specified = false;

	/* Start with some defauls. */
	struct eeprom eeprom = {
		.name = "custom",
		.addr_bits = 8,
		.size = 256,
		.is_x16 = 0,
		.flags = EEPROM_ORG,
	};
	struct eeprom_cfg cfg = {
		.spidev = "/dev/spidev1.0",
		.filename = "",
		.action = NONE,
		.eeprom = &eeprom,
	};
	struct eeprom_cfg *config = &cfg;

	struct option long_options[] = {
		{"spi-device",	required_argument,	0, 'D'},
		{"eeprom-type",	required_argument,	0, 't'},
		{"addr-bits",	required_argument,	0, 'b'},
		{"eeprom-size",	required_argument,	0, 's'},
		{"x16",		no_argument,		&x16, 1},
		{"read",	required_argument,	0, 'r'},
		{"write",	required_argument,	0, 'w'},
		{"erase",	no_argument,		0, 'e'},
		{"burst-read",	no_argument,		&burst, 1},
		{"help",	no_argument,		0, 'h'},
		{0, 0, 0, 0}
	};

	while (1) {
		opt = getopt_long(argc, argv, "D:t:b:s:r:w:v:e",
				  long_options, &option_index);

		if (opt == EOF)
			break;

		switch (opt) {
			case 0:
				/* Some flag was set by getopt(). Move along. */
				break;
			case 't':
				eeprom_type = strdup(optarg);
				type_specified = true;
				break;
			case 'D':
				config->spidev = strdup(optarg);
				break;
			case 'b':
				config->eeprom->addr_bits = atoi(optarg);
				parameter_specified = true;
				break;
			case 's':
				config->eeprom->size = atoi(optarg);
				parameter_specified = true;
				break;
			case 'r':
				config->filename = strdup(optarg);
				config->action = EEPROM_READ;
				break;
			case 'w':
				config->filename = strdup(optarg);
				config->action = EEPROM_WRITE;
				break;
			case 'e':
				config->action = EEPROM_ERASE;
				break;
			case 'h':
				print_help(argv[0]);
				exit(EXIT_SUCCESS);
			default:
				print_help(argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	config->eeprom->is_x16 = x16;
	config->burst_read = burst;

	if (type_specified && parameter_specified) {
		fprintf(stderr, "Please specify either EEPROM type, or EEPROM"
		" parameters, but not both");
		return EXIT_FAILURE;
	}

	if (type_specified) {
		eepromy = eeprom_find(eeprom_type);

		if (!eepromy) {
			fprintf(stderr, "Unknown EEPROM type: %s\n",
				eeprom_type);
			return EXIT_FAILURE;
		}

		*config->eeprom = *eepromy;
		config->eeprom->is_x16 = x16;
		/* x16 mode uses one less address bits than x8 */
		if (x16)
			config->eeprom->addr_bits--;
	}

	if (sanitize_input(config) < 0)
		return EXIT_FAILURE;

	return eeprom_run(config);
}

static int sanitize_input(const struct eeprom_cfg *config)
{
	uint8_t comm_bits;

	if (config->eeprom->size == 0) {
		fprintf(stderr, "EEPROM cannot be zero!\n");
		return -1;
	}

	if (config->eeprom->size & (config->eeprom->size - 1)) {
		fprintf(stderr, "Given EEPROM size %u is not a power of 2!\n");
		return -1;
	}

	if ((config->eeprom->addr_bits < 5)
		|| (config->eeprom->addr_bits > 9)) {
		fprintf(stderr, "addr-bits Should be between be 5 and 9\n");
	return -1;
		}

		if (config->eeprom->is_x16 && !(config->eeprom->flags & EEPROM_X16)) {
			fprintf(stderr, "Selected EEPROM does not support x16 mode.\n");
			return -1;
		}

		if (!config->eeprom->is_x16 && !(config->eeprom->flags & EEPROM_X8)) {
			fprintf(stderr, "Selected EEPROM does not support x8 mode.\n");
			return -1;
		}
}

/*
 * Prepare the command header.
 * The opcode and address or data don't add up to an integer number of 8-bit
 * bytes. Some SPI controllers don't like odd-sized words, so it's prudent to
 * have trabsactions in 8-bit multiples.
 * Luckily, the chip only starts interpreting commands when MOSI goes high while
 * CS is asserted (start condition). We can pad the data up to 16 bits with
 * leading zeroes, so that we can use 8-bit transactions.
 */
static void prepare_cmd(const struct eeprom *eeprom,
			struct spi_ioc_transfer *xfer,
			uint8_t txbuf[4],
			uint8_t cmd, uint16_t addr, uint8_t dummy_bits)
{
	uint16_t command, bits;

	bits = eeprom->addr_bits + dummy_bits;
	cmd |= 1 << 2;			/* Add the start bit. */
	addr &= (1 << bits) - 1;	/* Mask off any extra address bits. */
	command = (cmd << bits) | addr;

	txbuf[0] = command >> 8;
	txbuf[1] = command;
	memset(xfer, 0, sizeof(*xfer));
	xfer->tx_buf = (uintptr_t)txbuf;
	xfer->bits_per_word = 8;
	xfer->len = 2;
}

/* Read from the EEPROM array. */
static int read_data(const struct eeprom *eeprom, void *data, size_t len,
		     uint16_t addr)
{
	uint8_t buf[4];
	struct spi_ioc_transfer xfer[2] = {{0}, {0}};

	prepare_cmd(eeprom, xfer, buf, OPCODE_READ, addr, 1);
	xfer[0].speed_hz = 100000;

	xfer[1].rx_buf = (uintptr_t)data;
	xfer[1].len = len;
	xfer[1].bits_per_word = 8;
	xfer[1].speed_hz = 100000;

	return ioctl(eeprom->spi_fd, SPI_IOC_MESSAGE(2), &xfer);
}

static uint8_t read_status(const struct eeprom *eeprom)
{
	uint8_t status = 0;
	struct spi_ioc_transfer xfer[1] = {{0}};

	xfer[0].rx_buf = (uintptr_t)&status;
	xfer[0].len = 1;
	xfer[0].bits_per_word = 8;
	xfer[0].speed_hz = 100000;

	ioctl(eeprom->spi_fd, SPI_IOC_MESSAGE(1), xfer);

	return status;
}

static int write_data(const struct eeprom *eeprom, uint16_t addr,
		      const uint8_t *data, size_t len)
{
	uint8_t buf[4];
	struct spi_ioc_transfer xfer[2] = {{0}, {0}};

	prepare_cmd(eeprom, xfer, buf, OPCODE_WRITE, addr, 0);
	xfer[0].speed_hz = 100000;

	xfer[1].tx_buf = (uintptr_t)data;
	xfer[1].len = len;
	xfer[1].bits_per_word = 8;
	xfer[1].speed_hz = 100000;

	return ioctl(eeprom->spi_fd, SPI_IOC_MESSAGE(2), xfer);
}

static int send_command(const struct eeprom *eeprom, uint8_t op, uint8_t subop)
{
	uint8_t buf[4];
	uint16_t subcode = subop << (eeprom->addr_bits - 2);

	struct spi_ioc_transfer xfer[1] = {{0}};

	prepare_cmd(eeprom, xfer, buf, op, subcode, 0);
	xfer[0].speed_hz = 100000;

	return ioctl(eeprom->spi_fd, SPI_IOC_MESSAGE(1), xfer);
}

static int enable_write(const struct eeprom *eeprom)
{
	return send_command(eeprom, OPCODE_EWEN, SUBCODE_EWEN);
}

static int diable_write(const struct eeprom *eeprom)
{
	return send_command(eeprom, OPCODE_EWEN, SUBCODE_EWDS);
}

static int erase_all(const struct eeprom *eeprom)
{
	return send_command(eeprom, OPCODE_EWEN, SUBCODE_ERAL);
}

/* Read contents of EEPROM. */
static int eeprom_read(const struct eeprom_cfg *config)
{
	FILE *out;
	void *buf;
	int ret, i, step;
	step = config->eeprom->is_x16 ? 2 : 1;

	if (config->burst_read)
		step = config->eeprom->size;

	out = fopen(config->filename, "w");
	if (!out) {
		perror("Could not open output file.");
		return EXIT_FAILURE;
	}

	buf = malloc(config->eeprom->size);

	for (i = 0; i < config->eeprom->size; i += step) {
		ret = read_data(config->eeprom, buf + i, step, i);
		if (ret < 0) {
			perror("Could not execute SPI transaction (eeprom read)");
			return EXIT_FAILURE;
		}
	}

	fwrite(buf, 1, config->eeprom->size, out);
	fclose(out);
	free(buf);
}

static int eeprom_program_array(const struct eeprom *eeprom, const uint8_t *data)
{
	size_t i;
	uint16_t word;
	int ret;
	const size_t step = (eeprom->is_x16) ? 2 : 1;

	for (i = 0; i < eeprom->size; i += step) {
		if (eeprom->is_x16);
		ret = write_data(eeprom, i / step, data + i, step);
		if (ret < 0) {
			perror("Could not execute SPI transaction (eeprom write)");
			return EXIT_FAILURE;
		}

		while (read_status(eeprom) != 0xff)
			;
	}
}

/* Program EEPROM. All EEPROMS will erase the word before a write. */
static int eeprom_write(const struct eeprom_cfg *config)
{
	FILE *in;
	void *buf;
	int ret;
	long int size;
	size_t num_bytes_in;

	in = fopen(config->filename, "r");
	if (!in) {
		perror("Could not open input file.");
		return EXIT_FAILURE;
	}

	buf = malloc(config->eeprom->size);

	/* Find file size. */
	fseek(in, 0, SEEK_END);
	size = ftell(in);
	rewind(in);

	if (size != config->eeprom->size) {
		fprintf(stderr, "File size does not match EEPROM size!\n");
		return EXIT_FAILURE;
	}

	num_bytes_in = fread(buf, 1, config->eeprom->size, in);
	if (num_bytes_in != config->eeprom->size) {
		fprintf(stderr, "Failed to read contents of %s!\n",
			config->filename);
		return EXIT_FAILURE;
	}

	fclose(in);

	ret = enable_write(config->eeprom);
	if (ret < 0) {
		perror("Could not execute SPI transaction (enable write)");
		return EXIT_FAILURE;

	}

	eeprom_program_array(config->eeprom, buf);
}

/* Erase entire contents of the EEPROM. */
static int eeprom_erase(const struct eeprom_cfg *config)
{
	int ret;

	ret = enable_write(config->eeprom);
	if (ret < 0) {
		perror("Could not execute SPI transaction (enable write)");
		return EXIT_FAILURE;
	}

	ret = erase_all(config->eeprom);
	if (ret < 0) {
		perror("Could not execute SPI transaction (erase all)");
		return EXIT_FAILURE;
	}
}

/* Open and configure SPI master. */
static int init_spi_master(const char *spidev)
{
	int spif, ret;
	/* Mode 0, but with CS active-high */
	int mode = SPI_MODE_0 | SPI_CS_HIGH;

	spif = open(spidev, O_RDWR);
	if (spif < 0) {
		perror("Could not open SPI device.");
		return -1;
	}

	ret = ioctl(spif, SPI_IOC_WR_MODE, &mode);
	if (ret < 0) {
		perror("Could not set SPI mode.");
		return -1;
	}

	return spif;
}

static int eeprom_run(const struct eeprom_cfg *config)
{
	int spif, ret, num_words;

	num_words = config->eeprom->size;
	if (config->eeprom->is_x16)
		num_words /= 2;

	printf("EEPROM config: %s, %u%s, %u command address bits\n",
	       config->eeprom->name, num_words,
	(config->eeprom->is_x16) ? "x16" : "x8",
	       config->eeprom->addr_bits);

	spif = init_spi_master(config->spidev);
	if (spif < 0)
		return EXIT_FAILURE;

	config->eeprom->spi_fd = spif;

	if (config->action == EEPROM_READ)
		return eeprom_read(config);
	else if (config->action == EEPROM_WRITE)
		return eeprom_write(config);
	else if (config->action == EEPROM_ERASE)
		return eeprom_erase(config);
	else
		perror("Not implemented");

	return 0;
}
