#version 450 core

uniform int useLeafAlpha;
uniform sampler2D leafOpacityShadow;
uniform sampler2D leafOpacityShadowFall;
uniform float leafSeasonBlend;
uniform float leafAlphaScale;

in vec2 fTexCoordShadow;

layout(location = 0) out float fragmentDepth;

vec2 remapToFallAtlasUv(vec2 uv) {
  vec2 grid = clamp(uv, vec2(0.0), vec2(0.999999)) * vec2(3.0, 3.0);
  int col = int(floor(grid.x));
  int row = int(floor(grid.y));
  int springVariant = row * 3 + col;
  vec2 local = fract(grid);

  int springToFall[9] = int[9](
    0, 1, 2,
    1, 0, 2,
    3, 0, 2
  );
  vec4 fallRects[4] = vec4[4](
    vec4(0.029785, 0.029297, 0.319824, 0.469727),
    vec4(0.370605, 0.016113, 0.611816, 0.450684),
    vec4(0.654297, 0.043945, 0.952637, 0.472168),
    vec4(0.260742, 0.468750, 0.742188, 0.954102)
  );

  int fallVariant = springToFall[clamp(springVariant, 0, 8)];
  vec4 rect = fallRects[fallVariant];
  vec2 inset = vec2((rect.z - rect.x) * 0.006, (rect.w - rect.y) * 0.006);
  vec2 mn = rect.xy + inset;
  vec2 mx = rect.zw - inset;
  return mix(mn, mx, local);
}

void main(){
  // Discard transparent leaf pixels so they don't cast square shadows
  if (useLeafAlpha == 1) {
    float opacitySpring = texture(leafOpacityShadow, fTexCoordShadow).r;
    float opacityFall = texture(leafOpacityShadowFall, remapToFallAtlasUv(fTexCoordShadow)).r;
    float opacity = mix(opacitySpring, opacityFall, clamp(leafSeasonBlend, 0.0, 1.0)) * leafAlphaScale;
    if (opacity < 0.42) discard;
  }
  
  fragmentDepth = gl_FragCoord.z;
}
