#############
API reference
#############

Extracted by `Hawkmoth <https://hawkmoth.readthedocs.io/>`_ from
``cpp/ucis-xml/include/ucis_xml.hpp`` — the generated single header consumers
vendor — so what is documented here is what is delivered. Nothing on this page
is hand-maintained: to change it, change the comment in the source.

For a guided introduction start with :doc:`quickstart`; for what the writer
asks of you, :doc:`contract`; for one section per coverage kind,
:doc:`coverage-kinds`.

Conventions
===========

Everything lives in namespace ``ucisxml``, which the examples alias to ``ux``.
Rename it with ``UCIS_XML_NAMESPACE`` if you need two versions of the header in
one binary.

Every string parameter takes :cpp:struct:`ucisxml::Text`, which converts
implicitly from ``const char*``, from ``(pointer, length)``, and from
``std::string`` unless ``UCIS_XML_NO_STL`` is set. Caller strings are copied
before the call returns, so a ``Text`` never has to outlive the call it appears
in.

Handles returned by the recording calls — :cpp:class:`ucisxml::Scope`,
:cpp:class:`ucisxml::Branch`, :cpp:class:`ucisxml::BinRef` and the rest — are
*inert* rather than null when something has failed. Every call on an inert
handle does nothing and returns another inert handle, so a chain of calls never
has to be guarded and an error latched early cannot turn into a crash later.

.. note::

   **Default arguments are absent from the signatures below.** The extractor
   drops them, so every defaulted parameter states its default in its own
   description instead — which has room to say what the default *means*.

.. note::

   Only the public API appears here. Internals are commented with plain ``//``,
   which the extractor does not read, so private members and the whole
   ``ucisxml::stage`` namespace stay out of the reference by construction
   rather than by a filter that could be forgotten.

Reference
=========

The order below is the header's own: vocabulary types, then output, then errors
and options, then the descriptors, then the recording handles, then
:cpp:class:`ucisxml::Scope` and :cpp:class:`ucisxml::CoverageWriter`, and
finally the flat path.

.. cpp:autodoc:: ucis_xml.hpp

.. seealso::

   Every :cpp:enum:`ucisxml::Err` value, with its cause and its fix, is in
   :doc:`troubleshooting`.
