/*
 * Proprietary NCI initialization steps for the NXP NPC300 (NXP1001).
 *
 * These CORE_SET_CONFIG and proprietary-command payloads stabilize the RF
 * connection after CORE_INIT; without them the chip detects tags but longer
 * data exchanges drop out due to misconfigured power/RF parameters. See
 * https://github.com/jurajsarinay/ifdnlnfc/issues/2 and
 * https://github.com/nfc-tools/libnfc/issues/455#issuecomment-2221979571 .
 *
 * Copyright (C) 2026 nlnfc-init contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "npc300-config.h"

#define NXP_NCI_OID_SET_POWER_MODE 0x00
#define NXP_NCI_OID_PROPRIETARY_ACT 0x02

#define NXP_NCI_PARAM_PMU_CFG                       0xA0, 0x0E
#define NXP_NCI_PARAM_DH_EEPROM_AREA_1               0xA0, 0x0F
#define NXP_NCI_PARAM_TAG_DETECTOR_CFG                0xA0, 0x40
#define NXP_NCI_PARAM_TAG_DETECTOR_FALLBACK_CNT_CFG   0xA0, 0x43
#define NXP_NCI_PARAM_JEWEL_RID_CFG                   0xA0, 0x5E

#define NXP_NCI_PMU_VBAT1_5V           0x16
#define NXP_NCI_PMU_TVDD_3V6_TXLDO_3V3 0x09
#define NXP_NCI_PMU_RFU                0x00

#define NXP_NCI_TLV(id, ...) \
	id, (uint8_t)(sizeof((uint8_t[]){__VA_ARGS__})), __VA_ARGS__

/* PROPRIETARY_ACT (OID 0x02): unlocks the proprietary CORE_SET_CONFIG
 * parameters used below. Carries no payload beyond the OID. */
static const uint8_t nxp_nci_act_prop_extn[] = {
	NXP_NCI_OID_PROPRIETARY_ACT,
};

/* PMU/TVDD voltage configuration. */
static const uint8_t nxp_nci_tvdd_cfg[] = {
	1,
	NXP_NCI_TLV(NXP_NCI_PARAM_PMU_CFG,
		NXP_NCI_PMU_VBAT1_5V, NXP_NCI_PMU_TVDD_3V6_TXLDO_3V3, NXP_NCI_PMU_RFU),
};

/* Tag detector and DH EEPROM area configuration. */
static const uint8_t nxp_nci_core_ext_cfg[] = {
	4,
	NXP_NCI_TLV(NXP_NCI_PARAM_JEWEL_RID_CFG, 1),
	NXP_NCI_TLV(NXP_NCI_PARAM_TAG_DETECTOR_CFG, 0),
	NXP_NCI_TLV(NXP_NCI_PARAM_TAG_DETECTOR_FALLBACK_CNT_CFG, 0),
	NXP_NCI_TLV(NXP_NCI_PARAM_DH_EEPROM_AREA_1,
		0x00, 0x03, 0x1D, 0x01, 0x03, 0x00, 0x02, 0x00,
		0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
};

/* SET_POWER_MODE (OID 0x00): put the chip into standby, ready for
 * discovery to start. */
static const uint8_t nxp_nci_core_standby[] = {
	NXP_NCI_OID_SET_POWER_MODE, 1,
};

const struct npc300_config_step npc300_config_steps[] = {
	{
		.name = "proprietary activation extension",
		.subcmd = NXP_NCI_SUBCMD_PROP_CMD,
		.data = nxp_nci_act_prop_extn,
		.data_len = sizeof(nxp_nci_act_prop_extn),
	},
	{
		.name = "PMU/TVDD voltage configuration",
		.subcmd = NXP_NCI_SUBCMD_CORE_SET_CONFIG,
		.data = nxp_nci_tvdd_cfg,
		.data_len = sizeof(nxp_nci_tvdd_cfg),
	},
	{
		.name = "tag detector / EEPROM configuration",
		.subcmd = NXP_NCI_SUBCMD_CORE_SET_CONFIG,
		.data = nxp_nci_core_ext_cfg,
		.data_len = sizeof(nxp_nci_core_ext_cfg),
	},
	{
		.name = "standby power mode",
		.subcmd = NXP_NCI_SUBCMD_PROP_CMD,
		.data = nxp_nci_core_standby,
		.data_len = sizeof(nxp_nci_core_standby),
	},
};

const size_t npc300_config_steps_count =
	sizeof(npc300_config_steps) / sizeof(npc300_config_steps[0]);
