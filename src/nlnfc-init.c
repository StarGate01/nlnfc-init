/*
 * Prime a Lenovo NPC300 (NXP1001) NFC controller through Linux NFC netlink.
 *
 * Platform/vendor NCI initialization does not belong in a PC/SC IFD driver:
 * it should run once, out of band, before any consumer (pcscd, ...) opens
 * the adapter. This tool is meant to run as a oneshot at boot and again on
 * resume from suspend; see the systemd units under systemd/.
 *
 * Copyright (C) 2026 nlnfc-init contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "npc300-config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <linux/nfc.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * NFC_CMD_VENDOR / NFC_ATTR_VENDOR_* were added to the kernel's NFC netlink
 * uapi well before the nxp-nci vendor-command support this tool relies on,
 * but some distros still ship kernel headers predating them. These values
 * are part of the stable netlink ABI (enum members are only ever appended,
 * never renumbered), so it's safe to supply them here when missing.
 */
#ifndef NFC_CMD_VENDOR
#define NFC_CMD_VENDOR 29
#endif
#ifndef NFC_ATTR_VENDOR_ID
#define NFC_ATTR_VENDOR_ID 29
#endif
#ifndef NFC_ATTR_VENDOR_SUBCMD
#define NFC_ATTR_VENDOR_SUBCMD 30
#endif
#ifndef NFC_ATTR_VENDOR_DATA
#define NFC_ATTR_VENDOR_DATA 31
#endif
#include <stdlib.h>
#include <string.h>

#define NFC_SYSFS_ROOT "/sys/class/nfc"
#define NPC300_ACPI_ID "NXP1001"
#define SYSFS_PATH_SIZE 512

struct nfc_context {
	struct nl_sock *socket;
	int family_id;
};

struct request_state {
	int done;
	int error;
};

struct options {
	uint32_t device_index;
	bool device_selected;
	bool force;
	bool quiet;
	bool reset;
};

static int error_handler(struct sockaddr_nl *address, struct nlmsgerr *error,
		void *arg)
{
	struct request_state *state = arg;

	(void)address;
	state->error = error->error;
	state->done = 1;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *message, void *arg)
{
	struct request_state *state = arg;

	(void)message;
	state->done = 1;
	return NL_SKIP;
}

static int send_request(struct nfc_context *context, struct nl_msg *message)
{
	struct request_state state = {0};
	struct nl_cb *callbacks;
	int error;

	callbacks = nl_cb_alloc(NL_CB_DEFAULT);
	if (!callbacks)
		return -ENOMEM;

	error = nl_send_auto_complete(context->socket, message);
	if (error < 0) {
		fprintf(stderr, "unable to send netlink request: %s\n",
			nl_geterror(error));
		error = -EIO;
		goto out;
	}

	nl_cb_err(callbacks, NL_CB_CUSTOM, error_handler, &state);
	nl_cb_set(callbacks, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &state);
	nl_cb_set(callbacks, NL_CB_ACK, NL_CB_CUSTOM, finish_handler, &state);

	while (!state.done) {
		error = nl_recvmsgs(context->socket, callbacks);
		if (error < 0) {
			fprintf(stderr, "unable to receive netlink response: %s\n",
				nl_geterror(error));
			error = -EIO;
			goto out;
		}
	}

	error = state.error;

out:
	nl_cb_put(callbacks);
	return error;
}

static struct nl_msg *new_request(const struct nfc_context *context,
		uint8_t command, uint32_t device_index)
{
	struct nl_msg *message;

	message = nlmsg_alloc();
	if (!message)
		return NULL;

	if (!genlmsg_put(message, NL_AUTO_PID, NL_AUTO_SEQ, context->family_id,
		0, NLM_F_REQUEST, command, NFC_GENL_VERSION) ||
		nla_put_u32(message, NFC_ATTR_DEVICE_INDEX, device_index)) {
		nlmsg_free(message);
		return NULL;
	}

	return message;
}

static int set_device_power(struct nfc_context *context, uint32_t device_index,
		bool powered)
{
	struct nl_msg *message;
	int error;

	message = new_request(context,
		powered ? NFC_CMD_DEV_UP : NFC_CMD_DEV_DOWN, device_index);
	if (!message)
		return -ENOMEM;

	error = send_request(context, message);
	nlmsg_free(message);
	return error;
}

