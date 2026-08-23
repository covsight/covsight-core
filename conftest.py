"""Repo-root pytest configuration.

Its only job is to load the conformance plugin. ``pytest_plugins`` is only
honoured in the rootdir conftest, and the plugin adds a command-line option
(``--conformance-json``), so it has to be registered before argument parsing --
which rules out declaring it in ``tests/conftest.py``.

The plugin is loaded here rather than published as a ``pytest11`` entry point
deliberately: it is repo machinery, and a downstream ``pip install
covsight-core`` should not gain a conformance option or a dependency on the
registry being on disk.
"""

pytest_plugins = ("covsight.core.conformance.marker",)
