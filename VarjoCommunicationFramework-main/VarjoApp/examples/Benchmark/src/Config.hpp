#pragma once

#include <sdkddkver.h>

#if defined(NTDDI_WIN10_19H1)
// D3D12 Variable Rate Shading requires SDK v.10.0.18362.0
#define D3D12_VRS_ENABLED
#endif
