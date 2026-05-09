import pathlib
import pytest
from covsight.core.mem import MemFactory


@pytest.fixture
def testdata_dir():
    return pathlib.Path(__file__).resolve().parent.parent / "testdata"


@pytest.fixture(params=[("memory", "mem")], ids=["memory"])
def backend(request):
    backend_name, _backend_type = request.param

    def create_db():
        return MemFactory.create()

    def write_db(db, path):
        return db

    def read_db(db_or_path):
        return db_or_path

    yield (backend_name, create_db, write_db, read_db, None)


def assert_coverage_equal(expected, actual, tolerance=0.01):
    assert abs(expected - actual) < tolerance, f"Expected {expected}, got {actual}"


def assert_scope_tree_equal(scope1, scope2):
    assert scope1.getScopeName() == scope2.getScopeName(), (
        f"Scope names differ: {scope1.getScopeName()} != {scope2.getScopeName()}"
    )
    assert scope1.getScopeType() == scope2.getScopeType(), (
        f"Scope types differ: {scope1.getScopeType()} != {scope2.getScopeType()}"
    )
