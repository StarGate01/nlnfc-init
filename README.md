# nlnfc-init

`nlnfc-init` primes NXP NPC300 (NXP1001) NFC controllers over the Linux NFC netlink interface, applying the proprietary NCI configuration the chip needs for a stable RF connection.

## Why this exists

See [jurajsarinay/ifdnlnfc#2](https://github.com/jurajsarinay/ifdnlnfc/issues/2). Without this configuration the NPC300 detects tags but longer-running data exchanges drop out. Platform/vendor initialization like this does not belong inside a PC/SC IFD driver (it has no business knowing about proprietary EEPROM layouts or voltage configuration), so it lives here instead as a standalone oneshot tool: run it once at boot (and again after resume, since the chip loses its volatile configuration across suspend) and every NFC consumer, including [ifdnlnfc](https://github.com/jurajsarinay/ifdnlnfc), just reuses whatever state it finds the adapter in.

This requires the kernel's `nxp-nci` driver to support forwarding vendor NCI commands over netlink (`NFC_CMD_VENDOR` / `NFC_ATTR_VENDOR_*`), which is not yet upstream; see the kernel patch below.

## Prerequisites

Netlink Protocol Library Suite: <https://www.infradead.org/~tgr/libnl/>

On Debian, install `libnl-3-dev libnl-genl-3-dev`.

## Building

```sh
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

A Nix flake is provided for a development shell with all dependencies (`nix develop`, or `direnv allow` with the included `.envrc`).

## Usage

```
nlnfc-init [OPTIONS]

  -d, --device INDEX  initialize a specific nfc device index
  -f, --force         skip the NXP1001 sysfs safety check
  -q, --quiet         suppress progress output
  -r, --reset         power-cycle the device before initialization
  -h, --help          display this help
```

Without `--device`, every adapter under `/sys/class/nfc` identified as an NXP1001/NPC300 is initialized. `--force` is only accepted together with `--device`, since it bypasses the safety check that keeps this tool from sending NXP-proprietary commands to unrelated hardware.

Use `--reset` only while NFC consumers such as pcscd are stopped: it powers the adapter down first, and the kernel refuses to power down an adapter that is actively polling or has an active target (`-EBUSY`).

## Running at boot and on resume

Install the tool, then create the two files below to run it once at boot and again on resume. Adjust the `/usr/local/bin/nlnfc-init` path in both if you installed the binary somewhere else.

`/etc/systemd/system/nlnfc-init.service` — runs once at boot, ordered before `pcscd.service` so the adapter is already configured by the time ifdnlnfc's IFD driver opens it:

```ini
[Unit]
Description=Prime NXP NPC300/NXP1001 NFC controller
After=systemd-udev-settle.service
Before=pcscd.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nlnfc-init --quiet
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Enable it with `sudo systemctl enable nlnfc-init.service`.

`/usr/lib/systemd/system-sleep/nlnfc-init-resume.sh` (root-owned, mode `0755`) — re-runs the tool after suspend/hibernate, since the controller loses its volatile configuration across suspend:

```sh
#!/bin/sh
case "$1/$2" in
	post/suspend|post/hibernate|post/hybrid-sleep|post/suspend-then-hibernate)
		/usr/local/bin/nlnfc-init --quiet || true
		;;
esac

exit 0
```

See `man systemd-suspend.service` for details on system sleep hooks.

## Kernel patch

This tool depends on `nxp-nci` vendor-command support that is not yet upstream. See the `feat/nxp-nci-vendor` branch of the kernel tree for the required patch (`nxp-nci: Implement netlink vendor command support`, plus the follow-up `nxp-nci: Serialize and validate vendor netlink commands` hardening it).
