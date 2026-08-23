import pytest

from covsight.core.conformance import catalog, claims, registry


@pytest.fixture(scope="session")
def reg():
    return registry.load()


@pytest.fixture(scope="session")
def catalog_sections():
    return catalog.parse()


@pytest.fixture(scope="session")
def doc_claims():
    return claims.parse()
