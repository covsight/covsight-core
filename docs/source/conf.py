# -*- coding: utf-8 -*-
#
# covsight-core documentation build configuration file

import datetime
import os
import sys

doc_srcdir = os.path.dirname(os.path.abspath(__file__))
rootdir = os.path.dirname(os.path.dirname(doc_srcdir))
sys.path.insert(0, os.path.join(rootdir, 'python'))

os.environ["SPHINX_BUILD"] = "1"

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'sphinx.ext.intersphinx',
    'sphinx.ext.autosectionlabel',
    'sphinx.ext.inheritance_diagram',
    'sphinx_js',
    'hawkmoth',
]

intersphinx_mapping = {
    "python": ('https://docs.python.org/3', None),
}

templates_path = ['_templates']
source_suffix = '.rst'
master_doc = 'index'

project = u'covsight-core'
copyright = u'2019-{0}, Matthew Ballance and Contributors'.format(
    datetime.datetime.now().year)
release = "0.0.1"
version = "0.0"

autoclass_content = "both"
autodoc_member_order = "bysource"
autosectionlabel_prefix_document = True

# Hawkmoth: the C++ reference in reference/cpp-api/ is extracted from the
# writer's own headers, so the published API cannot drift from the shipped one.
#
# It reads cpp/ucis-xml/include/ucis_xml.hpp -- the generated single header that
# consumers actually vendor, not the split sources -- so what is documented is
# what is delivered. Only `/**` comments are extracted; internals use plain
# `//` and stay out of the reference.
hawkmoth_root = os.path.join(rootdir, 'cpp', 'ucis-xml', 'include')
# Ask the local compiler where the standard headers are, rather than hardcoding
# paths: libclang ships no builtin headers of its own, so without this every
# `#include <cstdint>` fails and hawkmoth silently emits nothing.
hawkmoth_compiler = 'g++'
hawkmoth_clang_cpp = ['-std=c++17']

# sphinx-js configuration
js_language = 'typescript'
js_source_path = os.path.join(rootdir, 'ts', 'src')
jsdoc_tsconfig_path = os.path.join(rootdir, 'ts', 'tsconfig.json')
# Tell sphinx-js where to find node_modules (typedoc lives in ts/node_modules)
os.environ.setdefault("SPHINX_JS_NODE_MODULES", os.path.join(rootdir, 'ts', 'node_modules'))

exclude_patterns = []
pygments_style = 'sphinx'

html_theme = 'sphinx_rtd_theme'
htmlhelp_basename = 'covsightcoredoc'

graphviz_output_format = 'svg'
