/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TouchNetix aXiom Touchscreen Driver#
 *
 * Copyright (C) 2020, 2021 TouchNetix Ltd.
 *
 * Author(s): Mark Satterthwaite <mark.satterthwaite@touchnetix.com>
 *            Pedro Torruella <pedro.torruella@touchnetix.com>
 *            Bart Prescott <bartp@baasheep.co.uk>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version. For details check
 * TNxAN00035: "aXiom_Touch_Controller_Comms_Protocol"
 *
 */
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/input/mt.h>
#include <linux/crc16.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>

#ifndef __AXIOM_CORE_H
#define __AXIOM_CORE_H

#define U41_MAX_TARGETS (10U)
#define U46_AUX_CHANNELS (4U)
#define U46_AUX_MASK (0xFFFU)
#define U31_BYTES_PER_USAGE (6U)
#define USAGE_2DCTS_REPORT_ID (0x41U)
#define USAGE_2AUX_REPORT_ID (0x46U)
#define USAGE_2HB_REPORT_ID (0x01U)
#define PROX_LEVEL (-128)
#define AX_U31_PAGE0_LENGTH (0x0C)
#define AX_COMMS_WRITE (0x00U)
#define AX_COMMS_READ (0x80U)
#define AX_COMMS_BYTES_MASK (0xFFU)
#define COMMS_MAX_USAGE_PAGES (3)
#define AX_COMMS_PAGE_SIZE (256)
#define COMMS_OVERFLOW_MSK (0x80)
#define COMMS_REPORT_LEN_MSK (0x7F)

#define DISABLE 0
#define ENABLE 1

/*System command manager*/
#define SYSMGR_CMD_HARD_RESET 1
#define SYSMGR_CMD_SOFT_RESET 2
#define SYSMGR_CMD_REBASELINE 3
#define SYSMGR_CMD_STOP 5
#define SYSMGR_CMD_START 6
#define SYSMGR_CMD_SAVE_CONFIG 7
#define SYSMGR_CMD_COMPUTE_CRCS 9
#define SYSMGR_CMD_ENTER_BOOTLOADER 11
#define SYSMGR_CMD_RUN_SELF_TESTS 12
#define BLP_FIFO_ADDRESS 0x0102
#define BLP_REG_COMMAND 0x0100
#define BLP_REG_STATUS 0x0100

/*CDU*/
#define CDU_XFER_SIZE 48
#define CDU_XFER_SIZE_COMMAND 8
#define CDU_XFER_SIZE_DATA 48
#define CDU_XFER_SIZE_FULL (CDU_XFER_SIZE_COMMAND + CDU_XFER_SIZE_DATA)

#define CDU_CMD_FETCH 0x0001
#define CDU_CMD_STORE 0x0002
#define CDU_CMD_COMMIT 0x0003
#define CDU_CMD_QUERY 0x0004
#define CDU_ERROR_MASK 0x8000


#ifdef __AXIOM_KERNEL_5_4
#define input_mt_report_slot_inactive(dev) input_mt_report_slot_state(dev, 0, false)
#endif

extern bool poll_enable;
extern int poll_interval;
// purpose: Holds device specific information
struct u31_DeviceInfo {
	u8 bootloader_mode;
	u16 device_id;
	u8 fw_major;
	u8 fw_minor;
	u16 fw_info_extra;
	u8 bootloader_fw_ver_major;
	u8 bootloader_fw_ver_minor;
	u16 jedec_id;
	u8 num_usages;
	u8 silicon_revision;
};

// purpose: Describes parameters of a specific usage, essenstially a single
//          element of the "Usage Table"
struct usage_Entry {
	u8 id;
	u8 is_report;
	u8 start_page;
	u8 num_pages;
	u8 usage_rev;
	u16 length;
};

// purpose: Holds state of a "Target", A.K.A. as a "touch", but called a
//          target as it can be a detected "target" prior to touch, eg, hovering.
enum u41_Target_State_e {
	Target_State_Not_Present = 0,
	Target_State_Prox = 1,
	Target_State_Hover = 2,
	Target_State_Touching = 3,

	Target_State_Min = Target_State_Not_Present,
	Target_State_Max = Target_State_Touching,
};

// purpose: Holds information describing a target.
struct u41_Target {
	enum u41_Target_State_e state;
	u16 x;
	u16 y;
	s8 z;
	bool insert;
	bool touch;
};

// purpose: Holds decoded data from an aXiom u41 report.
struct u41_Target_Report {
	u8 index;
	u8 present;
	u16 x;
	u16 y;
	s8 z;
};

// purpose: I2C & SPI command header structure
struct AxiomCmdHeader {
	u16 target_address;
	u16 length : 15;
	u16 read : 1;
	u8 writeData[];
};

// purpose: Groups several structures needed for the core module.
struct axiom_data_core {
	// aXiom entries
	struct u31_DeviceInfo u31_Info;
	struct u41_Target targets[U41_MAX_TARGETS];
	struct usage_Entry *usage_table;
	struct u33_CRCData *crc_data;
	bool usage_table_populated;
	u8 max_report_len;
	u32 report_overflow_counter;
	u32 report_counter;
	u32 bus_holdoff_delay_us;
	// This could potentially be allocated during discovery.
	u8 rx_buf[COMMS_MAX_USAGE_PAGES * AX_COMMS_PAGE_SIZE];

