####################
C API
####################

The C API provides a lightweight, ABI-stable interface to NCDB coverage databases.
All types are opaque handles; the implementation is fully hidden behind the public
headers in ``c/include/ncdb/``.

Include the library with:

.. code-block:: c

   #include "ncdb/ncdb.h"

.. toctree::
   :maxdepth: 2

   types
   functions
