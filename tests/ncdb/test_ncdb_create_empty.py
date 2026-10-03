"""An NcdbUCIS with no file behind it is an empty, writable database.

The format plugin's ``create()`` returns ``NcdbUCIS(None)``; ``covsight merge``
merges into it, which used to fail reading a ZIP from path ``None``.
"""

import os

from covsight.core.api import ScopeTypeT, SourceT
from covsight.core.ext import FormatRegistry
from covsight.core.merge import DbMerger
from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_ucis import NcdbUCIS
from covsight.core.ncdb.ncdb_writer import NcdbWriter


def _src_db():
    db = MemUCIS()
    du = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE, du, 0)
    return db


def test_create_without_path_is_empty():
    db = FormatRegistry().get_db_format("ncdb").fmt_if.create()
    assert list(db.scopes(ScopeTypeT.ALL)) == []
    assert list(db.historyNodes(-1)) == []


def test_create_with_unwritten_path_is_empty(tmp_path):
    db = NcdbUCIS(str(tmp_path / "new.ncdb"))
    assert list(db.scopes(ScopeTypeT.ALL)) == []


def test_merge_into_created_db_and_write(tmp_path):
    out = FormatRegistry().get_db_format("ncdb").fmt_if.create()
    DbMerger().merge(out, [_src_db(), _src_db()])
    path = str(tmp_path / "merged.ncdb")
    NcdbWriter().write(out, path)
    assert os.path.isfile(path)
    names = [s.getScopeName() for s in NcdbReader().read(path).scopes(ScopeTypeT.INSTANCE)]
    assert names == ["top"]
