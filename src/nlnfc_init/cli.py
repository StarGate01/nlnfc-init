import argparse
import errno
import glob
import logging
import os
import sys

from nlnfc_init import config
from nlnfc_init.netlink import NFCCommandError, NFCSocket

log = logging.getLogger("nlnfc_init")

DEFAULT_VENDOR_ID = 0x006037  # NXP Semiconductors OUI

SYSFS_NFC_CLASS = "/sys/class/nfc"


def _acpi_hid(nfc_index):
    """Return the ACPI hardware ID backing an nfc<N> device, or None.

    The netlink device index itself is just enumeration order -- it can
    shift across boots if more adapters show up, or after a kernel/driver
    change. The ACPI HID is the chip's actual identity as wired into the
    board, so it's what --acpi-hid matches against instead.
    """
    hid_path = os.path.join(SYSFS_NFC_CLASS, f"nfc{nfc_index}", "device", "firmware_node", "hid")
    try:
        with open(hid_path, encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return None


def resolve_device(acpi_hid):
    """Return the netlink device index whose ACPI HID matches acpi_hid."""
    matches = []
    for entry in sorted(glob.glob(os.path.join(SYSFS_NFC_CLASS, "nfc*"))):
        index = int(os.path.basename(entry)[len("nfc") :])
        if _acpi_hid(index) == acpi_hid:
            matches.append(index)
    if not matches:
        raise LookupError(
            f"no NFC device with ACPI HID {acpi_hid!r} found under {SYSFS_NFC_CLASS} (see --list)"
        )
    if len(matches) > 1:
        raise LookupError(f"multiple NFC devices with ACPI HID {acpi_hid!r}: nfc{matches}")
    return matches[0]


def cmd_list():
    with NFCSocket() as sock:
        sock.open()
        found = False
        for index, name, powered, protocols in sock.list_devices():
            found = True
            hid = _acpi_hid(index) or "?"
            print(
                f"nfc{index}: {name or '?'}  acpi-hid={hid}  "
                f"powered={bool(powered)} protocols=0x{protocols or 0:02x}"
            )
        if not found:
            print("no NFC devices found", file=sys.stderr)
            return 1
    return 0


def prime_device(sock, device, steps, vendor_id, reset):
    if reset:
        try:
            sock.dev_down(device)
        except NFCCommandError as err:
            if err.code == errno.EBUSY:
                log.error("device is busy -- stop NFC consumers such as pcscd before using --reset")
                return 1
            if err.code != errno.EALREADY:
                log.error("reset failed: %s", err)
                return 1

    try:
        sock.dev_up(device)
    except NFCCommandError as err:
        if err.code != errno.EALREADY:
            log.error("power-up failed: %s", err)
            return 1

    sock.get_device(device)

    for step in steps:
        try:
            sock.vendor_cmd(device, vendor_id, step.subcmd, step.data, step.name)
        except NFCCommandError as err:
            log.error("%s failed: %s", step.name, err)
            if err.code in (errno.EOPNOTSUPP, errno.ENODEV):
                log.error("the kernel may lack nxp-nci vendor-command support")
            return 1

    log.info("nfc%d: initialization complete (%d steps)", device, len(steps))
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Prime an NFC controller with a proprietary NCI command sequence."
    )
    parser.add_argument("--list", action="store_true", help="list NFC devices and exit")
    parser.add_argument(
        "--acpi-hid",
        help="ACPI hardware ID of the NFC device to initialize, e.g. NXP1001 (see --list)",
    )
    parser.add_argument("--config", help="NCI command config file (see conf/npc300.conf)")
    parser.add_argument(
        "--vendor-id",
        type=lambda s: int(s, 0),
        default=DEFAULT_VENDOR_ID,
        help=f"vendor OUI to address (default: 0x{DEFAULT_VENDOR_ID:06x}, NXP)",
    )
    parser.add_argument(
        "--reset", action="store_true", help="power-cycle the device before initializing it"
    )
    parser.add_argument("-q", "--quiet", action="store_true", help="only log warnings and errors")
    parser.add_argument("-v", "--verbose", action="store_true", help="log raw command/response detail")
    args = parser.parse_args()

    # Configure only our own logger, not the root logger: -v is meant to
    # show *our* per-command decisions, not pyroute2's internal socket
    # chatter, which logs at DEBUG under its own loggers.
    level = logging.WARNING if args.quiet else logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(format="%(message)s")
    log.setLevel(level)

    if args.list:
        return cmd_list()

    if not args.acpi_hid or not args.config:
        parser.error("--acpi-hid and --config are required (or pass --list)")

    try:
        steps = config.parse(args.config)
    except (OSError, config.ConfigError) as err:
        parser.error(str(err))
    if not steps:
        log.warning("%s: no NCI command frames found", args.config)

    try:
        device = resolve_device(args.acpi_hid)
    except LookupError as err:
        parser.error(str(err))

    with NFCSocket() as sock:
        sock.open()
        return prime_device(sock, device, steps, args.vendor_id, args.reset)
