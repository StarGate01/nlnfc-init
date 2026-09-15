# nlnfc-init

Primes NXP NPC300 (NXP1001) NFC controllers over Linux NFC netlink with the proprietary NCI config they need for a stable RF connection. Without it, tags are detected but longer data exchanges drop out.

Run as a oneshot at boot and after resume (the config is volatile). Consumers like [ifdnlnfc](https://github.com/jurajsarinay/ifdnlnfc) then just reuse whatever state they find the adapter in — see [issue #2](https://github.com/jurajsarinay/ifdnlnfc/issues/2).

Requires kernel support for forwarding vendor NCI commands over netlink, not yet upstream: [feat/nxp-nci-vendor](https://github.com/StarGate01/linux/tree/feat/nxp-nci-vendor).

## Build

Needs `libnl-3-dev libnl-genl-3-dev` (Debian) or use the provided Nix flake (`nix develop`).

```sh
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

## Usage

```
nlnfc-init [OPTIONS]

  -d, --device INDEX  initialize a specific nfc device index
  -f, --force         skip the NXP1001 sysfs safety check
  -q, --quiet         suppress progress output
  -r, --reset         power-cycle the device before initialization
  -h, --help          display this help
```

Without `--device`, every NXP1001/NPC300 adapter under `/sys/class/nfc` is initialized. `--force` requires `--device`. Only use `--reset` while NFC consumers (e.g. pcscd) are stopped.

## Boot and resume

`/etc/systemd/system/nlnfc-init.service`, then `systemctl enable` it:

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

`/usr/lib/systemd/system-sleep/nlnfc-init-resume.sh`, mode `0755`:

```sh
#!/bin/sh
case "$1/$2" in
	post/suspend|post/hibernate|post/hybrid-sleep|post/suspend-then-hibernate)
		/usr/local/bin/nlnfc-init --quiet || true
		;;
esac

exit 0
```