static int send_vendor_command(struct nfc_context *context,
		uint32_t device_index, const struct npc300_config_step *step)
{
	struct nl_msg *message;
	int error;

	if (!step || !step->data || !step->data_len || step->data_len > INT_MAX)
		return -EINVAL;

	message = new_request(context, NFC_CMD_VENDOR, device_index);
	if (!message)
		return -ENOMEM;

	if (nla_put_u32(message, NFC_ATTR_VENDOR_ID, NXP_NCI_VENDOR_OUI) ||
		nla_put_u32(message, NFC_ATTR_VENDOR_SUBCMD, step->subcmd) ||
		nla_put(message, NFC_ATTR_VENDOR_DATA, (int)step->data_len,
			step->data)) {
		nlmsg_free(message);
		return -EMSGSIZE;
	}

	error = send_request(context, message);
	nlmsg_free(message);
	return error;
}

static int open_nfc_context(struct nfc_context *context)
{
	int error;

	memset(context, 0, sizeof(*context));
	context->family_id = -1;
	context->socket = nl_socket_alloc();
	if (!context->socket)
		return -ENOMEM;

	error = genl_connect(context->socket);
	if (error < 0) {
		fprintf(stderr, "unable to connect to generic netlink: %s\n",
			nl_geterror(error));
		return -EIO;
	}

	context->family_id = genl_ctrl_resolve(context->socket, NFC_GENL_NAME);
	if (context->family_id < 0) {
		fprintf(stderr, "unable to resolve the Linux NFC netlink family: %s\n",
			nl_geterror(context->family_id));
		return -ENOENT;
	}

	return 0;
}

static void close_nfc_context(struct nfc_context *context)
{
	if (context->socket)
		nl_socket_free(context->socket);

	context->socket = NULL;
	context->family_id = -1;
}

static bool file_contains(const char *path, const char *needle)
{
	char line[256];
	FILE *file;

	file = fopen(path, "r");
	if (!file)
		return false;

	while (fgets(line, sizeof(line), file)) {
		if (strstr(line, needle)) {
			fclose(file);
			return true;
		}
	}

	fclose(file);
	return false;
}

static bool device_is_npc300(uint32_t device_index)
{
	static const char *const attributes[] = {
		"device/firmware_node/hid",
		"device/modalias",
		"device/uevent",
	};
	char path[SYSFS_PATH_SIZE];
	size_t i;

	for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
		int length = snprintf(path, sizeof(path), "%s/nfc%u/%s",
			NFC_SYSFS_ROOT, device_index, attributes[i]);

		if (length < 0 || (size_t)length >= sizeof(path))
			continue;
		if (file_contains(path, NPC300_ACPI_ID))
			return true;
	}

	return false;
}

static bool parse_device_name(const char *name, uint32_t *device_index)
{
	char *end;
	unsigned long value;

	if (strncmp(name, "nfc", 3) || !isdigit((unsigned char)name[3]))
		return false;

	errno = 0;
	value = strtoul(name + 3, &end, 10);
	if (errno || *end || value > UINT32_MAX)
		return false;

	*device_index = (uint32_t)value;
	return true;
}

static void report_command_error(uint32_t device_index, const char *operation,
		int error)
{
	fprintf(stderr, "nfc%u: %s failed: %s (%d)\n", device_index,
		operation, strerror(-error), error);
}

static int prime_device(struct nfc_context *context,
		const struct options *options, uint32_t device_index)
{
	size_t i;
	int error;

	if (options->reset) {
		error = set_device_power(context, device_index, false);
		if (error && error != -EALREADY) {
			report_command_error(device_index, "reset", error);
			if (error == -EBUSY)
				fprintf(stderr,
					"stop NFC consumers before using --reset\n");
			return error;
		}
	}

	error = set_device_power(context, device_index, true);
	if (error && error != -EALREADY) {
		report_command_error(device_index, "power-up", error);
		return error;
	}

	for (i = 0; i < npc300_config_steps_count; i++) {
		const struct npc300_config_step *step = &npc300_config_steps[i];

		if (!options->quiet)
			printf("nfc%u: applying %s\n", device_index, step->name);

		error = send_vendor_command(context, device_index, step);
		if (error) {
			report_command_error(device_index, step->name, error);
			if (error == -EOPNOTSUPP || error == -ENODEV)
				fprintf(stderr,
					"the kernel may lack nxp-nci vendor-command support\n");
			return error;
		}
	}

	if (!options->quiet)
		printf("nfc%u: NPC300 initialization complete\n", device_index);

	return 0;
}

