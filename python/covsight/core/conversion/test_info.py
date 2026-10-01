"""Stamp test identity (name, seed, status, ...) onto an imported database.

Format importers record whatever test information the source carries, which
for most simulator outputs is little or none.  The caller -- the CLI or a
flow task -- usually knows the real test name, seed and outcome, so this sets
them on the database's single TEST history node, creating one if needed.
"""
from typing import List, Optional, Union

from covsight.core.api import UCIS
from covsight.core.api.enums import HistoryNodeKind, TestStatusT

_STATUS = {
    "ok": TestStatusT.OK, "pass": TestStatusT.OK, "passed": TestStatusT.OK,
    "warning": TestStatusT.WARNING,
    "error": TestStatusT.ERROR, "fail": TestStatusT.ERROR,
    "failed": TestStatusT.ERROR,
    "fatal": TestStatusT.FATAL,
    "missing": TestStatusT.MISSING,
}


def parse_test_status(status: Union[str, bool, TestStatusT]) -> TestStatusT:
    """Accepts a TestStatusT, a bool (pass/fail) or a name such as
    ``ok``/``pass``/``fail``/``error``/``fatal`` (case-insensitive)."""
    if isinstance(status, TestStatusT):
        return status
    if isinstance(status, bool):
        return TestStatusT.OK if status else TestStatusT.ERROR
    try:
        return _STATUS[str(status).strip().lower()]
    except KeyError:
        raise ValueError("unknown test status %r (expected one of %s)" % (
            status, ", ".join(sorted(_STATUS)))) from None


def apply_test_info(db: UCIS,
                    name: Optional[str] = None,
                    seed: Optional[Union[str, int]] = None,
                    status: Optional[Union[str, bool, TestStatusT]] = None,
                    cmd: Optional[str] = None,
                    args: Optional[List[str]] = None,
                    physical_name: Optional[str] = None,
                    cpu_time: Optional[float] = None,
                    sim_time: Optional[float] = None,
                    time_unit: Optional[str] = None,
                    comment: Optional[str] = None):
    """Set test identity on ``db``'s TEST history node and return it.

    Only arguments that are not None are applied.  Raises ValueError when the
    database holds more than one TEST node, since it is then unclear which
    test the information describes.
    """
    tests = list(db.historyNodes(HistoryNodeKind.TEST))
    if len(tests) > 1:
        raise ValueError("database has %d tests; test info applies to "
                         "single-test databases" % len(tests))
    if tests:
        node = tests[0]
    else:
        node = db.createHistoryNode(None, name or "test", physical_name,
                                    HistoryNodeKind.TEST)
        node.setTestStatus(TestStatusT.OK)

    if name is not None:
        node.setLogicalName(name)
    if physical_name is not None:
        node.setPhysicalName(physical_name)
    if seed is not None:
        node.setSeed(str(seed))
    if status is not None:
        node.setTestStatus(parse_test_status(status))
    if cmd is not None:
        node.setCmd(cmd)
    if args is not None:
        node.setArgs(list(args))
    if cpu_time is not None:
        node.setCpuTime(float(cpu_time))
    if sim_time is not None:
        node.setSimTime(float(sim_time))
    if time_unit is not None:
        node.setTimeUnit(time_unit)
    if comment is not None:
        node.setComment(comment)
    return node
