#pragma once

// The Apple ALAC reference source predates the C99 stdint.h supplied by
// modern MSVC and ships its own msstdint.h.  Modern MSVC already provides
// the standard fixed-width integer types, so use the compiler's header and
// intentionally shadow the legacy compatibility header.
#include <stdint.h>
