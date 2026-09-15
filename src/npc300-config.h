/*
 * Proprietary NCI initialization steps for the NXP NPC300 (NXP1001).
 *
 * Copyright (C) 2026 nlnfc-init contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef NPC300_CONFIG_H
#define NPC300_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* NXP Semiconductors OUI, used as the NFC_ATTR_VENDOR_ID for every step. */
#define NXP_NCI_VENDOR_OUI 0x006037

/* NFC_ATTR_VENDOR_SUBCMD values understood by the nxp-nci kernel driver. */
#define NXP_NCI_SUBCMD_CORE_SET_CONFIG 0
#define NXP_NCI_SUBCMD_PROP_CMD        1

struct npc300_config_step {
	const char *name;
	uint32_t subcmd;
	const uint8_t *data;
	size_t data_len;
};

extern const struct npc300_config_step npc300_config_steps[];
extern const size_t npc300_config_steps_count;

#endif /* NPC300_CONFIG_H */
