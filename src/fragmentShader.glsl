#version 450 core            // minimal GL version support expected from the GPU

struct LightSource {
  vec3 position;
  vec3 color;
  float intensity;
  int isActive;
};

int numberOfLights = 3;
uniform LightSource lightSources[3];
// Shadow maps
uniform sampler2D shadowMaps[3];
uniform mat4 lightDepthMVP[3];

struct Material {
  vec3 albedo;
  int useNormalMap; 
  int useColorMap;   
  int useBarkTexture;
  int useLeafTexture;
};

uniform Material material;

uniform sampler2D normalMapTexture;
uniform sampler2D colorMapTexture;
uniform sampler2D barkTexture;
uniform sampler2D leafColorTexture;
uniform sampler2D leafOpacityTexture;
uniform sampler2D leafColorTextureFall;
uniform sampler2D leafOpacityTextureFall;
uniform float leafSeasonBlend;
uniform vec3 leafSeasonTint;
uniform float leafAlphaScale;

uniform vec3 camPos;

in vec3 fPositionModel;
in vec3 fPosition;
in vec3 fNormal;
in vec2 fTexCoord;
flat in float fLeafVariation;

out vec4 colorOut;

float pi = 3.1415927;

vec2 remapToFallAtlasUv(vec2 uv) {
  // Canopy UVs are generated on a 3x3 spring/summer atlas.
  // For fall texture, remap each spring cell to one of 4 fall leaves.
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

vec3 fallHueFromVariation(float v) {
  float t = clamp(v, 0.0, 1.0);
  // Elm-like autumn tones: mostly yellow/gold/orange, with limited muted reds and browns.
  vec3 yellow = vec3(1.10, 1.05, 0.76);
  vec3 gold = vec3(1.05, 0.93, 0.62);
  vec3 orange = vec3(0.98, 0.78, 0.50);
  vec3 brown = vec3(0.78, 0.62, 0.44);
  vec3 mutedRed = vec3(0.90, 0.60, 0.50);

  vec3 tone = (t < 0.45)
    ? mix(yellow, gold, t / 0.45)
    : (t < 0.82)
      ? mix(gold, orange, (t - 0.45) / 0.37)
      : mix(orange, brown, (t - 0.82) / 0.18);

  float redW = smoothstep(0.68, 0.95, t) * 0.22;
  tone = mix(tone, mutedRed, redW);

  // Small per-leaf value variation so nearby hues are still distinguishable.
  float valueJitter = mix(0.92, 1.06, fract(t * 13.37));
  return tone * valueJitter;
}

float sampleLeafOpacity(vec2 uv) {
  float opacitySpring = texture(leafOpacityTexture, uv).r;
  float opacityFall = texture(leafOpacityTextureFall, remapToFallAtlasUv(uv)).r;
  float baseOpacity = mix(opacitySpring, opacityFall, clamp(leafSeasonBlend, 0.0, 1.0));
  return baseOpacity * leafAlphaScale;
}

vec3 sampleLeafAlbedo(vec2 uv) {
  vec3 springColor = texture(leafColorTexture, uv).rgb;
  vec3 fallColor = texture(leafColorTextureFall, remapToFallAtlasUv(uv)).rgb;
  vec3 variedFall = mix(fallColor, fallColor * fallHueFromVariation(fLeafVariation), 0.68);
  vec3 blended = mix(springColor, variedFall, clamp(leafSeasonBlend, 0.0, 1.0));
  return blended * leafSeasonTint;
}

float calculateShadow(int lightIndex, vec3 fragPos) {
  vec4 fragPosLightSpace = lightDepthMVP[lightIndex] * vec4(fragPos, 1.0);
  vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
  projCoords = projCoords * 0.5 + 0.5;
  
  if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
     projCoords.y < 0.0 || projCoords.y > 1.0) {
    return 1.0;
  }
  
  float closestDepth = texture(shadowMaps[lightIndex], projCoords.xy).r;
  float currentDepth = projCoords.z;
  float bias = 0.005;
  float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;
  
  return shadow;
}

void main() {
  // === LEAF ALPHA DISCARD ===
  if (material.useLeafTexture == 1) {
    float opacity = sampleLeafOpacity(fTexCoord);
    if (opacity < 0.42) discard;
  }

  // === DETERMINE SURFACE NORMAL ===
  vec3 n;
  if (material.useNormalMap == 1) {
    vec3 normalFromMap = texture(normalMapTexture, fTexCoord).rgb;
    normalFromMap = normalFromMap * 2.0 - 1.0;
    n = normalize(normalFromMap);
  } else {
    n = normalize(fNormal);
  }
  
  // For leaves: flip normal on back faces so they're lit properly
  if (material.useLeafTexture == 1 && !gl_FrontFacing) {
    n = -n;
  }
  
  vec3 wo = normalize(camPos - fPosition);

  // === DETERMINE BASE COLOR ===
  vec3 albedo;
  if (material.useLeafTexture == 1) {
    albedo = sampleLeafAlbedo(fTexCoord);
    // Darken back faces slightly for realism
    if (!gl_FrontFacing) {
      albedo *= 0.9;
    }
  }
  else if (material.useBarkTexture == 1) {
    albedo = texture(barkTexture, fTexCoord).rgb;
  }
  else if (material.useColorMap == 1) {
    albedo = texture(colorMapTexture, fTexCoord).rgb;
  }
  else {
    albedo = material.albedo;
  }

  // Ambient term is applied once per fragment.
  vec3 radiance = 0.06 * albedo;
  for(int i=0; i<numberOfLights; ++i) {
    LightSource a_light = lightSources[i];
    if(a_light.isActive == 1) {
      vec3 wi = normalize(a_light.position - fPosition);
      vec3 Li = a_light.color * a_light.intensity;
      float shadowFactor = calculateShadow(i, fPosition);
      radiance += shadowFactor * Li * albedo * max(dot(n, wi), 0);
    }
  }

  colorOut = vec4(radiance, 1.0);
}
