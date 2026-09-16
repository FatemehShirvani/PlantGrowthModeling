#version 450 core

layout(location = 0) in vec3 vPosition;
layout(location = 2) in vec2 vTexCoord;
layout(location = 3) in float vLeafVariation;

uniform mat4 depthMVP;
uniform int windEnabled;
uniform int windAffectsObject; // 0: none, 1: branches, 2: attached leaves
uniform float windTime;
uniform float windStrength;
uniform vec2 windDir;
uniform float windGust;

out vec2 fTexCoordShadow;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise2(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for(int i = 0; i < 3; ++i) {
    v += a * valueNoise2(p);
    p = p * 2.03 + vec2(7.13, 5.71);
    a *= 0.5;
  }
  return v;
}

vec3 computeWindOffset(vec3 worldPos) {
  if(windEnabled == 0 || windAffectsObject == 0 || windStrength <= 1e-5) return vec3(0.0);

  vec2 mainDir = (dot(windDir, windDir) > 1e-6) ? normalize(windDir) : normalize(vec2(0.90, 0.44));
  vec2 sideDir = vec2(-mainDir.y, mainDir.x);

  float yNorm = clamp((worldPos.y + 1.0) / 3.2, 0.0, 1.0);
  float profile = smoothstep(0.04, 1.0, yNorm);
  profile = pow(profile, 1.35);
  float radial = clamp(length(worldPos.xz) / 2.8, 0.0, 1.0);
  profile *= mix(0.68, 1.0, radial);

  float gust = clamp(windGust, 0.15, 1.20);
  float t = windTime;

  vec2 fieldMain = worldPos.xz * 0.18 + mainDir * ((0.22 + 0.30 * gust) * t);
  vec2 fieldSide = worldPos.xz * 0.26 + sideDir * ((0.33 + 0.22 * gust) * t) + vec2(17.0, 9.0);
  float streamMain = fbm2(fieldMain + vec2(yNorm * 0.35, -yNorm * 0.21)) * 2.0 - 1.0;
  float streamSide = fbm2(fieldSide + vec2(-yNorm * 0.17, yNorm * 0.29)) * 2.0 - 1.0;

  float gustCell = fbm2(mainDir * (0.055 * t) + vec2(3.1, 8.7));
  float gustBurst = smoothstep(0.58, 0.90, gustCell);
  float envelope = (0.72 + 0.52 * gust) * (1.0 + 0.32 * gust * gustBurst);

  float profileVisible = 0.03 + 0.97 * profile;
  float amp = windStrength * profileVisible * envelope;
  float baseAmp = 0.23 * amp;

  float streamMix = 0.86 * streamMain + 0.28 * streamSide * abs(streamMain);
  vec2 sway = mainDir * (baseAmp * streamMix)
            + sideDir * (baseAmp * 0.52 * streamSide);
  float lift = baseAmp * (0.06 * streamSide + 0.05 * streamMain * streamMain);

  if(windAffectsObject == 1) {
    float crown = smoothstep(0.22, 1.0, yNorm);
    float crownField = fbm2(worldPos.xz * 0.32 + mainDir * (0.31 * t) + vec2(6.4, 2.7)) * 2.0 - 1.0;
    sway *= 1.02;
    sway += sideDir * (baseAmp * 0.16 * crown * crownField);
    lift += baseAmp * 0.06 * crown * crownField;
  } else if(windAffectsObject == 2) {
    float leafPhase = (2.0 + 1.2 * gust) * t
                    + dot(worldPos.xz, vec2(0.19, 0.13))
                    + vLeafVariation * 6.28318;
    float flutter = 0.72 * sin(leafPhase) + 0.28 * sin(1.83 * leafPhase + 0.9);
    float leafField = fbm2(worldPos.xz * 0.43 + sideDir * (0.75 * t) + vec2(vLeafVariation * 2.7, 1.3)) * 2.0 - 1.0;
    float flutterAmp = 0.020 * windStrength * (0.40 + 0.60 * gust) * (0.30 + 0.70 * profile);
    flutterAmp *= (1.0 + 0.25 * gustBurst);

    sway += sideDir * (flutterAmp * flutter);
    sway += mainDir * (flutterAmp * 0.22 * leafField);
    lift += flutterAmp * (0.22 * flutter + 0.18 * leafField);

    vec2 leafLocal = fract(vTexCoord * vec2(3.0, 3.0));
    float stemToTip = clamp(1.0 - leafLocal.y, 0.0, 1.0);
    float edge = abs(leafLocal.x - 0.5) * 2.0;
    float blade = pow(stemToTip, 1.45) * (0.65 + 0.35 * edge);
    float hingePhase = (2.7 + 1.6 * gust) * t + leafLocal.x * 6.28318 + vLeafVariation * 10.31;
    float hinge = sin(hingePhase) + 0.35 * sin(1.9 * hingePhase + 0.4);
    float hingeAmp = flutterAmp * (0.65 + 0.55 * gust) * blade;
    sway += sideDir * (hingeAmp * hinge);
    lift += hingeAmp * (0.42 * abs(hinge));
  }

  return vec3(sway.x, lift, sway.y);
}

void main(){
  vec3 worldPos = vPosition + computeWindOffset(vPosition);
  gl_Position = depthMVP * vec4(worldPos, 1.0);
  fTexCoordShadow = vTexCoord;
}
