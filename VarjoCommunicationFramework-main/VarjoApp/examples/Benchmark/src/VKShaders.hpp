#pragma once

/**
 * Files included here are generated from appropriate shader sources by glslc
 * See CMakeLists.txt for details
 */

#include <cstdint>

const uint32_t gridFrag[] = {
#include "grid.frag.spv.inc"
};

const uint32_t gridVert[] = {
#include "grid.vert.spv.inc"
};

const uint32_t sceneNoVelocityFrag[] = {
#include "sceneNoVelocity.frag.spv.inc"
};

const uint32_t sceneNoVelocityVert[] = {
#include "sceneNoVelocity.vert.spv.inc"
};

const uint32_t sceneVelocityFrag[] = {
#include "sceneVelocity.frag.spv.inc"
};

const uint32_t sceneVelocityVert[] = {
#include "sceneVelocity.vert.spv.inc"
};

const uint32_t stencilFrag[] = {
#include "stencil.frag.spv.inc"
};

const uint32_t stencilVert[] = {
#include "stencil.vert.spv.inc"
};

const uint32_t vrsComp[] = {
#include "vrs.comp.spv.inc"
};
