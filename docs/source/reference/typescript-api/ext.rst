############################
Format Registry
############################

The ``ext`` module provides a plugin registry for registering and looking up
coverage database format handlers.

.. code-block:: typescript

   import { defaultRegistry, FormatRegistry, DbFormat } from '@covsight/core';

.. js:autoclass:: FormatRegistry
   :members:

.. js:autoclass:: DbFormat
   :members:

.. js:autoattribute:: defaultRegistry

.. js:autoattribute:: ncdbFormat
