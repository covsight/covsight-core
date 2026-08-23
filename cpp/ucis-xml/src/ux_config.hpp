// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_config.hpp - compile-time configuration. Every knob is #ifndef-guarded so a
// consumer can set it on the command line or before including the header.
//
// See docs/ucis-xml-cpp-impl-plan.md task I-1.1.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// The namespace everything lives in. A consumer vendoring two versions of this
// header into one binary renames one of them.
#ifndef UCIS_XML_NAMESPACE
#define UCIS_XML_NAMESPACE ucisxml
#endif

// Version of the writer itself, not of UCIS.
#define UCIS_XML_VERSION_MAJOR 0
#define UCIS_XML_VERSION_MINOR 1
#define UCIS_XML_VERSION_PATCH 0
#define UCIS_XML_VERSION_STRING "0.1.0"

// The UCIS revision whose Chapter 9 schema this emits.
#define UCIS_XML_UCIS_VERSION "1.0"

// Output buffer size. The sink is called once per full buffer, so this is the
// I/O granularity, not a limit on anything.
#ifndef UCIS_XML_BUFSZ
#define UCIS_XML_BUFSZ (64u * 1024u)
#endif

// Contract violations (design §5.5) are latched as errors and every subsequent
// call becomes a no-op -- a coverage write must never take a simulation down.
// The test suite defines UCIS_XML_ASSERT so violations abort loudly instead.
#ifndef UCIS_XML_ASSERT
#define UCIS_XML_HAVE_ASSERT 0
#else
#undef UCIS_XML_HAVE_ASSERT
#define UCIS_XML_HAVE_ASSERT 1
#endif

// std::string / std::string_view conveniences at the API boundary. Define
// UCIS_XML_NO_STL to build with no C++ standard library headers at all beyond
// the C ones above; the core never had a std:: member either way (design §9.4).
#ifndef UCIS_XML_NO_STL
#define UCIS_XML_NO_STL 0
#endif

// FILE*-backed sinks and the spool's temp-file spill.
#ifndef UCIS_XML_NO_STDIO
#define UCIS_XML_NO_STDIO 0
#endif

// A gzip sink built on zlib. Off by default: the header has no dependencies
// unless the consumer asks for one (design §5.4).
#ifndef UCIS_XML_ENABLE_ZLIB
#define UCIS_XML_ENABLE_ZLIB 0
#endif

// Replace malformed UTF-8 sequences in caller strings. Control-character
// sanitization is unconditional -- XML 1.0 forbids those outright -- but a
// caller that guarantees valid UTF-8 can skip this scan.
#ifndef UCIS_XML_SANITIZE_UTF8
#define UCIS_XML_SANITIZE_UTF8 1
#endif
