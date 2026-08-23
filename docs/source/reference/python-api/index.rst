##########
Python API
##########

The **covsight-core** Python API is organized into four layers:

1. :doc:`oo-api` — the abstract UCIS object-oriented interface (classes and enums)
2. :doc:`mem-backend` — the in-memory backend and factory
3. :doc:`ncdb` — NCDB reader, writer, and lazy-loading wrapper
4. :doc:`parquet-backend` — the columnar backend, merge, and engine adapters
5. :doc:`utilities` — visitors, merge, conversion, and format registry

.. toctree::
   :maxdepth: 2

   oo-api
   mem-backend
   ncdb
   parquet-backend
   utilities
