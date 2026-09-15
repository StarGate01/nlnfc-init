"""Minimal Linux NFC generic-netlink client.

Describes just enough of the kernel's "nfc" genl family (see
include/uapi/linux/nfc.h) to power up an adapter and issue vendor
commands against it. Attribute/command numbering is taken directly
from the upstream enum, verified against the actual running kernel
this tool was developed against.
"""

import logging
import os

from pyroute2.netlink import NLM_F_ACK, NLM_F_DUMP, NLM_F_REQUEST, genlmsg
from pyroute2.netlink.exceptions import NetlinkError
from pyroute2.netlink.generic import GenericNetlinkSocket

log = logging.getLogger("nlnfc_init")

NFC_GENL_NAME = "nfc"
NFC_GENL_VERSION = 1

NFC_CMD_GET_DEVICE = 1
NFC_CMD_DEV_UP = 2
NFC_CMD_DEV_DOWN = 3
NFC_CMD_VENDOR = 29

# enum nfc_attrs, include/uapi/linux/nfc.h -- position in this tuple *is*
# the wire attribute type, so it must stay complete and in order even
# though this tool only ever reads/writes a handful of these.
NFC_ATTR_MAP = (
    ("NFC_ATTR_UNSPEC", "none"),
    ("NFC_ATTR_DEVICE_INDEX", "uint32"),
    ("NFC_ATTR_DEVICE_NAME", "asciiz"),
    ("NFC_ATTR_PROTOCOLS", "uint32"),
    ("NFC_ATTR_TARGET_INDEX", "uint32"),
    ("NFC_ATTR_TARGET_SENS_RES", "hex"),
    ("NFC_ATTR_TARGET_SEL_RES", "hex"),
    ("NFC_ATTR_TARGET_NFCID1", "hex"),
    ("NFC_ATTR_TARGET_SENSB_RES", "hex"),
    ("NFC_ATTR_TARGET_SENSF_RES", "hex"),
    ("NFC_ATTR_COMM_MODE", "uint8"),
    ("NFC_ATTR_RF_MODE", "uint8"),
    ("NFC_ATTR_DEVICE_POWERED", "uint8"),
    ("NFC_ATTR_IM_PROTOCOLS", "uint32"),
    ("NFC_ATTR_TM_PROTOCOLS", "uint32"),
    ("NFC_ATTR_LLC_PARAM_LTO", "uint8"),
    ("NFC_ATTR_LLC_PARAM_RW", "uint8"),
    ("NFC_ATTR_LLC_PARAM_MIUX", "uint16"),
    ("NFC_ATTR_SE", "hex"),
    ("NFC_ATTR_LLC_SDP", "hex"),
    ("NFC_ATTR_FIRMWARE_NAME", "asciiz"),
    ("NFC_ATTR_SE_INDEX", "uint32"),
    ("NFC_ATTR_SE_TYPE", "uint8"),
    ("NFC_ATTR_SE_AID", "hex"),
    ("NFC_ATTR_FIRMWARE_DOWNLOAD_STATUS", "uint8"),
    ("NFC_ATTR_SE_APDU", "hex"),
    ("NFC_ATTR_TARGET_ISO15693_DSFID", "uint8"),
    ("NFC_ATTR_TARGET_ISO15693_UID", "hex"),
    ("NFC_ATTR_SE_PARAMS", "hex"),
    ("NFC_ATTR_VENDOR_ID", "uint32"),
    ("NFC_ATTR_VENDOR_SUBCMD", "uint32"),
    ("NFC_ATTR_VENDOR_DATA", "hex"),
    ("NFC_ATTR_TARGET_ATS", "hex"),
)


class nfcmsg(genlmsg):
    prefix = "NFC_ATTR_"
    nla_map = NFC_ATTR_MAP


class NFCCommandError(RuntimeError):
    """A command was NACKed by the kernel."""

    def __init__(self, label, code, strerror):
        super().__init__(f"{label}: {strerror} ({code})")
        self.label = label
        self.code = code
        self.strerror = strerror


class NFCSocket(GenericNetlinkSocket):
    """A bound, logging netlink socket for the "nfc" genl family."""

    def open(self):
        self.bind(NFC_GENL_NAME, nfcmsg)

    def _request(self, label, cmd, attrs, dump=False):
        msg = nfcmsg()
        msg["cmd"] = cmd
        msg["version"] = NFC_GENL_VERSION
        msg["attrs"] = list(attrs)
        flags = NLM_F_REQUEST | (NLM_F_DUMP if dump else NLM_F_ACK)
        log.debug("-> %s: cmd=%d attrs=%s", label, cmd, attrs)
        try:
            replies = self.nlm_request(msg, msg_type=self.prid, msg_flags=flags)
        except NetlinkError as err:
            strerror = os.strerror(err.code)
            log.info("<- %s: NACK %s (%d)", label, strerror, err.code)
            raise NFCCommandError(label, err.code, strerror) from err
        log.info("<- %s: ACK", label)
        return replies

    def get_device(self, index):
        """Return (powered, protocols) for a device, or None if absent."""
        for reply in self._request(
            f"GET_DEVICE(index={index})",
            NFC_CMD_GET_DEVICE,
            [("NFC_ATTR_DEVICE_INDEX", index)],
            dump=True,
        ):
            if reply.get_attr("NFC_ATTR_DEVICE_INDEX") != index:
                continue
            powered = reply.get_attr("NFC_ATTR_DEVICE_POWERED")
            protocols = reply.get_attr("NFC_ATTR_PROTOCOLS")
            log.info(
                "   device state: powered=%s protocols=0x%02x",
                powered,
                protocols or 0,
            )
            return powered, protocols
        log.warning("   device %d not found", index)
        return None

    def list_devices(self):
        """Yield (index, name, powered, protocols) for every NFC device."""
        for reply in self._request(
            "GET_DEVICE(dump)", NFC_CMD_GET_DEVICE, [], dump=True
        ):
            yield (
                reply.get_attr("NFC_ATTR_DEVICE_INDEX"),
                reply.get_attr("NFC_ATTR_DEVICE_NAME"),
                reply.get_attr("NFC_ATTR_DEVICE_POWERED"),
                reply.get_attr("NFC_ATTR_PROTOCOLS"),
            )

    def dev_up(self, index):
        self._request(
            f"DEV_UP(index={index})",
            NFC_CMD_DEV_UP,
            [("NFC_ATTR_DEVICE_INDEX", index)],
        )

    def dev_down(self, index):
        self._request(
            f"DEV_DOWN(index={index})",
            NFC_CMD_DEV_DOWN,
            [("NFC_ATTR_DEVICE_INDEX", index)],
        )

    def vendor_cmd(self, index, vendor_id, subcmd, data, label):
        self._request(
            f"VENDOR({label}, subcmd={subcmd}, {len(data)}B)",
            NFC_CMD_VENDOR,
            [
                ("NFC_ATTR_DEVICE_INDEX", index),
                ("NFC_ATTR_VENDOR_ID", vendor_id),
                ("NFC_ATTR_VENDOR_SUBCMD", subcmd),
                ("NFC_ATTR_VENDOR_DATA", data),
            ],
        )
