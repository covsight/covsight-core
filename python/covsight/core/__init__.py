"""
covsight-core: UCIS data-model API, in-memory backend, and NCDB format.

Quick-start::

    from covsight.core.mem import MemFactory
    from covsight.core.api import ScopeTypeT, SourceT, CoverData

    db = MemFactory.create()
    cg = db.createScope("my_cg", None, 1, SourceT.SV,
                        ScopeTypeT.COVERGROUP, 0)
    db.close()
"""
