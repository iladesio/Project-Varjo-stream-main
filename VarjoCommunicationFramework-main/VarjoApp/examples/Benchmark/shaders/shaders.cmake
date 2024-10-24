set(_shader_list
  grid.frag
  grid.vert
  sceneNoVelocity.frag
  sceneNoVelocity.vert
  sceneVelocity.frag
  sceneVelocity.vert
  stencil.frag
  stencil.vert
  vrs.comp
)

set(_shader_include_list
  ${_shader_src_dir}/scene.frag.inc
  ${_shader_src_dir}/scene.vert.inc
  ${_shader_src_dir}/velocityConstants.inc
)
