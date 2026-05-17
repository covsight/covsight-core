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
