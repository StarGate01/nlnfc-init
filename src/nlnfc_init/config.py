"""Parser for nlnfc-init's NCI command config files.

The format is deliberately the same one used by linux_libnfc-nci and
its NixOS module (nixos/modules/hardware/nfc-nci.nix): shell-style
``KEY=value`` lines, byte arrays written as brace-enclosed comma
separated hex, comments starting with ``#``. Each brace-enclosed entry
here must be a *complete* raw NCI command frame -- header byte, OID,
length, payload -- exactly as you'd copy out of an NXP conf file or
nfc-nci.nix, e.g.::

    # Enable chip standby mode
    NXP_CORE_STANDBY = {2F, 00, 01, 01}

Everything needed to issue the equivalent vendor netlink command --
which subcommand, and the payload without the redundant NCI header the
kernel driver reconstructs itself -- is derived from that frame's
header/OID bytes. Non-brace (``KEY=0xNN``) entries, and brace-enclosed
entries that aren't comma-separated hex at all (e.g. upstream's
colon-separated ``NXP_NFC_PROPRIETARY_CFG``, which configures the HAL's
own discovery logic rather than being an NCI frame), belong to the full
userspace NCI stack's own settings and are not meaningful here, so
they're skipped rather than rejected, to keep whole config files
copy-pasteable. A brace-enclosed entry that *does* parse as a byte list
but looks malformed (bad declared length, unrecognized GID/OID) is
reported as an error rather than silently skipped, since that's more
likely a typo in an entry the caller meant for us to send.
"""

import logging
import re
from dataclasses import dataclass

log = logging.getLogger("nlnfc_init")

NCI_GID_CORE = 0x00
NCI_OID_CORE_SET_CONFIG = 0x02
NCI_GID_PROPRIETARY = 0x0F

SUBCMD_CORE_SET_CONFIG = 0
SUBCMD_PROP_CMD = 1

_ENTRY_RE = re.compile(r"(\w+)\s*=\s*(\{[^}]*\}|0x[0-9A-Fa-f]+)", re.DOTALL)


class ConfigError(ValueError):
    pass


@dataclass
class Step:
    name: str
    subcmd: int
    data: bytes


def _parse_byte_array(name, raw):
    tokens = [t.strip() for t in raw.strip("{}").split(",")]
    tokens = [t for t in tokens if t]
    try:
        frame = bytes(int(t, 16) for t in tokens)
    except ValueError:
        # Not comma-separated hex at all (e.g. upstream's colon-separated
        # NXP_NFC_PROPRIETARY_CFG) -- a HAL setting we don't understand,
        # not a malformed NCI frame, so skip rather than error out.
        log.debug("%s: not a comma-separated hex byte list, skipping: %r", name, raw)
        return None
    if len(frame) < 3:
        raise ConfigError(f"{name}: frame too short to be [header, oid, len, ...]: {frame.hex()}")

    header, oid, declared_len = frame[0], frame[1], frame[2]
    payload = frame[3:]
    if declared_len != len(payload):
        raise ConfigError(
            f"{name}: declared length 0x{declared_len:02x} does not match "
            f"{len(payload)} payload bytes: {frame.hex()}"
        )

    gid = header & 0x0F
    if gid == NCI_GID_CORE and oid == NCI_OID_CORE_SET_CONFIG:
        return Step(name, SUBCMD_CORE_SET_CONFIG, payload)
    if gid == NCI_GID_PROPRIETARY:
        return Step(name, SUBCMD_PROP_CMD, bytes([oid]) + payload)
    raise ConfigError(
        f"{name}: frame header 0x{header:02x} oid 0x{oid:02x} is neither a "
        "CORE_SET_CONFIG (GID 0x0, OID 0x2) nor a PROPRIETARY (GID 0xF) "
        "command -- nlnfc-init only knows how to forward those two."
    )


def parse(path):
    """Return an ordered list of Step objects from a config file."""
    text = "\n".join(
        line
        for line in open(path, encoding="utf-8")
        if not line.lstrip().startswith("#")
    )

    steps = []
    for match in _ENTRY_RE.finditer(text):
        name, raw = match.groups()
        if not raw.startswith("{"):
            continue  # a scalar HAL setting, not an NCI frame -- not ours
        step = _parse_byte_array(name, raw)
        if step is not None:
            steps.append(step)
    return steps