static int prime_detected_devices(struct nfc_context *context,
		const struct options *options)
{
	struct dirent *entry;
	DIR *directory;
	size_t found = 0;
	int result = 0;

	directory = opendir(NFC_SYSFS_ROOT);
	if (!directory) {
		int error = -errno;

		fprintf(stderr, "unable to open %s: %s\n", NFC_SYSFS_ROOT,
			strerror(-error));
		return error;
	}

	while ((entry = readdir(directory))) {
		uint32_t device_index;
		int error;

		if (!parse_device_name(entry->d_name, &device_index) ||
			!device_is_npc300(device_index))
			continue;

		found++;
		error = prime_device(context, options, device_index);
		if (error && !result)
			result = error;
	}

	closedir(directory);
	if (!found) {
		fprintf(stderr, "no %s/NPC300 NFC device found\n", NPC300_ACPI_ID);
		return -ENODEV;
	}

	return result;
}

static void usage(FILE *stream, const char *program)
{
	fprintf(stream,
		"Usage: %s [OPTIONS]\n"
		"Prime Lenovo NXP1001/NPC300 NFC controllers.\n\n"
		"  -d, --device INDEX  initialize a specific nfc device index\n"
		"  -f, --force         skip the NXP1001 sysfs safety check\n"
		"  -q, --quiet         suppress progress output\n"
		"  -r, --reset         power-cycle the device before initialization\n"
		"  -h, --help          display this help\n\n"
		"Without --device, every NXP1001 adapter found in %s is initialized.\n"
		"Use --reset only while NFC consumers such as pcscd are stopped.\n",
		program, NFC_SYSFS_ROOT);
}

static int parse_device_index(const char *text, uint32_t *device_index)
{
	char *end;
	unsigned long value;

	if (!text[0] || !isdigit((unsigned char)text[0]))
		return -EINVAL;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || *end || value > UINT32_MAX)
		return -EINVAL;

	*device_index = (uint32_t)value;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *options)
{
	static const struct option long_options[] = {
		{"device", required_argument, NULL, 'd'},
		{"force", no_argument, NULL, 'f'},
		{"help", no_argument, NULL, 'h'},
		{"quiet", no_argument, NULL, 'q'},
		{"reset", no_argument, NULL, 'r'},
		{NULL, 0, NULL, 0},
	};
	int option;

	memset(options, 0, sizeof(*options));
	while ((option = getopt_long(argc, argv, "d:fhqr", long_options,
		NULL)) != -1) {
		switch (option) {
		case 'd':
			if (parse_device_index(optarg, &options->device_index))
				return -EINVAL;
			options->device_selected = true;
			break;
		case 'f':
			options->force = true;
			break;
		case 'h':
			usage(stdout, argv[0]);
			exit(EXIT_SUCCESS);
		case 'q':
			options->quiet = true;
			break;
		case 'r':
			options->reset = true;
			break;
		default:
			return -EINVAL;
		}
	}

	if (optind != argc || (options->force && !options->device_selected))
		return -EINVAL;

	return 0;
}

int main(int argc, char **argv)
{
	struct nfc_context context;
	struct options options;
	int error;

	if (parse_options(argc, argv, &options)) {
		usage(stderr, argv[0]);
		return EXIT_FAILURE;
	}

	if (options.device_selected && !options.force &&
		!device_is_npc300(options.device_index)) {
		fprintf(stderr,
			"nfc%u is not identified as an %s/NPC300; use --force to override\n",
			options.device_index, NPC300_ACPI_ID);
		return EXIT_FAILURE;
	}

	error = open_nfc_context(&context);
	if (error) {
		close_nfc_context(&context);
		return EXIT_FAILURE;
	}

	if (options.device_selected)
		error = prime_device(&context, &options, options.device_index);
	else
		error = prime_detected_devices(&context, &options);

	close_nfc_context(&context);
	return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