	// Input Sub-system
	struct input_dev *input_dev;

	int irq_line;

	// RST gpio
	struct gpio_desc *rst_gpio;

	// regulator
	struct regulator *vdda;
	struct regulator *vddi;

	// The device
	struct device *pDev;
	void *pAxiomData;
};

struct u33_CRCData {
	u8 usage;
	u8 revision;
	u8 profile;
	u16 length;
	u32 runtime_crc;
	u32 runtime_nvm_crc;
	u32 bootloader_crc;
	u32 config_nvm_crc;
	u32 config_ram_crc;
	u32 cdu_u05_comments_crc;
	u32 cdu_u22_seqeunces_crc;
	u32 cdu_u43_hotspots_crc;
	u32 cdu_u77_dials_crc;
	u32 cdu_u93_profile_crc;
	u32 cdu_u94_delta_scale_map_crc;
	u32 runtime_hash;
	unsigned char *usage_content;
};

struct axiom_cdev {
	struct cdev ioctl_cdev;
	struct axiom_data_core *axiom_data_core;
};

u8 axiom_get_usage_table_pages(struct axiom_data_core *data_core,
							   u8 id_required);

void axiom_get_dev_info(struct axiom_data_core *data_core, u8 *data);

void print_device_info(struct axiom_data_core *data_core);

u8 axiom_populate_usage_table(struct axiom_data_core *data_core, u8 *pRX_data);

struct usage_Entry *get_usage(struct axiom_data_core *data_core, u8 usage);

u16 usage_to_target_address(struct axiom_data_core *data_core,
							u8 usage, u8 page, u8 offset);

bool axiom_discover(struct axiom_data_core *data_core);

void axiom_rebaseline(struct axiom_data_core *data_core);

void axiom_init_data_core(struct axiom_data_core *data_core, struct device *pDev, void *pAxiomData);

void axiom_remove(struct axiom_data_core *data_core);

void axiom_process_report(struct axiom_data_core *data_core, u8 *pReport);

void axiom_process_u41_report(u8 *rx_buf, struct axiom_data_core *data_core);

void axiom_process_u46_report(u8 *rx_buf, struct axiom_data_core *data_core);

struct input_dev *axiom_register_input_subsystem(struct axiom_data_core *data_core);

void axiom_display_revison(struct axiom_data_core *data_core);

int axiom_dev_init(struct axiom_data_core *data_core);
int axiom_cfg_update(struct axiom_data_core *data_core, unsigned char *buffer, int size);
int compare_CRC_config(struct axiom_data_core *data_core, struct u33_CRCData file_crc_data, struct u33_CRCData device_crc_data);
struct u33_CRCData get_u33_crc_data(struct axiom_data_core *data_core, struct u33_CRCData crc_data);
struct u33_CRCData parse_config_file(struct axiom_data_core *data_core, struct u33_CRCData file_crc_data, unsigned char *buffer, int size);
struct u33_CRCData get_target_crc_data(struct axiom_data_core *data_core, struct u33_CRCData device_crc_data);
void printU33Data(struct axiom_data_core *data_core, struct u33_CRCData crc_data);
void config_write_usage_to_device(struct axiom_data_core *data_core, u8 usage, u16 length, unsigned char *usage_content);
bool ignore_usage_list(struct axiom_data_core *data_core, u8 usage);
bool cdu_usage_list(struct axiom_data_core *data_core, u8 usage);
u16 cdu_write(struct axiom_data_core *data_core, u8 usage, u16 length, u8 *pBuffer);
u16 cdu_store(struct axiom_data_core *data_core, u8 usage, u16 length, u8 *pBuffer);
u16 cdu_commit(struct axiom_data_core *data_core, u8 usage, u16 length);
int axiom_fw_update(struct axiom_data_core *data_core, unsigned char *buffer, int size);
u16 axiom_read_page(void *pAxiomData, u16 target_address, u16 length, u8 *pBuffer);
u16 axiom_write_page(void *pAxiomData, u16 target_address, u16 length, u8 *pBuffer);
bool bootloader_get_busy_status(struct axiom_data_core *data_core);
bool bootloader_reset_axiom(struct axiom_data_core *data_core);
int write_busy_wait(struct axiom_data_core *data_core);
int bootloader_write_chunk(struct axiom_data_core *data_core, u8 *header, u32 length, u8 *payload);
bool enter_bootloader_mode(struct axiom_data_core *data_core);
int system_manager_command(struct axiom_data_core *data_core, int command, int seq, bool skip_verify);
int alc_download(struct axiom_data_core *data_core, unsigned char *buffer, int size);
void axiom_dev_exit(struct axiom_data_core *data_core);
bool axiom_cfg_checksum(struct axiom_data_core *data_core);
void axiom_reset(struct axiom_data_core *data_core);
#endif /* __AXIOM_CORE_H */
