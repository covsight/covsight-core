"""
history.json — test and merge history serialization.

JSON array of history node records.  Each record encodes the fields
available via the MemHistoryNode API.
"""

import json
from covsight.core.mem.mem_history_node import MemHistoryNode
from covsight.core.api import HistoryNodeKind
from covsight.core.api import TestStatusT


def _kind_to_str(kind) -> str:
    if kind is None:
        return "TEST"
    if isinstance(kind, HistoryNodeKind):
        return kind.name
    # SQLite backend may return bare int
    try:
        return HistoryNodeKind(int(kind)).name
    except (ValueError, TypeError):
        return "TEST"


def _kind_from_str(s: str) -> HistoryNodeKind:
    try:
        return HistoryNodeKind[s]
    except KeyError:
        return HistoryNodeKind.TEST


def _status_to_int(status) -> int:
    if status is None:
        return int(TestStatusT.OK)
    return int(status)


def _status_from_int(v: int):
    try:
        return TestStatusT(v)
    except Exception:
        return TestStatusT.OK


class HistoryWriter:
    """Serialize UCIS history nodes to a JSON bytes object."""

    def serialize(self, history_nodes: list) -> bytes:
        records = []
        for node in history_nodes:
            rec = {
                "logical_name":  node.getLogicalName(),
                "physical_name": node.getPhysicalName(),
                "kind":          _kind_to_str(node.getKind()),
                "test_status":   _status_to_int(node.getTestStatus()),
                "sim_time":      node.getSimTime(),
                "time_unit":     node.getTimeUnit(),
                "run_cwd":       node.getRunCwd(),
                "cpu_time":      node.getCpuTime(),
                "seed":          node.getSeed(),
                "cmd":           node.getCmd(),
                "args":          node.getArgs(),
                "compulsory":    node.getCompulsory(),
                "date":          node.getDate(),
                "user_name":     node.getUserName(),
                "cost":          node.getCost(),
                "tool_category": node.getToolCategory(),
                "ucis_version":  node.getUCISVersion(),
                "vendor_id":     node.getVendorId(),
                "vendor_tool":   node.getVendorTool(),
                "vendor_tool_version": node.getVendorToolVersion(),
                "same_tests":    node.getSameTests(),
                "comment":       node.getComment(),
            }
            records.append(rec)
        return json.dumps(records, indent=2).encode("utf-8")


_NHIS_MAGIC = b"NHIS"
_NHIS_VERSION = 1
_NHIS_STRINGS = (
    "logical_name", "physical_name", "user_name", "seed", "tool_category",
    "comment", "date", "run_cwd", "cmd", "args", "time_unit",
    "vendor_id", "vendor_tool", "vendor_tool_version", "same_tests",
)


def _enc_varint(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F; v >>= 7
        if v: out.append(b | 0x80)
        else: out.append(b); return bytes(out)


def _dec_varint(data: bytes, off: int):
    r = 0; shift = 0
    while True:
        b = data[off]; off += 1
        r |= (b & 0x7F) << shift
        if (b & 0x80) == 0: return r, off
        shift += 7


class HistoryReader:
    """Deserialize history nodes from history.bin bytes (binary NHIS or legacy JSON)."""

    def deserialize(self, data: bytes) -> list:
        if data[:4] == _NHIS_MAGIC:
            return self._deserialize_binary(data)
        records = json.loads(data.decode("utf-8"))
        nodes = []
        for rec in records:
            node = MemHistoryNode(
                parent=None,
                logicalname=rec.get("logical_name", ""),
                physicalname=rec.get("physical_name"),
                kind=_kind_from_str(rec.get("kind", "TEST")),
            )
            node.setTestStatus(_status_from_int(rec.get("test_status", 0)))
            if rec.get("sim_time") is not None:
                node.setSimTime(rec["sim_time"])
            if rec.get("time_unit") is not None:
                node.setTimeUnit(rec["time_unit"])
            if rec.get("run_cwd") is not None:
                node.setRunCwd(rec["run_cwd"])
            if rec.get("cpu_time") is not None:
                node.setCpuTime(rec["cpu_time"])
            if rec.get("seed") is not None:
                node.setSeed(rec["seed"])
            if rec.get("cmd") is not None:
                node.setCmd(rec["cmd"])
            if rec.get("args") is not None:
                node.setArgs(rec["args"])
            if rec.get("compulsory") is not None:
                node.setCompulsory(rec["compulsory"])
            if rec.get("date") is not None:
                node.setDate(rec["date"])
            if rec.get("user_name") is not None:
                node.setUserName(rec["user_name"])
            if rec.get("cost") is not None:
                node.setCost(rec["cost"])
            if rec.get("tool_category") is not None:
                node.setToolCategory(rec["tool_category"])
            if rec.get("vendor_id") is not None:
                node.setVendorId(rec["vendor_id"])
            if rec.get("vendor_tool") is not None:
                node.setVendorTool(rec["vendor_tool"])
            if rec.get("vendor_tool_version") is not None:
                node.setVendorToolVersion(rec["vendor_tool_version"])
            if rec.get("same_tests") is not None:
                node.setSameTests(rec["same_tests"])
            if rec.get("comment") is not None:
                node.setComment(rec["comment"])
            nodes.append(node)
        return nodes

    def _deserialize_binary(self, data: bytes) -> list:
        from struct import unpack
        o = 4
        version = data[o]; o += 1
        if version != _NHIS_VERSION:
            raise ValueError(f"unsupported history binary version {version}")
        n, o = _dec_varint(data, o)
        nodes = []
        for _ in range(n):
            kind_int = data[o]; o += 1
            parent_p1, o = _dec_varint(data, o)
            status, o = _dec_varint(data, o)
            compulsory = data[o]; o += 1
            sim_time, = unpack("<d", data[o:o + 8]); o += 8
            cpu_time, = unpack("<d", data[o:o + 8]); o += 8
            cost,     = unpack("<d", data[o:o + 8]); o += 8
            fields = {}
            for name in _NHIS_STRINGS:
                ln, o = _dec_varint(data, o)
                fields[name] = data[o:o + ln].decode("utf-8") if ln else ""
                o += ln
            node = MemHistoryNode(
                parent=None,
                logicalname=fields["logical_name"],
                physicalname=fields["physical_name"] or None,
                kind=HistoryNodeKind(kind_int) if kind_int in {1, 2} else HistoryNodeKind.TEST,
            )
            node.setTestStatus(_status_from_int(status))
            node.setCompulsory(compulsory)
            node.setSimTime(sim_time)
            node.setCpuTime(cpu_time)
            node.setCost(cost)
            for n_ in ("user_name", "seed", "tool_category", "comment", "date",
                       "run_cwd", "cmd", "args", "time_unit",
                       "vendor_id", "vendor_tool", "vendor_tool_version", "same_tests"):
                if fields[n_]:
                    getattr(node, "set" + "".join(p.capitalize() for p in n_.split("_")))(fields[n_])
            # parent_p1 reserved for future tree linking; current MemHistoryNode is flat
            nodes.append(node)
        return nodes
