// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// Selects which build of the library a test compiles against. Every test is
// built twice -- once over the split modules in src/, once over the generated
// single header in include/ -- so the artifact consumers vendor is covered by
// the same suite as the sources (T-13).
#pragma once

#if defined(UCIS_XML_AMALGAM_TEST)
#include "ucis_xml.hpp"
#else
#include "ux_points.hpp"
#endif
