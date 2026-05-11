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

exclude_patterns = []
pygments_style = 'sphinx'

html_theme = 'sphinx_rtd_theme'
htmlhelp_basename = 'covsightcoredoc'

graphviz_output_format = 'svg'
