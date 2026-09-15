# nlnfc-init

Sends a sequence of proprietary NCI commands to an NFC controller over Linux NFC generic netlink (`NFC_CMD_VENDOR`), to prime chips that need vendor-specific configuration before they'll hold a stable RF connection. The tool itself is generic — it knows nothing about any specific chip. What to send is read from a config file; which device to send it to is given explicitly. `conf/npc300.conf` is the config for the NXP NPC300 (NXP1001), the one this was written for — see [issue #2](https://github.com/jurajsarinay/ifdnlnfc/issues/2).

Run as a oneshot at boot and after resume (the config is volatile). Consumers like [ifdnlnfc](https://github.com/jurajsarinay/ifdnlnfc) then just reuse whatever state they find the adapter in.

Requires kernel support for forwarding vendor NCI commands over netlink, not yet upstream: [feat/nxp-nci-vendor](https://github.com/StarGate01/linux/tree/feat/nxp-nci-vendor).

## Config file format

Same format used by `linux_libnfc-nci` conf files and nixpkgs' `nixos/modules/hardware/nfc-nci.nix`: `KEY = {byte, byte, ...}` entries, comments starting with `#`. Each entry must be a complete raw NCI command frame — `{header-byte, OID, length, ...payload}` — copy-pasted straight out of an NXP conf file or `nfc-nci.nix`. `nlnfc-init` derives which vendor subcommand to send from the frame's header/OID and strips the header itself (the kernel driver reconstructs it). Non-frame (`KEY = 0xNN`) entries are HAL settings unrelated to this tool and are skipped, so a whole existing conf file can be pointed at without editing it down first. Steps are applied in file order.

## Build / install

Needs [pyroute2](https://github.com/svinota/pyroute2). With Nix + direnv:

```sh
direnv allow
python -m nlnfc_init --list
```

Without Nix:

```sh
python -m venv myenv
source myenv/bin/activate
pip install -e .
nlnfc-init --list
```

## Usage

```
nlnfc-init --list
nlnfc-init --acpi-hid HID --config PATH [--vendor-id HEX] [--reset] [-q|-v]
```

- `--list` — show NFC devices, their kernel netlink index, and (when available) their ACPI hardware ID
- `--acpi-hid` / `--config` — required for priming. `--acpi-hid` identifies the device by its ACPI HID (e.g. `NXP1001`), read from `/sys/class/nfc/nfcN/device/firmware_node/hid` and resolved to a netlink device index at runtime — not by that index directly, since it's just enumeration order and can shift across boots (more adapters showing up, driver/probe-order changes) while the HID stays fixed to the physical chip
- `--vendor-id` — OUI to address (default `0x006037`, NXP)
- `--reset` — power-cycle the device first; only while NFC consumers (e.g. pcscd) are stopped, since the kernel refuses to power down an adapter that's actively polling or has an active target
- `-q/--quiet` — warnings and errors only; `-v/--verbose` — log raw command/response detail

Every step logs its ACK/NACK and, after power-up, the device's reported power/protocol state.

## Boot and resume

`/etc/systemd/system/nlnfc-init.service`, then `systemctl enable` it:

```ini
[Unit]
Description=Prime NFC controller
After=systemd-udev-settle.service
Before=pcscd.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nlnfc-init --acpi-hid NXP1001 --config /etc/nlnfc-init/npc300.conf --quiet
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

`/usr/lib/systemd/system-sleep/nlnfc-init-resume.sh`, mode `0755` — re-runs the tool after suspend/hibernate, since the controller loses its volatile configuration across suspend:

```sh
#!/bin/sh
case "$1/$2" in
	post/suspend|post/hibernate|post/hybrid-sleep|post/suspend-then-hibernate)
		/usr/local/bin/nlnfc-init --device 0 --config /etc/nlnfc-init/npc300.conf --quiet || true
		;;
esac

exit 0
```

See `man systemd-suspend.service` for details on system sleep hooks.
