// ----------------------------------------------------------------------------
// main.cpp
//
//  Created on: Wed Oct 21 11:32:52 2020
//      Author: Kiwon Um (originally designed by Tamy Boubekeur)
//        Mail: kiwon.um@telecom-paris.fr
//
// Description: IGR202 - Practical - Plant Growth Simulation
//
// Modified for Plant Growth Project by adding:
// - Plant class with procedural branching
// - Growth simulation
// - Cylinder mesh generation
// - Bark and leaf texture mapping (atlas-based)
// - Phyllotaxis leaf arrangement (golden angle)
//
// Copyright 2020 Kiwon Um and Tamy Boubekeur
// ----------------------------------------------------------------------------

#define _USE_MATH_DEFINES

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <ios>
#include <vector>
#include <string>
#include <cmath>
#include <memory>
#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <numeric>
#include <chrono>

#include "Error.h"
#include "ShaderProgram.h"
#include "Camera.h"
#include "Mesh.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const std::string DEFAULT_MESH_FILENAME("data/monkey.off");

// window parameters
GLFWwindow *g_window = nullptr;
int g_windowWidth = 1024;
int g_windowHeight = 768;

// pointer to the current camera model
std::shared_ptr<Camera> g_cam;

// camera control variables
float g_meshScale = 1.0; // to update based on the mesh size, so that navigation runs at scale
bool g_rotatingP = false;
bool g_panningP = false;
bool g_zoomingP = false;
double g_baseX = 0.0, g_baseY = 0.0;
glm::vec3 g_baseTrans(0.0);
glm::vec3 g_baseRot(0.0);

// timer
float g_appTimer = 0.0;
bool g_appTimerStoppedP = true;

// TODO: textures
unsigned int g_availableTextureSlot = 0;

int g_albedoTexLoaded = 0;
GLuint g_albedoTex;
unsigned int g_albedoTexOnGPU;
int g_normalTexLoaded = 0;
GLuint g_normalTex;
unsigned int g_normalTexOnGPU;
GLuint loadTextureFromFileToGPU(const std::string &filename)
{
  int width, height, numComponents;
  unsigned char *data = stbi_load(
    filename.c_str(),
    &width,
    &height,
    &numComponents,
    0);
  if(!data) {
    std::cerr << "[Texture] Failed to load: " << filename << std::endl;
    return 0;
  }

  GLuint texID;
  glGenTextures(1, &texID);
  glBindTexture(GL_TEXTURE_2D, texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    (numComponents == 1 ? GL_RED : numComponents == 3 ? GL_RGB : GL_RGBA),
    width,
    height,
    0,
    (numComponents == 1 ? GL_RED : numComponents == 3 ? GL_RGB : GL_RGBA),
    GL_UNSIGNED_BYTE,
    data);

  glGenerateMipmap(GL_TEXTURE_2D);

  stbi_image_free(data);
  glBindTexture(GL_TEXTURE_2D, 0);
  return texID;
}

class FboShadowMap {
public:
  GLuint getTextureId() const { return _depthMapTexture; }

  bool allocate(unsigned int width=1024, unsigned int height=768)
  {
    glGenFramebuffers(1, &_depthMapFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _depthMapFbo);

    _depthMapTextureWidth = width;
    _depthMapTextureHeight = height;

    glGenTextures(1, &_depthMapTexture);
    glBindTexture(GL_TEXTURE_2D, _depthMapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, _depthMapTexture, 0);

    glDrawBuffer(GL_NONE);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return true;
    } else {
      std::cout << "PROBLEM IN FBO FboShadowMap::allocate(): FBO NOT successfully created" << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }
  }

  void bindFbo()
  {
    glViewport(0, 0, _depthMapTextureWidth, _depthMapTextureHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, _depthMapFbo);
    glClear(GL_DEPTH_BUFFER_BIT);
  }

  void free() { glDeleteFramebuffers(1, &_depthMapFbo); }

  void savePpmFile(std::string const &filename)
  {
    std::ofstream output_image(filename.c_str());

    int i, j, k;
    float *pixels = new float[_depthMapTextureWidth*_depthMapTextureHeight];

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, _depthMapTextureWidth, _depthMapTextureHeight, GL_DEPTH_COMPONENT , GL_FLOAT, pixels);

    output_image << "P3" << std::endl;
    output_image << _depthMapTextureWidth << " " << _depthMapTextureHeight << std::endl;
    output_image << "255" << std::endl;

    k = 0;
    for(i=0; i<(int)_depthMapTextureWidth; ++i) {
      for(j=0; j<(int)_depthMapTextureHeight; ++j) {
        output_image <<
          static_cast<unsigned int>(255*pixels[k]) << " " <<
          static_cast<unsigned int>(255*pixels[k]) << " " <<
          static_cast<unsigned int>(255*pixels[k]) << " ";
        k = k+1;
      }
      output_image << std::endl;
    }
    delete [] pixels;
    output_image.close();
  }

private:
  GLuint _depthMapFbo;
  GLuint _depthMapTexture;
  unsigned int _depthMapTextureWidth;
  unsigned int _depthMapTextureHeight;
};


struct Light {
  FboShadowMap shadowMap;
  glm::mat4 depthMVP;
  unsigned int shadowMapTexOnGPU;

  glm::vec3 position;
  glm::vec3 color;
  float intensity;

  void setupCameraForShadowMapping(
    std::shared_ptr<ShaderProgram> shader_shadow_map_Ptr,
    const glm::vec3 scene_center,
    const float scene_radius)
  {
    float ortho_size = scene_radius;
    glm::mat4 depthProjectionMatrix = glm::ortho<float>(
      -ortho_size, ortho_size,
      -ortho_size, ortho_size,
      0.1f, scene_radius * 3.0f
    );
    glm::mat4 depthViewMatrix = glm::lookAt(
      position,
      scene_center,
      glm::vec3(0, 1, 0)
    );
   
    depthMVP = depthProjectionMatrix * depthViewMatrix;
    shader_shadow_map_Ptr->set("depthMVP", depthMVP);
  }

  void allocateShadowMapFbo(unsigned int w=800, unsigned int h=600)
  {
    shadowMap.allocate(w, h);
  }
  void bindShadowMap()
  {
    shadowMap.bindFbo();
  }
};


// ============================================================================
// PLANT GROWTH SIMULATION
// ============================================================================

struct Branch {
    glm::vec3 startPos;
    glm::vec3 endPos;
    glm::vec3 growthDirection;
    float radius;
    int parentIndex;
    float parentAttachT;
    int activationStage;
    int depth;  
    float age;  
    float maxLength;
    bool isGrowing;
    bool branchingTried;
    std::vector<int> childBranches;
    
    Branch(glm::vec3 start, glm::vec3 dir, float r, int d, float maxLen, int parent = -1, int activation = -1) 
        : startPos(start), growthDirection(glm::normalize(dir)), radius(r), 
          parentIndex(parent), parentAttachT(1.0f), activationStage(activation >= 0 ? activation : d),
          depth(d), age(0.0f), maxLength(maxLen),
          isGrowing(true), branchingTried(false) {
        endPos = startPos + growthDirection * 0.01f;
    }
};

class Plant {
public:
    std::vector<Branch> branches;
    std::shared_ptr<Mesh> plantMesh;
    std::shared_ptr<Mesh> leafMesh;
    std::shared_ptr<Mesh> fallenLeafMesh;

    struct FallingLeaf {
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 vel = glm::vec3(0.0f);
        float size = 0.05f;
        float yaw = 0.0f;
        float spin = 0.0f;
        float wobblePhase = 0.0f;
        float age = 0.0f;
        float groundAge = 0.0f;
        float preFadeDelay = 2.5f;
        float fadeDuration = 4.0f;
        bool landed = false;
        int variant = 0;
        float colorVariation = 0.5f;
        glm::vec3 detachRight = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 detachUp = glm::vec3(0.0f, 1.0f, 0.0f);
        float detachOrientDuration = 1.0f;
        glm::vec3 landRightStart = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 landUpStart = glm::vec3(0.0f, 1.0f, 0.0f);
        int supportCorner = 0;
        float landSettle = 0.0f;
    };

    // Growth parameters
    float growthSpeed = 0.3f;
    float branchAngle = 40.0f;
    float lengthDecay = 0.65f;
    float radiusDecay = 0.55f;
    int maxDepth = 6;
    float branchingAge = 1.0f;
    int branchesPerNode = 4;
    unsigned int randomSeed = 42;
    int maxBranches = 1200; // Hard cap to keep growth cost bounded
    int maxLeaves = 20000;  // Cap generated leaves to control overlap solver cost
    bool growthCapReachedLogged = false;
    bool leafCapReachedLogged = false;

    // L-system parameters
    bool useLSystem = true;
    bool useLegacyEquivalentLSystem = true;
    int lsystemIterations = 8;
    float lsystemAngleDeg = 28.0f;
    float lsystemBaseSegment = 0.13f;
    float lsystemLengthDecay = 0.84f;
    float lsystemRadiusDecay = 0.56f;
    float lsystemStartRadius = 0.050f;
    float lsystemDepthDelay = 0.10f;
    bool useSpaceColonizationGuidance = true;
    int canopyAttractorCount = 1400;
    glm::vec3 canopyCenter = glm::vec3(0.0f, 0.75f, 0.0f);
    glm::vec3 canopyRadii = glm::vec3(2.15f, 2.10f, 2.05f);
    float canopyInnerRadius = 0.12f;
    float canopyInfluenceRadius = 1.15f;
    float canopyGuidanceStrength = 0.58f;
    float apicalDominance = 0.64f;
    glm::vec3 tropismDirection = glm::vec3(0.08f, 1.0f, 0.03f);
    float tropismStrength = 0.11f;
    int growthStage = 0;
    int maxGrowthStage = 0;
    bool suppressBranchLogs = false;
    
    int cylinderSegments = 8;
    float leafPositionSmoothing = 0.08f; // Lower = smoother/slower, higher = faster response
    float seasonalStability = 0.0f;      // 0: normal, 1: preserve mature leaf layout during season transitions
    float seasonalLeafDensity = 0.78f;   // 0: leafless, 1: full canopy
    float seasonalLeafSizeScale = 0.85f; // spring starts slightly smaller than summer
    float leafAttachTransitionBand = 0.18f; // wider band softens season attach/detach
    float minRenderedAttachment = 0.002f;   // keep tiny transitional leaves alive to avoid popping/flicker
    int leafAtlasCols = 3;               // spring/summer atlas layout
    int leafAtlasRows = 3;
    int leafVariantCount = 9;
    // Optional explicit UV boxes per leaf variant: (uMin, vMin, uMax, vMax).
    // Used for non-grid atlases (e.g., fall leaves 3 top + 1 bottom cluster).
    std::vector<glm::vec4> leafVariantUVRects;
    std::map<unsigned long long, glm::vec3> leafSmoothedPositions;
    std::map<unsigned long long, float> leafAttachmentState;
    std::map<unsigned long long, float> leafLastRenderedSize;
    std::map<unsigned long long, int> leafLastRenderedVariant;
    std::map<unsigned long long, float> leafLastRenderedColorVariation;
    std::map<unsigned long long, glm::vec3> leafLastRenderedRight;
    std::map<unsigned long long, glm::vec3> leafLastRenderedUp;
    std::set<unsigned long long> activeLeafKeysThisFrame;
    std::vector<FallingLeaf> fallenLeaves;
    int maxFallenLeaves = 1800;
    float fallenLeafGroundY = -1.0f;
    bool allowSeasonLeafDrop = false;
    float seasonLeafDropStrength = 0.0f;
    float fallenLeafGroundAgingRate = 1.0f; // >1 clears ground leaves faster, <1 keeps them longer.
    bool windPhysicsEnabled = false;
    float windBlend = 0.0f;              // smooth 0..1 enable blend
    float windTime = 0.0f;               // independent simulation clock
    glm::vec2 windDirection = glm::vec2(0.90f, 0.44f); // filtered dominant wind direction (XZ)
    glm::vec2 windDirectionVelocity = glm::vec2(0.0f);
    float windGust = 0.55f;              // filtered gust envelope [~0.2, 1.0]
    float windGustVelocity = 0.0f;
    float windMeshRebuildAccumulator = 0.0f;
    float windMeshRebuildInterval = 1.0f / 45.0f;
    float windLastStepDt = 1.0f / 60.0f;
    unsigned int windEvaluationFrame = 1u;
    std::map<unsigned long long, glm::vec3> windNodeOffsets;
    std::map<unsigned long long, glm::vec3> windNodeVelocities;
    std::map<unsigned long long, unsigned int> windNodeStamp;
    std::map<int, glm::vec3> branchFrameRightCache;

    static unsigned int makeRuntimeSeed() {
        static unsigned int sequence = 1u;
        unsigned int t = static_cast<unsigned int>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
        unsigned int seed = t ^ (sequence * 747796405u);
        sequence += 1u;
        return seed;
    }
    
    Plant() {
        randomSeed = makeRuntimeSeed();
        plantMesh = std::make_shared<Mesh>();
        leafMesh = std::make_shared<Mesh>();
        fallenLeafMesh = std::make_shared<Mesh>();

        if(useLSystem) {
            generateFromLSystem();
            initializeGrowthStages();
            std::cout << "[Plant] Generated L-system tree (" << branches.size() << " branches)" << std::endl;
        } else {
            branches.push_back(Branch(
                glm::vec3(0, -1, 0),
                glm::vec3(0, 1, 0),
                0.08f,
                0,
                0.8f
            ));
            std::cout << "[Plant] Created with initial trunk" << std::endl;
        }
        std::cout << "[Plant] Seed: " << randomSeed << std::endl;
    }
    
    // Deterministic pseudo-random (stable across frames for same inputs)
    static float pseudoRandom(int seed, int i) {
        int h = (seed * 2654435761 + i * 40503) & 0x7FFFFFFF;
        return (float)(h % 10000) / 10000.0f;
    }

    static float smoothNoise1D(float x, int seed) {
        float xFloor = std::floor(x);
        int i0 = static_cast<int>(xFloor);
        int i1 = i0 + 1;
        float t = x - xFloor;
        float u = t * t * (3.0f - 2.0f * t);
        float n0 = pseudoRandom(seed, i0 * 92821 + 17) * 2.0f - 1.0f;
        float n1 = pseudoRandom(seed, i1 * 92821 + 17) * 2.0f - 1.0f;
        return glm::mix(n0, n1, u);
    }

    bool isWindPhysicsEnabled() const {
        return windPhysicsEnabled;
    }

    void setWindPhysicsEnabled(bool enabled) {
        windPhysicsEnabled = enabled;
    }

    void toggleWindPhysics() {
        windPhysicsEnabled = !windPhysicsEnabled;
    }

    glm::vec3 windOffsetAt(
        const glm::vec3& worldPos,
        float localScale = 1.0f,
        float branchFlex = 1.0f,
        float branchSeed = 0.0f) const {
        float flex = glm::clamp(branchFlex, 0.04f, 1.35f);
        if(windBlend <= 1e-4f || localScale <= 1e-4f || flex <= 1e-4f) return glm::vec3(0.0f);

        auto layeredNoise = [](float x) {
            return 0.55f * std::sin(x)
                 + 0.30f * std::sin(1.91f * x + 1.37f)
                 + 0.15f * std::sin(3.77f * x + 2.41f);
        };

        float t = windTime;
        float yNorm = glm::clamp((worldPos.y + 1.0f) / 3.2f, 0.0f, 1.0f);
        float radial = glm::clamp(glm::length(glm::vec2(worldPos.x, worldPos.z)) / 2.6f, 0.0f, 1.0f);
        // Height-dependent envelope: calmer lower/mid canopy, stronger response near crown.
        float heightGain = std::pow(glm::smoothstep(0.28f, 1.0f, yNorm), 1.45f);
        float crownBoost = glm::mix(0.56f, 1.26f, heightGain);
        float profile = std::pow(glm::smoothstep(0.10f, 1.0f, yNorm), 2.05f) * (0.42f + 0.58f * radial);
        profile *= (0.35f + 0.65f * flex);
        float baseAmp = windBlend * localScale * flex * (0.0016f + 0.036f * profile) * crownBoost;

        float phase = worldPos.x * 2.41f + worldPos.z * 1.77f + yNorm * 3.6f + branchSeed * 9.13f;
        glm::vec2 meanDir = glm::normalize(glm::vec2(0.90f, 0.44f));
        float dirJitter = (0.10f + 0.28f * flex) * layeredNoise(0.18f * t + phase * 0.16f);
        float c = std::cos(dirJitter);
        float s = std::sin(dirJitter);
        glm::vec2 dir0 = glm::normalize(glm::vec2(
            meanDir.x * c - meanDir.y * s,
            meanDir.x * s + meanDir.y * c));
        glm::vec2 dir1(-dir0.y, dir0.x);

        float gust = 0.60f + 0.40f * (0.5f + 0.5f * layeredNoise(0.36f * t + phase * 0.11f));
        float lag = 0.18f + 0.55f * flex;
        float trunkProfile = std::pow(glm::smoothstep(0.10f, 1.0f, yNorm), 1.35f);
        float trunkMode = layeredNoise(0.28f * t + branchSeed * 0.37f + phase * 0.06f);
        float trunkAmp = windBlend
                       * (0.0008f + 0.0068f * trunkProfile)
                       * (0.30f + 0.60f * gust)
                       * (0.60f + 0.65f * heightGain);
        glm::vec2 trunkDisp = meanDir * trunkAmp * trunkMode;

        float primary = baseAmp * (0.42f + 0.58f * gust) * layeredNoise((1.12f - 0.22f * flex) * t + phase + lag * gust);
        float secondary = baseAmp * (0.34f + 0.36f * flex) * layeredNoise((2.08f + 0.32f * flex) * t + phase * 1.31f + 0.9f + lag);
        float lift = baseAmp * (0.035f + 0.075f * gust) * layeredNoise((1.84f + 0.22f * flex) * t + phase * 0.62f + 1.2f);

        return glm::vec3(
            trunkDisp.x + dir0.x * primary + dir1.x * secondary,
            lift,
            trunkDisp.y + dir0.y * primary + dir1.y * secondary);
    }

    float branchWindFlex(const Branch& branch) const {
        float radiusRef = glm::max(0.010f, lsystemStartRadius);
        float radiusNorm = glm::clamp(branch.radius / radiusRef, 0.0f, 1.0f);
        float radiusFlex = 1.0f - std::pow(radiusNorm, 0.45f);
        float depthFlex = glm::smoothstep(0.0f, 5.5f, (float)branch.depth);
        float tipBias = branch.childBranches.empty() ? 0.12f : 0.0f;
        float flex = 0.08f + 0.62f * depthFlex + 0.30f * radiusFlex + tipBias;
        return glm::clamp(flex, 0.05f, 1.20f);
    }

    float branchWindSeed(int branchIdx) const {
        return pseudoRandom((int)randomSeed + 7013, branchIdx * 29 + 5);
    }

    static unsigned long long makeWindNodeKey(int branchIdx, int nodeKind) {
        return (static_cast<unsigned long long>(static_cast<unsigned int>(branchIdx)) << 1ull) |
               static_cast<unsigned long long>(nodeKind & 1);
    }

    glm::vec3 sampleBranchNodeWindOffset(
        const glm::vec3& worldPos,
        int branchIdx,
        int nodeKind,
        float localScale,
        float branchFlex,
        float branchSeed) {
        unsigned long long key = makeWindNodeKey(branchIdx, nodeKind);
        auto stampIt = windNodeStamp.find(key);
        if(stampIt != windNodeStamp.end() && stampIt->second == windEvaluationFrame) {
            return windNodeOffsets[key];
        }

        glm::vec3 target = windOffsetAt(worldPos, localScale, branchFlex, branchSeed);
        glm::vec3& offset = windNodeOffsets[key];
        glm::vec3& velocity = windNodeVelocities[key];

        float flex = glm::clamp(branchFlex, 0.04f, 1.35f);
        float dt = glm::clamp(windLastStepDt, 1.0f / 240.0f, 1.0f / 24.0f);
        int subSteps = glm::max(1, (int)std::ceil(dt / (1.0f / 140.0f)));
        float stepDt = dt / (float)subSteps;

        // Physically guided branch response: each branch node follows a damped oscillator,
        // which produces lag and settling instead of rigid periodic back-and-forth.
        float omega = 2.1f + 5.3f * flex;
        float dampingRatio = 0.70f + 0.22f * (1.0f - flex);
        for(int i = 0; i < subSteps; ++i) {
            glm::vec3 accel = (omega * omega) * (target - offset) - (2.0f * dampingRatio * omega) * velocity;
            velocity += accel * stepDt;
            offset += velocity * stepDt;
        }

        float maxOffset = glm::length(target) * 2.4f + (0.0025f + 0.010f * flex);
        float offsetLen = glm::length(offset);
        if(offsetLen > maxOffset && offsetLen > 1e-6f) {
            offset *= (maxOffset / offsetLen);
            velocity *= 0.74f;
        }

        if(windBlend <= 1e-4f && !windPhysicsEnabled &&
           glm::length(target) <= 1e-6f &&
           glm::length(offset) < 1e-4f &&
           glm::length(velocity) < 1e-4f) {
            offset = glm::vec3(0.0f);
            velocity = glm::vec3(0.0f);
        }

        windNodeStamp[key] = windEvaluationFrame;
        return offset;
    }

    void computeWindedBranchEndpoints(const Branch& branch, int branchIdx, glm::vec3& outStart, glm::vec3& outEnd) {
        // Stronger cantilever behavior: branch base follows parent-attached offset,
        // branch tip receives larger deflection for visible bending.
        const float startScaleRoot = 0.72f;
        const float parentStartScale = 0.95f;
        const float parentEndScale = 2.05f;
        const float tipScale = 2.45f;

        float branchFlex = branchWindFlex(branch);
        float branchSeed = branchWindSeed(branchIdx);

        if(branch.parentIndex >= 0 && branch.parentIndex < (int)branches.size()) {
            const Branch& parent = branches[branch.parentIndex];
            float parentFlex = branchWindFlex(parent);
            float parentSeed = branchWindSeed(branch.parentIndex);
            glm::vec3 parentStartOff = sampleBranchNodeWindOffset(
                parent.startPos, branch.parentIndex, 0, parentStartScale, parentFlex, parentSeed);
            glm::vec3 parentEndOff = sampleBranchNodeWindOffset(
                parent.endPos, branch.parentIndex, 1, parentEndScale, parentFlex, parentSeed);
            float attachT = glm::clamp(branch.parentAttachT, 0.0f, 1.0f);
            glm::vec3 attachOffset = glm::mix(parentStartOff, parentEndOff, attachT);
            outStart = branch.startPos + attachOffset;
        } else {
            outStart = branch.startPos + sampleBranchNodeWindOffset(
                branch.startPos, branchIdx, 0, startScaleRoot, branchFlex, branchSeed);
        }

        glm::vec3 endBase = branch.endPos + sampleBranchNodeWindOffset(
            branch.endPos, branchIdx, 1, tipScale, branchFlex, branchSeed);

        // Amplify tip-vs-base differential so branch bending is visually readable.
        glm::vec3 startOffset = outStart - branch.startPos;
        glm::vec3 endOffset = endBase - branch.endPos;
        glm::vec3 relBend = endOffset - startOffset;
        outEnd = endBase + 0.58f * relBend;

        // Clamp twig whip to avoid tiny segments spinning/flipping under wind.
        glm::vec3 restVec = branch.endPos - branch.startPos;
        float restLen = glm::length(restVec);
        if(restLen > 1e-6f) {
            glm::vec3 seg = outEnd - outStart;
            float segLen = glm::length(seg);
            if(segLen < 1e-6f) {
                outEnd = outStart + glm::normalize(restVec) * restLen;
                seg = outEnd - outStart;
                segLen = glm::length(seg);
            }

            glm::vec3 restDir = glm::normalize(restVec);
            glm::vec3 segDir = glm::normalize(seg);
            float cosA = glm::clamp(glm::dot(restDir, segDir), -1.0f, 1.0f);
            float angle = std::acos(cosA);
            float maxAngle = glm::radians(16.0f + 24.0f * branchFlex);
            if(angle > maxAngle && angle > 1e-5f) {
                float t = maxAngle / angle;
                segDir = glm::normalize(glm::mix(restDir, segDir, t));
            }

            float minLen = 0.70f * restLen;
            float maxLen = 1.35f * restLen;
            float clampedLen = glm::clamp(segLen, minLen, maxLen);
            outEnd = outStart + segDir * clampedLen;
        }
    }

    void computeStableBranchFrame(int branchIdx, const glm::vec3& direction, glm::vec3& rightOut, glm::vec3& forwardOut) {
        glm::vec3 prevRight(0.0f);
        bool hasPrev = false;
        auto it = branchFrameRightCache.find(branchIdx);
        if(it != branchFrameRightCache.end()) {
            prevRight = it->second;
            hasPrev = true;
        }

        glm::vec3 rightCandidate(0.0f);
        if(hasPrev) {
            rightCandidate = prevRight - glm::dot(prevRight, direction) * direction;
        }

        if(glm::length(rightCandidate) < 1e-5f) {
            float seed = branchWindSeed(branchIdx);
            float phi = seed * 2.0f * (float)M_PI;
            glm::vec3 ref = glm::normalize(glm::vec3(std::cos(phi), 0.42f + 0.18f * seed, std::sin(phi)));
            rightCandidate = ref - glm::dot(ref, direction) * direction;
        }

        if(glm::length(rightCandidate) < 1e-5f) {
            glm::vec3 fallback = (std::abs(direction.y) < 0.95f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            rightCandidate = glm::cross(fallback, direction);
        }

        rightOut = glm::normalize(rightCandidate);
        forwardOut = glm::normalize(glm::cross(direction, rightOut));
        rightOut = glm::normalize(glm::cross(forwardOut, direction));

        if(hasPrev && glm::dot(rightOut, prevRight) < 0.0f) {
            rightOut = -rightOut;
            forwardOut = -forwardOut;
        }

        branchFrameRightCache[branchIdx] = rightOut;
    }

    void updateWindSimulation(float dt) {
        float safeDt = glm::max(0.0f, dt);
        if(safeDt > 1e-6f) {
            windLastStepDt = glm::clamp(safeDt, 1.0f / 240.0f, 1.0f / 24.0f);
        }
        windTime += safeDt;

        float target = windPhysicsEnabled ? 1.0f : 0.0f;
        float prevBlend = windBlend;
        float response = windPhysicsEnabled ? 4.0f : 6.0f;
        float blendK = 1.0f - std::exp(-response * safeDt);
        windBlend = glm::mix(windBlend, target, glm::clamp(blendK, 0.0f, 1.0f));
        if(std::abs(windBlend - target) < 1e-4f) {
            windBlend = target;
        }

        bool active = (windBlend > 1e-4f) || windPhysicsEnabled;
        if(!active && prevBlend <= 1e-4f) {
            windNodeOffsets.clear();
            windNodeVelocities.clear();
            windNodeStamp.clear();
            branchFrameRightCache.clear();
            windDirection = glm::vec2(0.90f, 0.44f);
            windDirectionVelocity = glm::vec2(0.0f);
            windGust = 0.55f;
            windGustVelocity = 0.0f;
            windMeshRebuildAccumulator = 0.0f;
            return;
        }

        float simDt = glm::clamp(safeDt, 1.0f / 240.0f, 1.0f / 20.0f);
        glm::vec2 meanDir = glm::normalize(glm::vec2(0.90f, 0.44f));

        // Slowly drifting wind direction (stochastic, low-frequency) with inertial response.
        float dirNoiseA = smoothNoise1D(windTime * 0.055f + 12.7f, static_cast<int>(randomSeed) + 4109);
        float dirNoiseB = smoothNoise1D(windTime * 0.097f + 41.3f, static_cast<int>(randomSeed) + 9127);
        float targetAngle = 0.40f * dirNoiseA + 0.22f * dirNoiseB;
        float c = std::cos(targetAngle);
        float s = std::sin(targetAngle);
        glm::vec2 targetDir = glm::normalize(glm::vec2(
            meanDir.x * c - meanDir.y * s,
            meanDir.x * s + meanDir.y * c
        ));

        float dirOmega = 2.0f + 0.8f * windBlend;
        float dirDamping = 0.95f;
        glm::vec2 dirAccel = (dirOmega * dirOmega) * (targetDir - windDirection)
                           - (2.0f * dirDamping * dirOmega) * windDirectionVelocity;
        windDirectionVelocity += dirAccel * simDt;
        windDirection += windDirectionVelocity * simDt;
        float dirLen = glm::length(windDirection);
        if(dirLen > 1e-5f) {
            windDirection /= dirLen;
        } else {
            windDirection = targetDir;
            windDirectionVelocity = glm::vec2(0.0f);
        }

        // Gust envelope with independent inertia to avoid periodic pumping.
        float gustNoiseA = 0.5f + 0.5f * smoothNoise1D(windTime * 0.12f + 9.1f, static_cast<int>(randomSeed) + 14011);
        float gustNoiseB = 0.5f + 0.5f * smoothNoise1D(windTime * 0.24f + 23.4f, static_cast<int>(randomSeed) + 5011);
        float gustTarget = glm::clamp(0.35f + 0.65f * (0.68f * gustNoiseA + 0.32f * gustNoiseB), 0.20f, 1.00f);
        float gustOmega = 2.4f;
        float gustDamping = 1.05f;
        float gustAccel = (gustOmega * gustOmega) * (gustTarget - windGust)
                        - (2.0f * gustDamping * gustOmega) * windGustVelocity;
        windGustVelocity += gustAccel * simDt;
        windGust += windGustVelocity * simDt;
        windGust = glm::clamp(windGust, 0.15f, 1.10f);

        // Rebuild mesh at a stable cadence so CPU-side winded branch/leaf geometry animates.
        windMeshRebuildAccumulator += safeDt;
        bool blendChanged = std::abs(windBlend - prevBlend) > 1e-4f;
        if(blendChanged || windMeshRebuildAccumulator >= windMeshRebuildInterval) {
            windMeshRebuildAccumulator = 0.0f;
            updateMesh();
        }
    }

    static unsigned long long makeLeafKey(int branchId, int localLeafId) {
        return (static_cast<unsigned long long>(static_cast<unsigned int>(branchId)) << 32) |
               static_cast<unsigned long long>(static_cast<unsigned int>(localLeafId));
    }

    static float seasonalLeafNoise(unsigned long long leafKey, unsigned int seed) {
        unsigned int hi = static_cast<unsigned int>(leafKey >> 32);
        unsigned int lo = static_cast<unsigned int>(leafKey & 0xFFFFFFFFull);
        unsigned int mixed = hi ^ (lo * 1664525u) ^ (seed * 1013904223u);
        return pseudoRandom(static_cast<int>(mixed & 0x7FFFFFFF), static_cast<int>((mixed >> 1) & 0x7FFFFFFF));
    }

    static float leafOldness(unsigned long long leafKey) {
        unsigned int localLeafId = static_cast<unsigned int>(leafKey & 0xFFFFFFFFull);
        float oldness = 0.5f;
        if(localLeafId >= 1000u) {
            // Tip-cluster leaves are generally newer.
            float tipNorm = glm::clamp((float)(localLeafId - 1000u) / 8.0f, 0.0f, 1.0f);
            oldness = 0.28f * (1.0f - tipNorm);
        } else {
            // Lower local ids are emitted earlier -> older.
            oldness = glm::clamp(1.0f - (float)localLeafId / 14.0f, 0.0f, 1.0f);
        }

        // Small deterministic jitter avoids visible banding by index.
        int jitterSeed = static_cast<int>((leafKey >> 8) & 0x7FFFFFFF);
        float jitter = pseudoRandom(jitterSeed, 991) - 0.5f;
        return glm::clamp(oldness + 0.18f * jitter, 0.0f, 1.0f);
    }

    glm::vec4 computeLeafUVRect(int leafVariant) const {
        float uMin = 0.0f, uMax = 1.0f, vMin = 0.0f, vMax = 1.0f;
        int variantCount = glm::max(1, leafVariantCount);
        int clampedVariant = glm::clamp(leafVariant, 0, variantCount - 1);
        if(!leafVariantUVRects.empty()) {
            int rectIndex = glm::clamp(clampedVariant, 0, (int)leafVariantUVRects.size() - 1);
            glm::vec4 uv = leafVariantUVRects[rectIndex];
            uMin = uv.x; vMin = uv.y; uMax = uv.z; vMax = uv.w;
            // Slight inset avoids bleeding from neighboring atlas content.
            float du = (uMax - uMin) * 0.006f;
            float dv = (vMax - vMin) * 0.006f;
            uMin += du; uMax -= du;
            vMin += dv; vMax -= dv;
        } else {
            int atlasCols = glm::max(1, leafAtlasCols);
            int atlasRows = glm::max(1, leafAtlasRows);
            int col = clampedVariant % atlasCols;
            int row = clampedVariant / atlasCols;
            row = glm::clamp(row, 0, atlasRows - 1);
            uMin = col / (float)atlasCols;
            uMax = (col + 1) / (float)atlasCols;
            vMin = row / (float)atlasRows;
            vMax = (row + 1) / (float)atlasRows;
        }
        return glm::vec4(uMin, vMin, uMax, vMax);
    }

    float seasonalLeafAttachment(unsigned long long leafKey, bool forceKeep = false) const {
        if(forceKeep) return 1.0f;
        float density = glm::clamp(seasonalLeafDensity, 0.0f, 1.0f);
        if(density >= 0.999f) return 1.0f;

        float oldness = leafOldness(leafKey);
        float effectiveDensity = density;
        if(allowSeasonLeafDrop) {
            // Keep leaves mature through most of fall, then detach with age ordering.
            float dropProgress = glm::clamp((1.0f - density) / 0.96f, 0.0f, 1.0f);
            float holdBoost = (1.0f - dropProgress) * (0.58f - 0.34f * oldness);
            effectiveDensity = density + holdBoost * (1.0f - density);
            effectiveDensity -= 0.10f * oldness * dropProgress;
            effectiveDensity = glm::clamp(effectiveDensity, 0.0f, 1.0f);
        }

        float noise = seasonalLeafNoise(leafKey, randomSeed + 911u);
        float band = allowSeasonLeafDrop
            ? glm::clamp(leafAttachTransitionBand * 0.35f, 0.02f, 0.10f)
            : glm::clamp(leafAttachTransitionBand, 0.02f, 0.45f);
        float t = glm::clamp((effectiveDensity - noise + band) / (2.0f * band), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float smoothLeafAttachment(unsigned long long leafKey, float targetAttachment) {
        targetAttachment = glm::clamp(targetAttachment, 0.0f, 1.0f);

        float previous = 0.0f;
        auto it = leafAttachmentState.find(leafKey);
        if(it != leafAttachmentState.end()) {
            previous = it->second;
        }

        float noise = seasonalLeafNoise(leafKey, randomSeed + 2237u);
        float riseRate = 0.055f + 0.055f * noise;
        // Early spring buds should grow visibly over time instead of popping in.
        if(!allowSeasonLeafDrop && seasonalLeafDensity < 0.55f) {
            riseRate *= 0.45f;
        }
        if(seasonalLeafSizeScale < 0.55f) {
            riseRate *= 0.55f;
        }
        float fallRate = allowSeasonLeafDrop
            ? (0.010f + 0.040f * noise)      // Slower detach to avoid popping.
            : (0.06f + 0.08f * noise);
        float rate = (targetAttachment > previous) ? riseRate : fallRate;

        float next = glm::mix(previous, targetAttachment, glm::clamp(rate, 0.0f, 1.0f));
        if(std::abs(next - targetAttachment) < 5e-4f) {
            next = targetAttachment;
        }
        leafAttachmentState[leafKey] = next;
        return next;
    }

    void appendLeafQuadToMesh(std::shared_ptr<Mesh> &mesh,
                              const glm::vec3& pos,
                              const glm::vec3& leafRight,
                              const glm::vec3& leafUp,
                              float size,
                              int leafVariant,
                              float leafColorVariation) {
        if(!mesh || size <= 1e-4f) return;

        auto& positions = mesh->vertexPositions();
        auto& triangles = mesh->triangleIndices();
        auto& texCoords = mesh->vertexTexCoords();
        auto& leafVariations = mesh->vertexLeafVariations();
        unsigned int baseIndex = (unsigned int)positions.size();

        glm::vec4 uv = computeLeafUVRect(leafVariant);
        float uMin = uv.x, vMin = uv.y, uMax = uv.z, vMax = uv.w;

        glm::vec3 p0 = pos - leafRight * size * 0.5f - leafUp * size * 0.1f;
        glm::vec3 p1 = pos + leafRight * size * 0.5f - leafUp * size * 0.1f;
        glm::vec3 p2 = pos + leafRight * size * 0.5f + leafUp * size * 1.2f;
        glm::vec3 p3 = pos - leafRight * size * 0.5f + leafUp * size * 1.2f;

        positions.push_back(p0);
        positions.push_back(p1);
        positions.push_back(p2);
        positions.push_back(p3);

        texCoords.push_back(glm::vec2(uMin, vMax));
        texCoords.push_back(glm::vec2(uMax, vMax));
        texCoords.push_back(glm::vec2(uMax, vMin));
        texCoords.push_back(glm::vec2(uMin, vMin));

        leafVariations.push_back(leafColorVariation);
        leafVariations.push_back(leafColorVariation);
        leafVariations.push_back(leafColorVariation);
        leafVariations.push_back(leafColorVariation);

        triangles.push_back(glm::uvec3(baseIndex, baseIndex + 1, baseIndex + 2));
        triangles.push_back(glm::uvec3(baseIndex, baseIndex + 2, baseIndex + 3));
    }

    bool ensureFallingLeafCapacity() {
        if(maxFallenLeaves <= 0) return false;
        if((int)fallenLeaves.size() < maxFallenLeaves) return true;
        if(fallenLeaves.empty()) return false;

        int evictIndex = -1;
        float bestScore = -1.0f;
        for(int i = 0; i < (int)fallenLeaves.size(); ++i) {
            const FallingLeaf& l = fallenLeaves[i];
            // Prefer removing the oldest landed leaf first so newly detached leaves
            // are not silently dropped during heavy shedding windows.
            float score = l.landed ? (l.groundAge + 0.35f * l.age) : (0.10f * l.age);
            if(score > bestScore) {
                bestScore = score;
                evictIndex = i;
            }
        }
        if(evictIndex < 0) return false;
        fallenLeaves.erase(fallenLeaves.begin() + evictIndex);
        return true;
    }

    bool spawnFallingLeaf(unsigned long long leafKey, const glm::vec3& startPos, bool forceSpawn = false) {
        if(!allowSeasonLeafDrop && !forceSpawn) return false;
        if(!ensureFallingLeafCapacity()) return false;

        float baseSize = 0.055f * seasonalLeafSizeScale;
        auto sIt = leafLastRenderedSize.find(leafKey);
        if(sIt != leafLastRenderedSize.end()) baseSize = sIt->second;
        baseSize = glm::clamp(baseSize, 0.024f, 0.11f);

        int variant = 0;
        auto vIt = leafLastRenderedVariant.find(leafKey);
        if(vIt != leafLastRenderedVariant.end()) variant = vIt->second;

        float colorVariation = seasonalLeafNoise(leafKey, randomSeed + 6311u);
        auto cIt = leafLastRenderedColorVariation.find(leafKey);
        if(cIt != leafLastRenderedColorVariation.end()) colorVariation = cIt->second;

        float r0 = seasonalLeafNoise(leafKey, randomSeed + 4751u);
        float r1 = seasonalLeafNoise(leafKey, randomSeed + 4761u);
        float r2 = seasonalLeafNoise(leafKey, randomSeed + 4771u);
        float r3 = seasonalLeafNoise(leafKey, randomSeed + 4781u);

        float driftAngle = r2 * 2.0f * (float)M_PI;
        float sideSpeed = 0.03f + 0.10f * r3;
        FallingLeaf leaf;
        // Keep exact detach point so the same branch leaf is the one that falls.
        leaf.pos = startPos;
        leaf.vel = glm::vec3(
            std::cos(driftAngle) * sideSpeed,
            -(0.05f + 0.15f * r1),
            std::sin(driftAngle) * sideSpeed);
        leaf.size = baseSize;
        leaf.yaw = driftAngle;
        leaf.spin = (r3 - 0.5f) * 1.6f;
        leaf.wobblePhase = r0 * 2.0f * (float)M_PI;
        leaf.preFadeDelay = 4.0f + 2.4f * r2;
        leaf.fadeDuration = 3.8f + 2.4f * r1;
        leaf.variant = variant;
        leaf.colorVariation = colorVariation;
        leaf.detachOrientDuration = 0.75f + 0.65f * r1;

        auto rIt = leafLastRenderedRight.find(leafKey);
        auto uIt = leafLastRenderedUp.find(leafKey);
        if(rIt != leafLastRenderedRight.end() && uIt != leafLastRenderedUp.end()) {
            leaf.detachRight = glm::normalize(rIt->second);
            leaf.detachUp = glm::normalize(uIt->second);
        } else {
            leaf.detachRight = glm::normalize(glm::vec3(std::cos(driftAngle), 0.0f, std::sin(driftAngle)));
            leaf.detachUp = glm::normalize(glm::vec3(0.0f, 0.92f, 0.38f));
        }
        leaf.detachUp = glm::normalize(leaf.detachUp - glm::dot(leaf.detachUp, leaf.detachRight) * leaf.detachRight);
        if(glm::length(leaf.detachUp) < 1e-5f) {
            leaf.detachUp = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        leaf.landRightStart = leaf.detachRight;
        leaf.landUpStart = leaf.detachUp;
        fallenLeaves.push_back(leaf);
        return true;
    }

    void detachAllAttachedLeavesToFalling() {
        if(leafSmoothedPositions.empty()) return;

        for(const auto& kv : leafSmoothedPositions) {
            spawnFallingLeaf(kv.first, kv.second, true);
        }

        leafSmoothedPositions.clear();
        leafAttachmentState.clear();
        leafLastRenderedSize.clear();
        leafLastRenderedVariant.clear();
        leafLastRenderedColorVariation.clear();
        leafLastRenderedRight.clear();
        leafLastRenderedUp.clear();
        activeLeafKeysThisFrame.clear();

        if(leafMesh) {
            leafMesh->clear();
        }
    }

    bool hasFallenLeaves() const {
        return !fallenLeaves.empty();
    }

    void clearFallenLeaves() {
        fallenLeaves.clear();
        if(fallenLeafMesh && !fallenLeafMesh->triangleIndices().empty()) {
            fallenLeafMesh->clear();
        }
    }

    void updateFallingLeaves(float dt) {
        if(!fallenLeafMesh) return;
        float safeDt = glm::max(0.0f, dt);
        if(safeDt <= 0.0f && fallenLeaves.empty()) return;

        const float gravity = 0.58f;
        const float drag = 1.65f;
        const float terminalSpeed = 0.42f;
        const float groundOffset = 0.0008f;
        const float maxGroundFlatten = 0.18f; // 0 = keep touchdown tilt, 1 = fully flat
        const float maxStep = 1.0f / 90.0f;
        int simSteps = glm::max(1, (int)std::ceil(safeDt / maxStep));
        float stepDt = (simSteps > 0) ? (safeDt / (float)simSteps) : 0.0f;
        auto minLeafCornerYOffset = [](float size, const glm::vec3& right, const glm::vec3& up) {
            float y0 = (-0.5f * right.y - 0.1f * up.y) * size;
            float y1 = ( 0.5f * right.y - 0.1f * up.y) * size;
            float y2 = ( 0.5f * right.y + 1.2f * up.y) * size;
            float y3 = (-0.5f * right.y + 1.2f * up.y) * size;
            return glm::min(glm::min(y0, y1), glm::min(y2, y3));
        };
        auto leafCornerYOffset = [](float size, const glm::vec3& right, const glm::vec3& up, int corner) {
            switch(corner) {
                case 1: return ( 0.5f * right.y - 0.1f * up.y) * size;
                case 2: return ( 0.5f * right.y + 1.2f * up.y) * size;
                case 3: return (-0.5f * right.y + 1.2f * up.y) * size;
                case 0:
                default: return (-0.5f * right.y - 0.1f * up.y) * size;
            }
        };
        auto lowestLeafCorner = [](const glm::vec3& right, const glm::vec3& up) {
            float y0 = (-0.5f * right.y - 0.1f * up.y);
            float y1 = ( 0.5f * right.y - 0.1f * up.y);
            float y2 = ( 0.5f * right.y + 1.2f * up.y);
            float y3 = (-0.5f * right.y + 1.2f * up.y);
            int idx = 0;
            float best = y0;
            if(y1 < best) { best = y1; idx = 1; }
            if(y2 < best) { best = y2; idx = 2; }
            if(y3 < best) { idx = 3; }
            return idx;
        };
        auto orthonormalizeBasis = [](glm::vec3& rightOut, glm::vec3& upOut) {
            if(glm::length(rightOut) < 1e-5f) {
                rightOut = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                rightOut = glm::normalize(rightOut);
            }
            upOut = upOut - glm::dot(upOut, rightOut) * rightOut;
            if(glm::length(upOut) < 1e-5f) {
                glm::vec3 fallback = (std::abs(rightOut.y) < 0.99f)
                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                    : glm::vec3(0.0f, 0.0f, 1.0f);
                upOut = fallback - glm::dot(fallback, rightOut) * rightOut;
                if(glm::length(upOut) < 1e-5f) {
                    upOut = glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }
            upOut = glm::normalize(upOut);
        };
        auto computeAirborneOrientation = [&](const FallingLeaf& leaf, glm::vec3& rightOut, glm::vec3& upOut, float fallenLeafGroundY) {
            glm::vec3 travel(leaf.vel.x, 0.0f, leaf.vel.z);
            float travelSpeed = glm::length(travel);
            // Raised threshold: avoids yaw-based jitter when nearly stopped
            if(travelSpeed < 0.04f) {
                travel = glm::vec3(std::cos(leaf.yaw), 0.0f, std::sin(leaf.yaw));
            } else {
                travel = glm::normalize(travel);
            }
            // Damp flap as the leaf slows down (horizontal speed) AND approaches ground.
            // Without this, a nearly-stopped leaf near the ground flips tip up/down rapidly
            // at 0.83 Hz — the "dying fish" effect.
            float speedDamp  = glm::clamp(travelSpeed / 0.12f, 0.0f, 1.0f);
            float groundDist = glm::max(0.0f, leaf.pos.y - fallenLeafGroundY);
            float groundDamp = glm::smoothstep(0.0f, 0.28f, groundDist);
            float flapAmp = 0.30f * (0.55f + 0.95f * windBlend);
            float flap = flapAmp * (speedDamp * groundDamp) * std::sin(leaf.wobblePhase + leaf.age * 5.2f);
            rightOut = glm::normalize(glm::vec3(-travel.z, 0.0f, travel.x));
            upOut = glm::normalize(travel + glm::vec3(0.0f, flap, 0.0f));
        };
        auto computeDisplayedAirborneOrientation = [&](const FallingLeaf& leaf, glm::vec3& rightOut, glm::vec3& upOut) {
            glm::vec3 airRight, airUp;
            computeAirborneOrientation(leaf, airRight, airUp, fallenLeafGroundY);
            float orientT = glm::clamp(
                leaf.age / glm::max(0.12f, leaf.detachOrientDuration),
                0.0f, 1.0f);
            orientT = orientT * orientT * (3.0f - 2.0f * orientT);
            rightOut = glm::mix(leaf.detachRight, airRight, orientT);
            upOut = glm::mix(leaf.detachUp, airUp, orientT);
            orthonormalizeBasis(rightOut, upOut);
        };

        for(int step = 0; step < simSteps; ++step) {
            for(FallingLeaf& leaf : fallenLeaves) {
                if(!leaf.landed) {
                    glm::vec3 prevPos = leaf.pos;
                    glm::vec3 prevRight, prevUp;
                    computeDisplayedAirborneOrientation(leaf, prevRight, prevUp);
                    float prevMinY = prevPos.y + minLeafCornerYOffset(leaf.size, prevRight, prevUp);
                    float windPhase = leaf.wobblePhase + leaf.age * (0.9f + 0.8f * seasonLeafDropStrength);
                    glm::vec3 windVec(std::cos(windPhase), 0.0f, std::sin(windPhase * 1.13f));
                    float windForceScale = 0.35f + 1.25f * windBlend;
                    leaf.vel += windVec * (0.22f * windForceScale * stepDt);
                    leaf.vel.y -= gravity * stepDt;
                    leaf.vel *= (1.0f / (1.0f + drag * stepDt));
                    leaf.vel.y = glm::max(leaf.vel.y, -terminalSpeed);
                    leaf.pos += leaf.vel * stepDt;
                    float tumble = 0.35f + 0.25f * windBlend;
                    leaf.yaw += (leaf.spin + tumble * std::sin(leaf.wobblePhase + leaf.age * 2.7f)) * stepDt;
                    leaf.age += stepDt;

                    glm::vec3 currRight, currUp;
                    computeDisplayedAirborneOrientation(leaf, currRight, currUp);
                    float currMinOffset = minLeafCornerYOffset(leaf.size, currRight, currUp);
                    float currMinY = leaf.pos.y + currMinOffset;

                    bool crossedGround = (prevMinY > fallenLeafGroundY && currMinY <= fallenLeafGroundY);
                    if(crossedGround || currMinY <= fallenLeafGroundY) {
                        glm::vec3 touchRight = currRight;
                        glm::vec3 touchUp = currUp;

                        if(crossedGround) {
                            float denom = glm::max(1e-5f, prevMinY - currMinY);
                            float hitT = glm::clamp((prevMinY - fallenLeafGroundY) / denom, 0.0f, 1.0f);
                            leaf.pos = glm::mix(prevPos, leaf.pos, hitT);
                            touchRight = glm::mix(prevRight, currRight, hitT);
                            touchUp = glm::mix(prevUp, currUp, hitT);
                            orthonormalizeBasis(touchRight, touchUp);
                        }
                        leaf.supportCorner = lowestLeafCorner(touchRight, touchUp);
                        float supportOffset = leafCornerYOffset(leaf.size, touchRight, touchUp, leaf.supportCorner);
                        leaf.pos.y = fallenLeafGroundY - supportOffset + groundOffset;
                        leaf.landed = true;
                        leaf.vel = glm::vec3(leaf.vel.x * 0.18f, 0.0f, leaf.vel.z * 0.18f);
                        leaf.groundAge = 0.0f;
                        leaf.landSettle = 0.0f;
                        leaf.landRightStart = touchRight;
                        leaf.landUpStart = touchUp;
                    }
                } else {
                    leaf.groundAge += stepDt * glm::max(0.05f, fallenLeafGroundAgingRate);
                    leaf.age += stepDt;
                    leaf.yaw += leaf.spin * 0.03f * (1.0f - leaf.landSettle) * stepDt;
                    leaf.landSettle = glm::min(1.0f, leaf.landSettle + stepDt * 0.55f);
                    // Tiny ground glide with friction avoids a hard "stick on contact" jump.
                    leaf.pos += leaf.vel * stepDt;
                    leaf.vel *= (1.0f / (1.0f + 8.0f * stepDt));
                    if(glm::length(leaf.vel) < 0.0015f) {
                        leaf.vel = glm::vec3(0.0f);
                    }
                    glm::vec3 flatRight = glm::normalize(glm::vec3(std::cos(leaf.yaw), 0.0f, std::sin(leaf.yaw)));
                    glm::vec3 flatUp = glm::normalize(glm::vec3(-std::sin(leaf.yaw), 0.0f, std::cos(leaf.yaw)));
                    float settleT = leaf.landSettle * leaf.landSettle * (3.0f - 2.0f * leaf.landSettle);
                    float flattenT = maxGroundFlatten * settleT;
                    glm::vec3 settleRight = glm::mix(leaf.landRightStart, flatRight, flattenT);
                    glm::vec3 settleUp = glm::mix(leaf.landUpStart, flatUp, flattenT);
                    orthonormalizeBasis(settleRight, settleUp);
                    float settleSupportOffset = leafCornerYOffset(leaf.size, settleRight, settleUp, leaf.supportCorner);
                    leaf.pos.y = fallenLeafGroundY - settleSupportOffset + groundOffset;
                }
            }
        }

        fallenLeaves.erase(
            std::remove_if(fallenLeaves.begin(), fallenLeaves.end(),
                [](const FallingLeaf& leaf) {
                    if(!leaf.landed) return false;
                    float fadeT = glm::clamp(
                        (leaf.groundAge - leaf.preFadeDelay) / glm::max(0.01f, leaf.fadeDuration),
                        0.0f, 1.0f);
                    return fadeT >= 1.0f;
                }),
            fallenLeaves.end());

        if(fallenLeaves.empty()) {
            if(!fallenLeafMesh->triangleIndices().empty()) {
                fallenLeafMesh->clear();
            }
            return;
        }

        fallenLeafMesh->clear();
        const float drawGroundOffset = 0.0008f;
        for(const FallingLeaf& leaf : fallenLeaves) {
            float shrink = 1.0f;
            if(leaf.landed) {
                float fadeT = glm::clamp(
                    (leaf.groundAge - leaf.preFadeDelay) / glm::max(0.01f, leaf.fadeDuration),
                    0.0f, 1.0f);
                float remain = 1.0f - (fadeT * fadeT * (3.0f - 2.0f * fadeT));
                shrink = remain * remain;
            }
            if(shrink <= 0.01f) continue;

            float size = leaf.size * shrink;
            if(size < 0.018f) continue;
            glm::vec3 leafRight, leafUp;
            if(leaf.landed) {
                // Landed leaves keep most touchdown tilt; only a slight flattening bias is applied.
                glm::vec3 flatRight = glm::normalize(glm::vec3(std::cos(leaf.yaw), 0.0f, std::sin(leaf.yaw)));
                glm::vec3 flatUp = glm::normalize(glm::vec3(-std::sin(leaf.yaw), 0.0f, std::cos(leaf.yaw)));
                float settleT = leaf.landSettle * leaf.landSettle * (3.0f - 2.0f * leaf.landSettle);
                float flattenT = maxGroundFlatten * settleT;
                leafRight = glm::mix(leaf.landRightStart, flatRight, flattenT);
                leafUp = glm::mix(leaf.landUpStart, flatUp, flattenT);
            } else {
                // Airborne orientation blends from branch-attached pose to aerodynamic tumble.
                computeDisplayedAirborneOrientation(leaf, leafRight, leafUp);
            }
            orthonormalizeBasis(leafRight, leafUp);
            glm::vec3 drawPos = leaf.pos;
            if(leaf.landed) {
                float drawSupportOffset = leafCornerYOffset(size, leafRight, leafUp, leaf.supportCorner);
                drawPos.y = fallenLeafGroundY - drawSupportOffset + drawGroundOffset;
            }

            appendLeafQuadToMesh(
                fallenLeafMesh, drawPos, leafRight, leafUp, size, leaf.variant, leaf.colorVariation);
        }

        fallenLeafMesh->recomputePerVertexNormals();
        fallenLeafMesh->init();
    }

    glm::vec3 smoothLeafPosition(unsigned long long leafKey, const glm::vec3& targetPos) {
        activeLeafKeysThisFrame.insert(leafKey);
        auto it = leafSmoothedPositions.find(leafKey);
        if(it == leafSmoothedPositions.end()) {
            leafSmoothedPositions[leafKey] = targetPos;
            return targetPos;
        }
        float localSmoothing = glm::mix(leafPositionSmoothing, 0.03f, glm::clamp(seasonalStability, 0.0f, 1.0f));
        it->second = glm::mix(it->second, targetPos, localSmoothing);
        return it->second;
    }

    void buildCanopyAttractors(std::vector<glm::vec3>& out) const {
        out.clear();
        if(!useSpaceColonizationGuidance || canopyAttractorCount <= 0) return;

        out.reserve((size_t)canopyAttractorCount);
        int attempts = 0;
        int maxAttempts = canopyAttractorCount * 18;
        while((int)out.size() < canopyAttractorCount && attempts < maxAttempts) {
            float rx = pseudoRandom((int)randomSeed + 5001, attempts * 3 + 0) * 2.0f - 1.0f;
            float ry = pseudoRandom((int)randomSeed + 5002, attempts * 3 + 1) * 2.0f - 1.0f;
            float rz = pseudoRandom((int)randomSeed + 5003, attempts * 3 + 2) * 2.0f - 1.0f;
            attempts++;

            glm::vec3 u(rx, ry, rz);
            if(glm::dot(u, u) > 1.0f) continue;

            glm::vec3 p = canopyCenter + glm::vec3(
                u.x * canopyRadii.x,
                u.y * canopyRadii.y,
                u.z * canopyRadii.z
            );

            // Elm crowns are broad and lifted; avoid very low attractors.
            if(p.y < -0.20f) continue;

            float radial = std::sqrt(p.x * p.x + p.z * p.z);
            float yNorm = glm::clamp((p.y + 1.0f) / 2.0f, 0.0f, 1.0f);
            float minCore = canopyInnerRadius * (0.65f + 0.35f * (1.0f - yNorm));
            if(radial < minCore) continue;

            out.push_back(p);
        }
    }

    std::string buildLSystemSentence() const {
        // Stochastic, low-copy branching: closer to natural recursive growth
        // (avoids both straight poles and dense "bucket stacks").
        std::string sentence = "A";
        for(int it = 0; it < lsystemIterations; ++it) {
            std::string next;
            next.reserve(sentence.size() * 5);
            for(int i = 0; i < (int)sentence.size(); ++i) {
                char c = sentence[i];
                if(c == 'A') {
                    float r = pseudoRandom((int)randomSeed + it * 113, i * 31 + it * 17);
                    float stage = (lsystemIterations > 1)
                        ? (float)it / (float)(lsystemIterations - 1)
                        : 1.0f;
                    float trunkKeep = glm::clamp(
                        glm::mix(0.90f, 0.28f, stage) * apicalDominance + 0.10f,
                        0.18f, 0.92f
                    );

                    if(r < trunkKeep) {
                        next += "FA";
                    } else {
                        // Multi-child branching rules to increase branches per node.
                        float splitR = (r - trunkKeep) / glm::max(1e-5f, 1.0f - trunkKeep);
                        if(splitR < 0.16f) {
                            next += "F[+A][-A]A";
                        } else if(splitR < 0.34f) {
                            next += "F[+A][-A][&A]A";
                        } else if(splitR < 0.52f) {
                            next += "F[+A][-A][^A]A";
                        } else if(splitR < 0.68f) {
                            next += "F[+A][-A][/A]A";
                        } else if(splitR < 0.82f) {
                            next += "F[+A][-A][&A][^A]A";
                        } else if(splitR < 0.92f) {
                            next += "F[+A][-A][&A][^A][/A]A";
                        } else {
                            next += "F[+A][-A][&A][^A][/A][\\A]A";
                        }
                    }
                } else {
                    next.push_back(c);
                }
            }
            sentence.swap(next);
            if(sentence.size() > 90000) {
                break;
            }
        }
        return sentence;
    }

    void initializeGrowthStages() {
        if(!useLSystem) return;
        growthStage = 0;
        maxGrowthStage = 0;
        for(const Branch& b : branches) {
            maxGrowthStage = std::max(maxGrowthStage, b.activationStage);
        }
        for(Branch& b : branches) {
            if(b.activationStage <= growthStage) {
                b.age = 0.0f;
                b.isGrowing = true;
            } else {
                b.age = -lsystemDepthDelay * (float)(b.activationStage - growthStage);
                b.isGrowing = false;
                b.endPos = b.startPos;
            }
        }
    }

    void advanceGrowthStage() {
        if(!useLSystem) return;

        if(growthStage < maxGrowthStage) {
            growthStage++;
            int activated = 0;
            for(int bi = 0; bi < (int)branches.size(); ++bi) {
                Branch& b = branches[bi];
                if(b.activationStage == growthStage) {
                    b.isGrowing = true;
                    // Small per-branch lag removes mechanical "all-at-once" growth.
                    float startLag = 0.20f * pseudoRandom((int)randomSeed + growthStage * 131, bi + 17);
                    b.age = -startLag;
                    b.endPos = b.startPos;
                    activated++;
                }
            }
            std::cout << "[Plant] Growth stage " << growthStage << "/" << maxGrowthStage
                      << " unlocked (" << activated << " branches)." << std::endl;
        } else {
            int resumed = 0;
            for(Branch& b : branches) {
                float len = glm::length(b.endPos - b.startPos);
                if(len < b.maxLength - 1e-4f) {
                    b.isGrowing = true;
                    resumed++;
                }
            }
            std::cout << "[Plant] Final stage already unlocked; resumed " << resumed
                      << " unfinished branches." << std::endl;
        }
        updateMesh();
    }

    void generateLegacyEquivalentLSystem() {
        branches.clear();
        growthCapReachedLogged = false;
        leafCapReachedLogged = false;

        branches.push_back(Branch(
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            0.08f,
            0,
            0.8f,
            -1,
            0
        ));
        branches[0].branchingTried = true;
        branches[0].endPos = branches[0].startPos;

        bool oldSuppress = suppressBranchLogs;
        suppressBranchLogs = true;

        // Build exactly the same branching topology as the legacy growth model:
        // each node spawns children using createChildBranches() rules.
        for(int i = 0; i < (int)branches.size(); ++i) {
            if((int)branches.size() >= maxBranches) break;
            if(branches[i].depth >= maxDepth) continue;
            createChildBranches(i);
        }

        suppressBranchLogs = oldSuppress;

        // Uniform per-level activation: all branches of same depth start together.
        for(int i = 0; i < (int)branches.size(); ++i) {
            Branch& b = branches[i];
            int parentStage = 0;
            if(b.parentIndex >= 0 && b.parentIndex < (int)branches.size()) {
                parentStage = branches[b.parentIndex].activationStage;
            }
            b.activationStage = (i == 0) ? 0 : glm::max(parentStage + 1, b.depth);
        }

        for(Branch& b : branches) {
            b.age = 0.0f;
            b.isGrowing = false;
            b.endPos = b.startPos;
            b.branchingTried = true;
        }

        std::cout << "[Plant] Built legacy-equivalent L-system topology ("
                  << branches.size() << " branches)" << std::endl;
    }

    void generateFromLSystem() {
        if(useLegacyEquivalentLSystem) {
            generateLegacyEquivalentLSystem();
            return;
        }

        struct TurtleState {
            glm::vec3 pos;
            glm::quat rot;
            int depth;
            int stemStep;
            int parentBranch;
        };

        branches.clear();
        growthCapReachedLogged = false;
        leafCapReachedLogged = false;

        std::vector<TurtleState> stack;
        stack.reserve(512);
        TurtleState turtle{
            glm::vec3(0.0f, -1.0f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            0,
            0,
            -1
        };

        auto rotateLocal = [&](const glm::vec3& localAxis, float angleRad) {
            glm::vec3 worldAxis = glm::normalize(turtle.rot * localAxis);
            turtle.rot = glm::normalize(glm::angleAxis(angleRad, worldAxis) * turtle.rot);
        };

        const std::string sentence = buildLSystemSentence();
        std::vector<glm::vec3> canopyAttractors;
        buildCanopyAttractors(canopyAttractors);
        std::vector<float> canopyOccupancy(canopyAttractors.size(), 0.0f);
        glm::vec3 tropismDir = glm::normalize(tropismDirection);

        auto applyDirectionalGuidance = [&](const glm::vec3& pos, glm::vec3 dir, int depth, int commandId) {
            if(useSpaceColonizationGuidance && !canopyAttractors.empty()) {
                glm::vec3 acc(0.0f);
                float wSum = 0.0f;
                float influenceR2 = canopyInfluenceRadius * canopyInfluenceRadius;
                for(size_t ai = 0; ai < canopyAttractors.size(); ++ai) {
                    glm::vec3 toTarget = canopyAttractors[ai] - pos;
                    float d2 = glm::dot(toTarget, toTarget);
                    if(d2 < 1e-6f || d2 > influenceR2) continue;

                    glm::vec3 n = toTarget * glm::inversesqrt(d2);
                    float front = glm::dot(n, dir);
                    if(front < -0.25f) continue;

                    // Bug 2 fix: hard kill zone — skip attractors that have been consumed
                    // (occupancy >= 1.5 means a branch tip came within kill radius).
                    if(canopyOccupancy[ai] >= 1.5f) continue;

                    float distWeight = 1.0f - d2 / influenceR2;
                    distWeight *= distWeight;
                    float frontWeight = 0.30f + glm::max(0.0f, front);
                    float occupancyWeight = 1.0f / (1.0f + canopyOccupancy[ai] * 0.7f);
                    float w = distWeight * frontWeight * occupancyWeight;
                    acc += n * w;
                    wSum += w;
                }
                if(wSum > 1e-6f) {
                    glm::vec3 colonizationDir = glm::normalize(acc / wSum);
                    float depthFactor = glm::clamp((float)depth / 5.0f, 0.30f, 1.0f);
                    float blend = canopyGuidanceStrength * depthFactor;
                    dir = glm::normalize(glm::mix(dir, colonizationDir, blend));
                }
            }

            float apical = apicalDominance * glm::clamp(1.0f - 0.12f * (float)depth, 0.20f, 1.0f);
            glm::vec3 apicalTarget = glm::normalize(glm::vec3(dir.x * 0.30f, 1.0f, dir.z * 0.30f));
            dir = glm::normalize(glm::mix(dir, apicalTarget, 0.12f * apical));

            float tropismBlend = tropismStrength * glm::clamp(0.35f + 0.15f * (float)depth, 0.35f, 1.0f);
            dir = glm::normalize(glm::mix(dir, tropismDir, tropismBlend));

            float micro = glm::radians((pseudoRandom((int)randomSeed + 6900, commandId) - 0.5f) * 6.0f);
            glm::vec3 refAxis = (glm::abs(dir.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 axis = glm::cross(dir, refAxis);
            if(glm::dot(axis, axis) > 1e-8f) {
                axis = glm::normalize(axis);
                glm::mat4 microRot = glm::rotate(glm::mat4(1.0f), micro, axis);
                dir = glm::normalize(glm::vec3(microRot * glm::vec4(dir, 0.0f)));
            }

            return dir;
        };

        auto claimNearestAttractor = [&](const glm::vec3& tipPos) {
            if(canopyAttractors.empty()) return;
            float influenceR2 = canopyInfluenceRadius * canopyInfluenceRadius;
            int bestIdx = -1;
            float bestD2 = influenceR2;
            for(int ai = 0; ai < (int)canopyAttractors.size(); ++ai) {
                glm::vec3 d = canopyAttractors[ai] - tipPos;
                float d2 = glm::dot(d, d);
                if(d2 < bestD2) {
                    bestD2 = d2;
                    bestIdx = ai;
                }
            }
            if(bestIdx >= 0) {
                float killRadius = 0.07f;
                if(bestD2 < killRadius * killRadius) {
                    // Hard kill: permanently mark this attractor as consumed (>= 1.5 threshold).
                    canopyOccupancy[(size_t)bestIdx] = 2.0f;
                } else {
                    canopyOccupancy[(size_t)bestIdx] += 0.35f;
                }
            }
        };

        bool stoppedByCap = false;
        int cmd = 0;
        int generatedDepth = 0;
        for(char c : sentence) {
            if(c == 'F') {
                if((int)branches.size() >= maxBranches) {
                    stoppedByCap = true;
                    break;
                }

                float lenJitter = 0.85f + 0.30f * pseudoRandom((int)randomSeed + 700, cmd);
                float segmentLen = lsystemBaseSegment * std::pow(lsystemLengthDecay, (float)turtle.depth) * lenJitter;
                float stemLenTaper = std::pow(0.975f, (float)turtle.stemStep);
                segmentLen *= stemLenTaper;
                segmentLen = glm::max(segmentLen, 0.022f);

                float stemTaper = std::pow(0.88f, (float)turtle.stemStep);
                float radius = lsystemStartRadius *
                               std::pow(lsystemRadiusDecay, (float)turtle.depth) *
                               stemTaper;
                radius = glm::max(radius, 0.002f);

                glm::vec3 dir = glm::normalize(turtle.rot * glm::vec3(0.0f, 1.0f, 0.0f));
                // Elm-style widening: progressively bias branches away from trunk axis.
                if(turtle.depth >= 1) {
                    glm::vec3 outward(turtle.pos.x, 0.0f, turtle.pos.z);
                    float outLen = glm::length(outward);
                    if(outLen < 1e-5f) {
                        float az = pseudoRandom((int)randomSeed + 2600, cmd) * 2.0f * (float)M_PI;
                        outward = glm::vec3(std::cos(az), 0.0f, std::sin(az));
                    } else {
                        outward /= outLen;
                    }
                    float upWeight = 0.68f - 0.09f * glm::clamp((float)turtle.depth, 0.0f, 4.0f);
                    glm::vec3 outwardTarget = glm::normalize(outward + glm::vec3(0.0f, upWeight, 0.0f));
                    float spread = glm::clamp(0.24f + 0.06f * (float)turtle.depth, 0.24f, 0.52f);
                    dir = glm::normalize(glm::mix(dir, outwardTarget, spread));
                }
                dir = applyDirectionalGuidance(turtle.pos, dir, turtle.depth, cmd);

                // Bug 1 fix: update turtle.rot to match the guidance-corrected direction,
                // so subsequent rotations (+/-/&/^) operate in the correct local frame.
                {
                    glm::vec3 oldDir = glm::normalize(turtle.rot * glm::vec3(0.0f, 1.0f, 0.0f));
                    float cosA = glm::clamp(glm::dot(oldDir, dir), -1.0f, 1.0f);
                    if(cosA < 0.9999f) {
                        glm::vec3 axis = glm::cross(oldDir, dir);
                        float axisLen = glm::length(axis);
                        if(axisLen > 1e-6f) {
                            axis /= axisLen;
                            turtle.rot = glm::normalize(glm::angleAxis(std::acos(cosA), axis) * turtle.rot);
                        }
                    }
                }

                int activation = turtle.depth + (turtle.stemStep / 3);
                branches.push_back(Branch(
                    turtle.pos,
                    dir,
                    radius,
                    turtle.depth,
                    segmentLen,
                    turtle.parentBranch,
                    activation
                ));

                int branchIndex = (int)branches.size() - 1;
                Branch& branch = branches[branchIndex];
                branch.age = 0.0f;
                branch.branchingTried = true;
                branch.endPos = branch.startPos; // Hide dormant segments at initialization.

                if(turtle.parentBranch >= 0 && turtle.parentBranch < (int)branches.size()) {
                    branches[turtle.parentBranch].childBranches.push_back(branchIndex);
                }

                turtle.pos += dir * segmentLen;
                claimNearestAttractor(turtle.pos);
                turtle.parentBranch = branchIndex;
                turtle.stemStep += 1;
                generatedDepth = std::max(generatedDepth, turtle.depth);
            } else if(c == '[') {
                stack.push_back(turtle);
                turtle.depth += 1;
                // Bug 4 fix: do NOT increment stemStep at '['. stemStep counts drawn segments
                // (F commands), not bracket depth. Double-incrementing here made sub-branches
                // too short/thin because pow(0.975, stemStep) taper fired twice per branch node.
                // Randomize branch azimuth so canopy does not stay in one plane.
                float az = glm::radians((pseudoRandom((int)randomSeed + 3400, cmd + turtle.depth * 37) - 0.5f) * 240.0f);
                rotateLocal(glm::vec3(0.0f, 1.0f, 0.0f), az);
            } else if(c == ']') {
                if(!stack.empty()) {
                    turtle = stack.back();
                    stack.pop_back();
                }
            } else if(c == '+' || c == '-' || c == '&' || c == '^' || c == '\\' || c == '/') {
                float angleJitter = 0.85f + 0.30f * pseudoRandom((int)randomSeed + 1700, cmd);
                float angle = glm::radians(lsystemAngleDeg * angleJitter);
                if(c == '-') angle = -angle;
                if(c == '^' || c == '/') angle = -angle;

                if(c == '+' || c == '-') {
                    rotateLocal(glm::vec3(0.0f, 0.0f, 1.0f), angle); // yaw
                } else if(c == '&' || c == '^') {
                    rotateLocal(glm::vec3(1.0f, 0.0f, 0.0f), angle); // pitch
                } else {
                    rotateLocal(glm::vec3(0.0f, 1.0f, 0.0f), angle); // roll
                }
            }
            cmd++;
        }

        if(stoppedByCap && !growthCapReachedLogged) {
            std::cout << "[Plant] Growth cap reached (" << maxBranches
                      << " branches) during L-system generation." << std::endl;
            growthCapReachedLogged = true;
        }

        if(branches.empty()) {
            branches.push_back(Branch(
                glm::vec3(0, -1, 0),
                glm::vec3(0, 1, 0),
                0.08f,
                0,
                0.8f
            ));
        }

        maxDepth = std::max(maxDepth, generatedDepth);
    }
    
    void update(float dt) {
        bool anyGrowing = false;
        bool anyLeafMaturing = false;
        
        int currentSize = (int)branches.size();
        for(int i = 0; i < currentSize; ++i) {
            Branch& branch = branches[i];

            // Bug 5 fix: only reattach a branch to its parent tip once it becomes active
            // (activationStage <= growthStage). Dormant branches must not slide along the
            // parent's growing tip — that causes visible popping when they activate.
            float currentLength = glm::length(branch.endPos - branch.startPos);
            if(branch.parentIndex >= 0 && branch.parentIndex < (int)branches.size()) {
                const Branch& parent = branches[branch.parentIndex];
                glm::vec3 parentSeg = parent.endPos - parent.startPos;
                branch.startPos = parent.startPos + parentSeg * branch.parentAttachT;
                branch.endPos = branch.startPos + branch.growthDirection * currentLength;
            }

            if(useLSystem && branch.activationStage > growthStage) {
                branch.endPos = branch.startPos;
                continue;
            }

            branch.age += dt;
            if(branch.depth >= 2 && branch.age > 0.0f && branch.age < 2.0f) {
                anyLeafMaturing = true;
            }
            
            if(branch.isGrowing) {
                if(branch.age < 0.0f) {
                    continue; // depth delay: keep branch dormant until its scheduled start.
                }
                anyGrowing = true;

                currentLength = glm::length(branch.endPos - branch.startPos);
                if(currentLength < branch.maxLength) {
                    float growthAmount = growthSpeed * dt;
                    branch.endPos += branch.growthDirection * growthAmount;
                    
                    currentLength = glm::length(branch.endPos - branch.startPos);
                    if(currentLength > branch.maxLength) {
                        branch.endPos = branch.startPos + branch.growthDirection * branch.maxLength;
                        branch.isGrowing = false;
                        std::cout << "[Plant] Branch " << i << " finished growing" << std::endl;
                    }
                }
                
                if(!useLSystem && branch.age > branchingAge && !branch.branchingTried && branch.depth < maxDepth) {
                    createChildBranches(i);
                    currentSize = (int)branches.size();
                }
            }
        }
        
        if(anyGrowing || anyLeafMaturing) {
            updateMesh();
        }
    }
    
    void createChildBranches(int parentIndex) {
        if(parentIndex < 0 || parentIndex >= (int)branches.size()) return;
        branches[parentIndex].branchingTried = true;

        if((int)branches.size() >= maxBranches) {
            if(!growthCapReachedLogged) {
                std::cout << "[Plant] Growth cap reached (" << maxBranches
                          << " branches). Stopping new branch creation." << std::endl;
                growthCapReachedLogged = true;
            }
            return;
        }

        const Branch parent = branches[parentIndex];
        
        float childLength = parent.maxLength * lengthDecay;
        float childRadius = parent.radius * radiusDecay;
        int childDepth = parent.depth + 1;
        
        float angleRad = glm::radians(branchAngle);
        glm::vec3 parentDir = glm::normalize(parent.growthDirection);
        
        glm::vec3 perpendicular;
        if(glm::abs(parentDir.y) < 0.9f) {
            perpendicular = glm::normalize(glm::cross(parentDir, glm::vec3(0, 1, 0)));
        } else {
            perpendicular = glm::normalize(glm::cross(parentDir, glm::vec3(1, 0, 0)));
        }

        // Seed all legacy-branch randomness with the per-tree randomSeed so R creates a fresh tree.
        int seedBase = (int)(randomSeed ^ (unsigned int)(parentIndex * 1664525u + 1013904223u));
        auto randBranch = [&](int k) {
            return pseudoRandom(seedBase, k);
        };
        
        int localBranchesPerNode = branchesPerNode;
        if(useLSystem) {
            float rCount = randBranch(910);
            if(rCount < 0.28f) localBranchesPerNode = branchesPerNode - 1;
            else if(rCount > 0.82f) localBranchesPerNode = branchesPerNode + 1;
            localBranchesPerNode = glm::clamp(localBranchesPerNode, 2, 6);
        }

        int createdChildren = 0;
        float parentAzimuthPhase = useLSystem ? (randBranch(777) * 2.0f * M_PI) : 0.0f;
        for(int i = 0; i < localBranchesPerNode; ++i) {
            if((int)branches.size() >= maxBranches) {
                if(!growthCapReachedLogged) {
                    std::cout << "[Plant] Growth cap reached (" << maxBranches
                              << " branches). Stopping new branch creation." << std::endl;
                    growthCapReachedLogged = true;
                }
                break;
            }

            float baseAngleAround = parentAzimuthPhase + (2.0f * M_PI * i) / localBranchesPerNode;
            float randomOffset = (randBranch(i) - 0.5f) * (useLSystem ? 1.15f : 0.5f);
            float angleAround = baseAngleAround + randomOffset;
            
            float tiltVariation = 1.0f + (randBranch(i + 100) - 0.5f) * (useLSystem ? 0.75f : 0.30f);
            float tiltAngle = angleRad * tiltVariation;
            
            glm::mat4 rotAroundParent = glm::rotate(glm::mat4(1.0f), angleAround, parentDir);
            glm::vec3 outward = glm::normalize(glm::vec3(rotAroundParent * glm::vec4(perpendicular, 0.0f)));
            
            glm::mat4 tiltRotation = glm::rotate(glm::mat4(1.0f), tiltAngle, outward);
            glm::vec3 childDir = glm::normalize(glm::vec3(tiltRotation * glm::vec4(parentDir, 0.0f)));
            if(useLSystem) {
                // Add vertical randomness so sibling chains don't all rise similarly.
                float upJitter = (randBranch(i + 330) - 0.5f) * 0.42f;
                childDir = glm::normalize(childDir + glm::vec3(0.0f, upJitter, 0.0f));
            }
            
            float lengthVariation = useLSystem
                ? (0.78f + 0.50f * randBranch(i + 200))
                : (1.0f + (randBranch(i + 200) - 0.5f) * 0.3f);
            float radiusVariation = useLSystem
                ? (0.90f + 0.22f * randBranch(i + 260))
                : 1.0f;
            
            float attachT = 1.0f;
            if(useLSystem) {
                if(parent.depth == 0) {
                    // Keep first-order branches higher on the trunk so the tree
                    // establishes vertical growth before spreading outward.
                    float highBase = 0.90f + 0.08f * randBranch(i + 840);
                    float highJitter = (randBranch(i + 841) - 0.5f) * 0.04f;
                    attachT = glm::clamp(highBase + highJitter, 0.75f, 0.99f);
                } else {
                    float attachBase = 0.26f + 0.64f * randBranch(i + 840);
                    float attachJitter = (randBranch(i + 841) - 0.5f) * 0.22f;
                    attachT = glm::clamp(attachBase + attachJitter, 0.18f, 0.98f);
                }
            }

            glm::vec3 childStart = parent.endPos;
            if(useLSystem) {
                childStart = parent.startPos + parent.growthDirection * (parent.maxLength * attachT);
            }
            
            branches.push_back(Branch(
                childStart, 
                childDir, 
                childRadius * radiusVariation, 
                childDepth, 
                childLength * lengthVariation,
                parentIndex
            ));
            branches.back().parentAttachT = attachT;
            branches[parentIndex].childBranches.push_back((int)branches.size() - 1);
            createdChildren++;
        }
        
        if(!suppressBranchLogs) {
            std::cout << "[Plant] Branch " << parentIndex << " spawned " << createdChildren
                      << " children at depth " << childDepth << std::endl;
        }
    }
    
    void updateMesh() {
        plantMesh->clear();
        leafMesh->clear();
        activeLeafKeysThisFrame.clear();
        ++windEvaluationFrame;
        if(windEvaluationFrame == 0u) {
            windEvaluationFrame = 1u;
            windNodeStamp.clear();
        }
        
        for(int bi = 0; bi < (int)branches.size(); ++bi) {
            const Branch& branch = branches[bi];
            // Root base is capped; internal branch junctions use buried bases + tip cones.
            bool capBottom = (branch.parentIndex < 0);
            bool capTop = false;
            addCylinderForBranch(branch, bi, capBottom, capTop);
        }
        
        // Keep leaves visible while branches keep growing.
        // Store lightweight collision spheres to prevent leaf intersections.
        std::vector<glm::vec4> placedLeafSpheres;
        placedLeafSpheres.reserve(branches.size() * 8);

        // Emit leaves on terminal/deep branches first so tip branches do not starve
        // when the global leaf cap is reached.
        std::vector<int> leafOrder(branches.size());
        std::iota(leafOrder.begin(), leafOrder.end(), 0);
        std::stable_sort(leafOrder.begin(), leafOrder.end(), [&](int a, int b) {
            bool tipA = branches[a].childBranches.empty();
            bool tipB = branches[b].childBranches.empty();
            if(tipA != tipB) return tipA > tipB;
            if(branches[a].depth != branches[b].depth) return branches[a].depth > branches[b].depth;
            return a < b;
        });

        for(int bi : leafOrder) {
            const Branch& branch = branches[bi];
            bool emitLeaves = useLSystem ? (branch.activationStage >= 2) : (branch.depth >= 2);
            if(emitLeaves) {
                addLeavesForBranch(branch, bi, placedLeafSpheres);
            }
        }
        
        plantMesh->recomputePerVertexNormals();
        plantMesh->init();
        
        leafMesh->recomputePerVertexNormals();
        leafMesh->init();

        // Remove stale leaves that no longer exist in this frame.
        for(auto it = leafSmoothedPositions.begin(); it != leafSmoothedPositions.end();) {
            if(activeLeafKeysThisFrame.find(it->first) == activeLeafKeysThisFrame.end()) {
                spawnFallingLeaf(it->first, it->second);
                leafAttachmentState.erase(it->first);
                leafLastRenderedSize.erase(it->first);
                leafLastRenderedVariant.erase(it->first);
                leafLastRenderedColorVariation.erase(it->first);
                leafLastRenderedRight.erase(it->first);
                leafLastRenderedUp.erase(it->first);
                it = leafSmoothedPositions.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void addLeavesForBranch(const Branch& branch, int branchIdx, std::vector<glm::vec4>& placedLeafSpheres) {
        if(useLSystem && branch.activationStage > growthStage) return;
        if(branch.age < 0.0f) return;
        glm::vec3 branchStartPos = branch.startPos;
        glm::vec3 branchEndPos = branch.endPos;
        computeWindedBranchEndpoints(branch, branchIdx, branchStartPos, branchEndPos);
        float branchLen = glm::length(branchEndPos - branchStartPos);
        if(branchLen < 0.01f) return;
        
        glm::vec3 dir = glm::normalize(branchEndPos - branchStartPos);
        
        // FIX: use stable loop index instead of pointer arithmetic
        int branchId = branchIdx;
        int numLeaves = 3 + branch.depth + (branch.childBranches.empty() ? 2 : 0);
        int leafCountJitter = (int)(pseudoRandom(branchId, 913) * 3.0f) - 1; // -1,0,+1
        numLeaves = glm::max(3, numLeaves + leafCountJitter);
        
        // Leaf growth factor: starts small, grows to full size over ~2 seconds
        float growthFactor = glm::clamp(branch.age / 1.6f, 0.12f, 1.0f);
        if(growthFactor <= 0.001f) return;

        // Local frame around the branch axis (used for phyllotaxis offset)
        glm::vec3 up = glm::vec3(0, 1, 0);
        if(glm::abs(glm::dot(dir, up)) > 0.99f) {
            up = glm::vec3(1, 0, 0);
        }
        glm::vec3 right = glm::normalize(glm::cross(up, dir));
        glm::vec3 forward = glm::normalize(glm::cross(dir, right));

        // Phyllotaxis: constant golden-angle increment around branch axis.
        const float goldenAngle = glm::radians(137.50776405f);
        const float branchPhase = pseudoRandom(branchId, 900) * 2.0f * M_PI;

        auto resolveLeafOverlap = [&](glm::vec3 pos, const glm::vec3& radialHint, float size) {
            float radius = size * 0.45f;
            const float separationScale = 1.10f;
            // Soft push away from already placed leaves. Continuous adjustment avoids candidate-flip pumping.
            for(int it = 0; it < 3; ++it) {
                glm::vec3 push(0.0f);
                int overlaps = 0;
                for(const glm::vec4& otherLeaf : placedLeafSpheres) {
                    glm::vec3 d = pos - glm::vec3(otherLeaf);
                    float dist = glm::length(d);
                    float minDist = (radius + otherLeaf.w) * separationScale;
                    if(dist < minDist) {
                        glm::vec3 n;
                        if(dist > 1e-5f) {
                            n = d / dist;
                        } else {
                            n = radialHint;
                        }
                        push += n * (minDist - dist);
                        overlaps++;
                    }
                }
                if(overlaps == 0) break;
                pos += 0.7f * (push / (float)overlaps);
            }

            // Keep leaves outside branch core so they do not sink into the branch.
            float axial = glm::dot(pos - branchStartPos, dir);
            axial = glm::clamp(axial, 0.0f, branchLen);
            glm::vec3 axisPoint = branchStartPos + dir * axial;
            glm::vec3 radial = pos - axisPoint;
            float radialLen = glm::length(radial);
            float minRad = branch.radius * 0.65f + radius * 0.25f;
            if(radialLen < minRad) {
                glm::vec3 n = (radialLen > 1e-5f) ? (radial / radialLen) : radialHint;
                pos = axisPoint + n * minRad;
            }
            return pos;
        };

        auto canEmitLeaf = [&](unsigned long long leafKey, bool forceKeep = false) {
            if(forceKeep) return true;
            if((int)placedLeafSpheres.size() < maxLeaves) return true;
            if(leafSmoothedPositions.find(leafKey) != leafSmoothedPositions.end()) return true;
            if(!leafCapReachedLogged) {
                std::cout << "[Plant] Leaf cap reached (" << maxLeaves
                          << "). Keeping existing leaves stable; skipping new leaves." << std::endl;
                leafCapReachedLogged = true;
            }
            return false;
        };

        int emittedLeaves = 0;
        auto placeLeaf = [&](unsigned long long leafKey,
                             const glm::vec3& targetPos,
                             float size,
                             float rotAngle,
                             float tiltAngle,
                             int leafVariant,
                             float leafColorVariation) {
            glm::vec3 stableTarget = targetPos;
            // During seasonal density transitions, keep surviving leaves spatially stable
            // to avoid canopy pumping from repeated overlap re-solves.
            bool preserveMatureLeafLayout =
                (seasonalStability > 0.01f || seasonalLeafDensity < 0.999f) && branch.age > 1.6f;
            if(preserveMatureLeafLayout) {
                auto it = leafSmoothedPositions.find(leafKey);
                if(it != leafSmoothedPositions.end()) {
                    stableTarget = it->second;
                }
            }
            glm::vec3 smoothedPos = smoothLeafPosition(leafKey, stableTarget);
            // Branch endpoints are already wind-deformed; avoid extra translational wind here
            // to prevent double movement. Keep only local flutter via angle modulation below.
            glm::vec3 visualPos = smoothedPos;
            float windedRotAngle = rotAngle;
            float windedTiltAngle = tiltAngle;
            if(windBlend > 1e-4f) {
                float leafNoise = seasonalLeafNoise(leafKey, randomSeed + 12891u);
                float leafHeightNorm = glm::clamp((smoothedPos.y + 1.0f) / 3.2f, 0.0f, 1.0f);
                float leafHeightGain = std::pow(glm::smoothstep(0.30f, 1.0f, leafHeightNorm), 1.25f);
                float leafFlutterScale = glm::mix(0.58f, 1.22f, leafHeightGain);
                float flutterA = std::sin(windTime * (2.2f + 1.6f * leafNoise) + leafNoise * 6.28318f);
                float flutterB = std::sin(windTime * (3.6f + 1.1f * leafNoise) + leafNoise * 3.1f);
                windedRotAngle += windBlend * leafFlutterScale * (0.18f + 0.12f * leafNoise) * flutterA;
                windedTiltAngle += windBlend * leafFlutterScale * 0.13f * flutterB;
            }
            glm::vec3 leafRight(1.0f, 0.0f, 0.0f), leafUp(0.0f, 1.0f, 0.0f);
            computeLeafBasis(dir, windedRotAngle, windedTiltAngle, leafRight, leafUp);
            addLeafQuad(visualPos, dir, size, windedRotAngle, windedTiltAngle, leafVariant, leafColorVariation);
            leafLastRenderedSize[leafKey] = size;
            leafLastRenderedVariant[leafKey] = leafVariant;
            leafLastRenderedColorVariation[leafKey] = leafColorVariation;
            leafLastRenderedRight[leafKey] = leafRight;
            leafLastRenderedUp[leafKey] = leafUp;
            // Collision is solved in target-space to keep layout stable; smoothing only affects rendering.
            placedLeafSpheres.push_back(glm::vec4(targetPos, size * 0.45f));
            emittedLeaves++;
        };

        auto attachmentForSeason = [&](unsigned long long leafKey, bool forceKeep = false) {
            return seasonalLeafAttachment(leafKey, forceKeep);
        };

        int mandatoryBodyLeaves = 0;
        if(seasonalLeafDensity > 0.70f) mandatoryBodyLeaves = branch.childBranches.empty() ? 4 : 2;
        else if(seasonalLeafDensity > 0.35f) mandatoryBodyLeaves = branch.childBranches.empty() ? 2 : 1;

        for(int i = 0; i < numLeaves; ++i) {
            unsigned long long leafKey = makeLeafKey(branchId, i);
            bool forceKeep = (i < mandatoryBodyLeaves);
            float targetAttachment = attachmentForSeason(leafKey, forceKeep);
            float attachment = smoothLeafAttachment(leafKey, targetAttachment);
            if(attachment <= minRenderedAttachment) continue;
            if(!canEmitLeaf(leafKey, forceKeep)) continue;
            float s = (i + 0.5f) / (float)numLeaves;
            float tJitter = (pseudoRandom(branchId, i + 700) - 0.5f) * 0.06f;
            float t = glm::clamp(0.2f + 0.72f * s + tJitter, 0.15f, 0.95f);
            
            float depthScale = glm::max(0.45f, 1.0f - 0.07f * branch.depth);
            float leafSize = 0.08f * depthScale;
            leafSize *= (0.7f + 0.6f * pseudoRandom(branchId, i + 50));
            leafSize *= growthFactor;
            leafSize *= seasonalLeafSizeScale;
            float oldness = leafOldness(leafKey);
            // Earlier-emitted leaves tend to be slightly larger during healthy canopy states.
            leafSize *= (0.92f + 0.14f * oldness);
            float growthAttachScale = allowSeasonLeafDrop
                ? (0.86f + 0.14f * glm::smoothstep(0.0f, 1.0f, attachment))
                : glm::mix(0.02f, 1.0f, attachment);
            leafSize *= growthAttachScale;
            
            if(leafSize < 0.0025f) continue;  // Skip tiny leaves
            
            float angleJitter = (pseudoRandom(branchId, i + 980) - 0.5f) * 0.36f;
            float rotAngle = branchPhase + i * goldenAngle + angleJitter;
            float tiltAngle = 0.3f + pseudoRandom(branchId, i + 150) * 1.0f;
            int variantCount = glm::max(1, leafVariantCount);
            int leafVariant = 0;
            if(!leafVariantUVRects.empty() && (int)leafVariantUVRects.size() == 4) {
                float vr = pseudoRandom(branchId, i + 175);
                // Prefer single-leaf variants; keep the bottom cluster variant rarer.
                leafVariant = (vr < 0.32f) ? 0 : (vr < 0.64f) ? 1 : (vr < 0.92f) ? 2 : 3;
            } else {
                leafVariant = (int)(pseudoRandom(branchId, i + 175) * (float)variantCount) % variantCount;
            }
            float leafColorVariation = seasonalLeafNoise(leafKey, randomSeed + 6311u);
            
            glm::vec3 radialDir = glm::normalize(
                right * std::cos(rotAngle) + forward * std::sin(rotAngle)
            );
            float radialOffset = branch.radius * (0.65f + 0.20f * pseudoRandom(branchId, i + 520))
                               + leafSize * 0.14f;
            glm::vec3 leafPos = branchStartPos + dir * (branchLen * t) + radialDir * radialOffset;
            glm::vec3 resolvedPos = resolveLeafOverlap(leafPos, radialDir, leafSize);
            placeLeaf(leafKey, resolvedPos, leafSize, rotAngle, tiltAngle, leafVariant, leafColorVariation);
        }
        
        // Cluster at the tip
        int mandatoryTipLeaves = 0;
        if(seasonalLeafDensity > 0.70f) mandatoryTipLeaves = branch.childBranches.empty() ? 3 : 1;
        else if(seasonalLeafDensity > 0.35f) mandatoryTipLeaves = branch.childBranches.empty() ? 1 : 0;
        int tipClusterCount = 4 + (int)(pseudoRandom(branchId, 914) * 3.0f); // 4..6
        for(int i = 0; i < tipClusterCount; ++i) {
            unsigned long long leafKey = makeLeafKey(branchId, 1000 + i);
            bool forceKeep = (i < mandatoryTipLeaves);
            float targetAttachment = attachmentForSeason(leafKey, forceKeep);
            float attachment = smoothLeafAttachment(leafKey, targetAttachment);
            if(attachment <= minRenderedAttachment) continue;
            if(!canEmitLeaf(leafKey, forceKeep)) continue;
            int spiralIdx = numLeaves + i;
            float leafSize = 0.07f * (0.8f + 0.4f * pseudoRandom(branchId, i + 200));
            leafSize *= growthFactor;
            leafSize *= seasonalLeafSizeScale;
            float oldness = leafOldness(leafKey);
            leafSize *= (0.92f + 0.14f * oldness);
            float growthAttachScale = allowSeasonLeafDrop
                ? (0.86f + 0.14f * glm::smoothstep(0.0f, 1.0f, attachment))
                : glm::mix(0.02f, 1.0f, attachment);
            leafSize *= growthAttachScale;
            if(leafSize < 0.0025f) continue;
            float tipJitter = (pseudoRandom(branchId, i + 990) - 0.5f) * 0.42f;
            float rotAngle = branchPhase + spiralIdx * goldenAngle + tipJitter;
            float tiltAngle = 0.5f + pseudoRandom(branchId, i + 300) * 0.8f;
            int variantCount = glm::max(1, leafVariantCount);
            int leafVariant = 0;
            if(!leafVariantUVRects.empty() && (int)leafVariantUVRects.size() == 4) {
                float vr = pseudoRandom(branchId, i + 325);
                leafVariant = (vr < 0.32f) ? 0 : (vr < 0.64f) ? 1 : (vr < 0.92f) ? 2 : 3;
            } else {
                leafVariant = (int)(pseudoRandom(branchId, i + 325) * (float)variantCount) % variantCount;
            }
            float leafColorVariation = seasonalLeafNoise(leafKey, randomSeed + 6311u);
            
            glm::vec3 radialDir = glm::normalize(
                right * std::cos(rotAngle) + forward * std::sin(rotAngle)
            );
            float radialOffset = branch.radius * (0.70f + 0.20f * pseudoRandom(branchId, i + 620))
                               + leafSize * 0.16f;
            float backOffset = 0.03f * branchLen * i;
            glm::vec3 leafPos = branchEndPos - dir * backOffset + radialDir * radialOffset;
            glm::vec3 resolvedPos = resolveLeafOverlap(leafPos, radialDir, leafSize);
            placeLeaf(leafKey, resolvedPos, leafSize, rotAngle, tiltAngle, leafVariant, leafColorVariation);
        }

        // Hard fallback: ensure no branch is left completely leafless.
        if(emittedLeaves == 0 && seasonalLeafDensity > 0.32f) {
            unsigned long long leafKey = makeLeafKey(branchId, 60000);
            float leafSize = glm::max(0.028f, 0.055f * growthFactor);
            leafSize *= seasonalLeafSizeScale;
            float fallbackScale = glm::smoothstep(0.32f, 0.56f, seasonalLeafDensity);
            leafSize *= glm::mix(0.14f, 1.0f, fallbackScale);
            float rotAngle = branchPhase;
            float tiltAngle = 0.7f;
            int leafVariant = glm::max(0, leafVariantCount / 2);
            float leafColorVariation = seasonalLeafNoise(leafKey, randomSeed + 6311u);
            glm::vec3 radialDir = right;
            float radialOffset = branch.radius * 0.75f + leafSize * 0.20f;
            glm::vec3 leafPos = branchEndPos + radialDir * radialOffset;
            glm::vec3 resolvedPos = resolveLeafOverlap(leafPos, radialDir, leafSize);
            placeLeaf(leafKey, resolvedPos, leafSize, rotAngle, tiltAngle, leafVariant, leafColorVariation);
        }
    }
    
    void computeLeafBasis(const glm::vec3& branchDir,
                          float rotAngle,
                          float tiltAngle,
                          glm::vec3& leafRight,
                          glm::vec3& leafUp) const {
        glm::vec3 up = glm::vec3(0, 1, 0);
        if(glm::abs(glm::dot(branchDir, up)) > 0.99f) {
            up = glm::vec3(1, 0, 0);
        }
        glm::vec3 right = glm::normalize(glm::cross(up, branchDir));
        glm::vec3 forward = glm::normalize(glm::cross(branchDir, right));
        
        glm::mat4 rot = glm::rotate(glm::mat4(1.0f), rotAngle, branchDir);
        right = glm::vec3(rot * glm::vec4(right, 0.0f));
        forward = glm::vec3(rot * glm::vec4(forward, 0.0f));
        
        glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), tiltAngle, forward);
        glm::vec3 leafNormal = glm::vec3(tilt * glm::vec4(branchDir, 0.0f));
        leafRight = glm::normalize(glm::cross(leafNormal, forward));
        leafUp = glm::normalize(glm::cross(leafRight, leafNormal));
    }

    void addLeafQuad(const glm::vec3& pos, const glm::vec3& branchDir, 
                     float size,
                     float rotAngle,
                     float tiltAngle,
                     int leafVariant,
                     float leafColorVariation) {
        glm::vec3 leafRight(1.0f, 0.0f, 0.0f), leafUp(0.0f, 1.0f, 0.0f);
        computeLeafBasis(branchDir, rotAngle, tiltAngle, leafRight, leafUp);

        appendLeafQuadToMesh(leafMesh, pos, leafRight, leafUp, size, leafVariant, leafColorVariation);
    }
    
    void addCylinderForBranch(const Branch& branch, int branchIdx, bool capBottom, bool capTop) {
        if(useLSystem && branch.activationStage > growthStage) return;
        if(branch.age < 0.0f) return;
        float restLength = glm::length(branch.endPos - branch.startPos);
        if(restLength < 1e-5f) return;

        auto& positions = plantMesh->vertexPositions();
        auto& triangles = plantMesh->triangleIndices();
        auto& texCoords = plantMesh->vertexTexCoords();
        
        unsigned int baseIndex = positions.size();

        glm::vec3 startPos = branch.startPos;
        glm::vec3 endPos = branch.endPos;
        computeWindedBranchEndpoints(branch, branchIdx, startPos, endPos);
        glm::vec3 seg = endPos - startPos;
        float length = glm::length(seg);
        if(length < 1e-6f) return;
        glm::vec3 direction = seg / length;
        
        float segmentTaper = useLSystem ? 0.86f : 0.65f;
        float bottomRadius = branch.radius;
        float topRadius = branch.radius * segmentTaper;

        // Keep same-depth chains smooth to avoid the "stacked bucket" look.
        if(branch.parentIndex >= 0 && branch.parentIndex < (int)branches.size()) {
            const Branch& parent = branches[branch.parentIndex];
            if(parent.depth == branch.depth) {
                bottomRadius = glm::mix(bottomRadius, parent.radius * segmentTaper, 0.70f);
            } else {
                bottomRadius = glm::mix(bottomRadius, parent.radius * 0.78f, 0.20f);
            }
            // Slightly narrow attached branch bases to keep cap discs tucked into the parent surface.
            bottomRadius *= 0.92f;
        }

        if(!branch.childBranches.empty()) {
            const Branch& child0 = branches[branch.childBranches[0]];
            topRadius = glm::mix(topRadius, child0.radius, 0.30f);
        }

        bottomRadius = glm::max(bottomRadius, 0.0018f);
        topRadius = glm::max(topRadius, 0.0015f);

        // Sink child branch bases slightly into parent so open bases are hidden at junctions.
        if(branch.parentIndex >= 0) {
            float bury = glm::min(length * 0.22f, bottomRadius * 1.30f);
            startPos -= direction * bury;
            seg = endPos - startPos;
            length = glm::length(seg);
            if(length < 1e-6f) return;
            direction = seg / length;
        }
        
        glm::vec3 right, forward;
        computeStableBranchFrame(branchIdx, direction, right, forward);
        
        float vTile = length / (bottomRadius * 2.0f * M_PI);
        
        for(int i = 0; i < cylinderSegments; ++i) {
            float angle = (float)i / cylinderSegments * 2.0f * M_PI;
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);
            
            glm::vec3 offsetDir = right * cosA + forward * sinA;
            float u = (float)i / cylinderSegments;
            
            positions.push_back(startPos + offsetDir * bottomRadius);
            texCoords.push_back(glm::vec2(u, 0.0f));
            
            positions.push_back(endPos + offsetDir * topRadius);
            texCoords.push_back(glm::vec2(u, vTile));
        }
        
        for(int i = 0; i < cylinderSegments; ++i) {
            int next = (i + 1) % cylinderSegments;
            
            unsigned int bottom1 = baseIndex + i * 2;
            unsigned int top1 = baseIndex + i * 2 + 1;
            unsigned int bottom2 = baseIndex + next * 2;
            unsigned int top2 = baseIndex + next * 2 + 1;
            
            triangles.push_back(glm::uvec3(bottom1, bottom2, top1));
            triangles.push_back(glm::uvec3(bottom2, top2, top1));
        }

        // Smooth closure: use a short tip cone instead of a flat top disc.
        if(!capTop) {
            unsigned int tipIndex = positions.size();
            float tipExtend = glm::max(0.0025f, topRadius * 0.70f);
            positions.push_back(endPos + direction * tipExtend);
            texCoords.push_back(glm::vec2(0.5f, vTile + tipExtend / (bottomRadius * 2.0f * M_PI)));
            for(int i = 0; i < cylinderSegments; ++i) {
                int next = (i + 1) % cylinderSegments;
                unsigned int top1 = baseIndex + i * 2 + 1;
                unsigned int top2 = baseIndex + next * 2 + 1;
                triangles.push_back(glm::uvec3(top1, top2, tipIndex));
            }
        }
        
        if(capBottom) {
            // Bottom cap only for root/trunk base.
            for(int i = 1; i < cylinderSegments - 1; ++i) {
                triangles.push_back(glm::uvec3(
                    baseIndex,
                    baseIndex + (i + 1) * 2,
                    baseIndex + i * 2
                ));
            }
        }
        
        if(capTop) {
            // Optional flat top cap (disabled for junction-safe rendering).
            for(int i = 1; i < cylinderSegments - 1; ++i) {
                triangles.push_back(glm::uvec3(
                    baseIndex + 1,
                    baseIndex + i * 2 + 1,
                    baseIndex + (i + 1) * 2 + 1
                ));
            }
        }
    }
};


enum class Season {
  Spring = 0,
  Summer = 1,
  Fall = 2,
  Winter = 3
};

struct Scene {
  std::vector<Light> lights;

  std::shared_ptr<Mesh> rhino = nullptr;
  std::shared_ptr<Mesh> plane = nullptr;
  std::shared_ptr<Plant> plant = nullptr;

  GLuint normalMapTexture = 0;
  GLuint colorMapTexture = 0;
  unsigned int normalMapTextureSlot = 0;
  unsigned int colorMapTextureSlot = 0;

  GLuint barkTexture = 0;
  GLuint leafColorTexture = 0;
  GLuint leafOpacityTexture = 0;
  GLuint leafColorTextureFall = 0;
  GLuint leafOpacityTextureFall = 0;
  unsigned int barkTextureSlot = 0;
  unsigned int leafColorTextureSlot = 0;
  unsigned int leafOpacityTextureSlot = 0;
  unsigned int leafColorTextureFallSlot = 0;
  unsigned int leafOpacityTextureFallSlot = 0;

  glm::mat4 rhinoMat = glm::mat4(1.0);
  glm::mat4 planeMat = glm::mat4(1.0);
  glm::mat4 floorMat = glm::mat4(1.0);

  glm::vec3 scene_center = glm::vec3(0);
  float scene_radius = 1.f;

  std::shared_ptr<ShaderProgram> mainShader, shadomMapShader;

  bool saveShadowMapsPpm = false;
  bool saveViewportScreenshotPpm = false;
  unsigned int screenshotCounter = 0;

  struct SeasonParams {
    float leafDensity = 1.0f;
    float leafSizeScale = 1.0f;
    float leafAlphaScale = 1.0f;
    glm::vec3 leafTint = glm::vec3(1.0f);
    float leafTextureBlend = 0.0f; // 0 = spring/summer atlas, 1 = fall atlas
  };

  Season currentSeason = Season::Spring;
  Season targetSeason = Season::Spring;
  float seasonTransitionTime = 0.0f;
  float seasonTransitionDuration = 18.0f; // seconds for the current transition
  float seasonTransitionDurationDefault = 18.0f;
  float seasonTransitionDurationSummer = 9.0f; // shorter transitions involving summer (except Summer->Fall)
  float seasonTransitionDurationSummerToFall = 18.0f;
  SeasonParams seasonParams;
  float lastAppliedLeafDensity = -1.0f;
  float lastAppliedLeafSizeScale = -1.0f;
  int lastAppliedLeafAtlasCols = -1;
  int lastAppliedLeafAtlasRows = -1;
  int lastAppliedLeafVariantCount = -1;
  float seasonRebuildCooldown = 0.0f;
  float minSeasonRebuildInterval = 0.12f;
  float minSeasonRebuildIntervalActive = 0.02f; // ~50 Hz while transitioning seasons
  bool springLeafFlushDone = true;

  static const char* seasonName(Season s) {
    switch(s) {
      case Season::Spring: return "Spring";
      case Season::Summer: return "Summer";
      case Season::Fall: return "Fall";
      case Season::Winter: return "Winter";
      default: return "Unknown";
    }
  }

  static Season nextSeason(Season s) {
    switch(s) {
      case Season::Spring: return Season::Summer;
      case Season::Summer: return Season::Fall;
      case Season::Fall: return Season::Winter;
      case Season::Winter: return Season::Spring;
      default: return Season::Spring;
    }
  }

  float transitionDurationFor(Season from, Season to) const {
    if(from == Season::Summer && to == Season::Fall) {
      return seasonTransitionDurationSummerToFall;
    }
    if(from == Season::Summer || to == Season::Summer) {
      return seasonTransitionDurationSummer;
    }
    return seasonTransitionDurationDefault;
  }

  static SeasonParams paramsForSeason(Season s) {
    SeasonParams p;
    switch(s) {
      case Season::Spring:
        // Spring keeps a full bud set; summer mostly matures size and fullness.
        p.leafDensity = 1.00f;
        p.leafSizeScale = 0.85f;
        p.leafAlphaScale = 0.96f;
        p.leafTint = glm::vec3(0.88f, 1.03f, 0.82f);
        p.leafTextureBlend = 0.0f;
        break;
      case Season::Summer:
        p.leafDensity = 1.00f;
        p.leafSizeScale = 1.00f;
        p.leafAlphaScale = 1.00f;
        p.leafTint = glm::vec3(1.00f, 1.00f, 1.00f);
        p.leafTextureBlend = 0.0f;
        break;
      case Season::Fall:
        p.leafDensity = 0.56f;
        p.leafSizeScale = 0.74f;
        p.leafAlphaScale = 0.90f;
        // Keep fall tint close to neutral so per-leaf fall variation drives the look.
        p.leafTint = glm::vec3(1.00f, 0.97f, 0.90f);
        p.leafTextureBlend = 1.0f;
        break;
      case Season::Winter:
        p.leafDensity = 0.04f;
        p.leafSizeScale = 0.78f;
        p.leafAlphaScale = 0.62f;
        p.leafTint = glm::vec3(0.78f, 0.72f, 0.67f);
        p.leafTextureBlend = 1.0f;
        break;
      default:
        break;
    }
    return p;
  }

  void applySeasonToPlant(bool forceRebuild = false) {
    if(!plant) return;

    float previousDensity = plant->seasonalLeafDensity;
    plant->seasonalLeafDensity = glm::clamp(seasonParams.leafDensity, 0.0f, 1.0f);
    plant->seasonalLeafSizeScale = glm::max(0.05f, seasonParams.leafSizeScale);
    plant->seasonalStability = (currentSeason != targetSeason) ? 1.0f : 0.0f;
    bool dropSeason =
      currentSeason == Season::Fall || currentSeason == Season::Winter ||
      targetSeason == Season::Fall || targetSeason == Season::Winter;
    float densityDrop = glm::max(0.0f, previousDensity - plant->seasonalLeafDensity);
    bool isRegrowing = plant->seasonalLeafDensity > previousDensity + 1e-4f;
    bool activeLeafShedding = dropSeason && !isRegrowing;
    plant->allowSeasonLeafDrop = activeLeafShedding;
    plant->seasonLeafDropStrength = activeLeafShedding
      ? glm::clamp(densityDrop * 8.0f + (currentSeason != targetSeason ? 0.05f : 0.0f), 0.0f, 1.0f)
      : 0.0f;
    if(activeLeafShedding) {
      // During Fall/Winter shedding, keep canopy alpha high enough so leaves
      // detach and fall instead of disappearing from alpha discard.
      seasonParams.leafAlphaScale = glm::max(seasonParams.leafAlphaScale, 0.86f);
    }
    float transitionT = (currentSeason != targetSeason)
      ? glm::clamp(seasonTransitionTime / glm::max(1e-4f, seasonTransitionDuration), 0.0f, 1.0f)
      : 0.0f;
    float groundAgingRate = 1.0f;
    bool inFallWindow = (currentSeason == Season::Fall || targetSeason == Season::Fall);
    if(inFallWindow) {
      // Fall litter should stay visible longer.
      groundAgingRate = 0.32f;
    }
    bool toSpringCleanup = (targetSeason == Season::Spring && currentSeason != Season::Spring);
    if(toSpringCleanup) {
      // Clear winter litter before spring buds emerge.
      float clearT = glm::smoothstep(0.0f, 0.78f, transitionT);
      groundAgingRate = glm::mix(4.0f, 16.0f, clearT);
    }
    plant->fallenLeafGroundAgingRate = groundAgingRate;

    // Keep a stable 3x3 UV layout for all seasons.
    // Fall atlas remapping is handled in shaders, avoiding abrupt UV layout jumps.
    plant->leafAtlasCols = 3;
    plant->leafAtlasRows = 3;
    plant->leafVariantCount = 9;
    plant->leafVariantUVRects.clear();

    bool seasonTransitionActive = (currentSeason != targetSeason) || activeLeafShedding;
    bool seasonParamsChanged =
      seasonTransitionActive
      || std::abs(plant->seasonalLeafDensity - lastAppliedLeafDensity) > 0.012f
      || std::abs(plant->seasonalLeafSizeScale - lastAppliedLeafSizeScale) > 0.016f
      || plant->leafAtlasCols != lastAppliedLeafAtlasCols
      || plant->leafAtlasRows != lastAppliedLeafAtlasRows
      || plant->leafVariantCount != lastAppliedLeafVariantCount;

    bool needsRebuild = forceRebuild || (seasonParamsChanged && seasonRebuildCooldown <= 0.0f);

    if(needsRebuild) {
      plant->updateMesh();
      lastAppliedLeafDensity = plant->seasonalLeafDensity;
      lastAppliedLeafSizeScale = plant->seasonalLeafSizeScale;
      lastAppliedLeafAtlasCols = plant->leafAtlasCols;
      lastAppliedLeafAtlasRows = plant->leafAtlasRows;
      lastAppliedLeafVariantCount = plant->leafVariantCount;
      float interval = seasonTransitionActive ? minSeasonRebuildIntervalActive : minSeasonRebuildInterval;
      seasonRebuildCooldown = forceRebuild ? 0.0f : interval;
    }
  }

  void updateSeason(float dt) {
    seasonRebuildCooldown = glm::max(0.0f, seasonRebuildCooldown - glm::max(0.0f, dt));

    SeasonParams a = paramsForSeason(currentSeason);
    SeasonParams b = paramsForSeason(targetSeason);

    if(currentSeason != targetSeason) {
      seasonTransitionTime += glm::max(0.0f, dt);
      float t = glm::clamp(seasonTransitionTime / glm::max(1e-4f, seasonTransitionDuration), 0.0f, 1.0f);
      // Quintic smootherstep further softens starts/stops to avoid visible stepping.
      float s = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
      bool toSpringReset = (targetSeason == Season::Spring && currentSeason != Season::Spring);
      if(toSpringReset) {
        // Hard lifecycle split:
        // 1) keep old leaves as fall/winter leaves and detach them,
        // 2) once flushed, regrow only fresh spring leaves from tiny buds.
        float shedT = glm::smoothstep(0.0f, 0.60f, t);
        if(!springLeafFlushDone && t >= 0.58f && plant) {
          plant->detachAllAttachedLeavesToFalling();
          springLeafFlushDone = true;
          seasonRebuildCooldown = 0.0f;
          lastAppliedLeafDensity = -1.0f;
          lastAppliedLeafSizeScale = -1.0f;
        }

        bool groundClearedForSpring = true;
        if(plant) {
          // Hard guarantee: clear old fallen leaves before new spring buds regrow.
          if(t >= 0.80f && plant->hasFallenLeaves()) {
            plant->clearFallenLeaves();
          }
          groundClearedForSpring = !plant->hasFallenLeaves();
        }
        float regrowT = (springLeafFlushDone && groundClearedForSpring)
          ? glm::smoothstep(0.84f, 1.0f, t)
          : 0.0f;
        float regrowSlow = regrowT * regrowT;

        float shedDensity = glm::mix(a.leafDensity, 0.0f, shedT);
        seasonParams.leafDensity = glm::mix(shedDensity, b.leafDensity, regrowSlow);

        if(!springLeafFlushDone) {
          seasonParams.leafSizeScale = a.leafSizeScale;
          seasonParams.leafAlphaScale = glm::mix(a.leafAlphaScale, 0.88f, shedT);
          seasonParams.leafTint = a.leafTint;
          seasonParams.leafTextureBlend = 1.0f;
        } else {
          const float springBudStartSize = 0.06f;
          seasonParams.leafSizeScale = glm::mix(springBudStartSize, b.leafSizeScale, regrowSlow);
          seasonParams.leafAlphaScale = glm::mix(0.68f, b.leafAlphaScale, regrowSlow);
          seasonParams.leafTint = b.leafTint;
          seasonParams.leafTextureBlend = 0.0f;
        }
      } else {
        seasonParams.leafDensity = glm::mix(a.leafDensity, b.leafDensity, s);
        seasonParams.leafSizeScale = glm::mix(a.leafSizeScale, b.leafSizeScale, s);
        seasonParams.leafAlphaScale = glm::mix(a.leafAlphaScale, b.leafAlphaScale, s);
        seasonParams.leafTint = glm::mix(a.leafTint, b.leafTint, s);
        // Smoothly blend leaf textures over the whole transition.
        seasonParams.leafTextureBlend = glm::mix(a.leafTextureBlend, b.leafTextureBlend, s);
      }

      if(t >= 1.0f) {
        currentSeason = targetSeason;
        seasonTransitionTime = 0.0f;
        springLeafFlushDone = true;
      }
    } else {
      seasonParams = a;
    }

    applySeasonToPlant(false);
  }

  void advanceToNextSeason() {
    if(currentSeason != targetSeason) {
      std::cout << "[Season] Transition already running: "
                << seasonName(currentSeason) << " -> " << seasonName(targetSeason) << std::endl;
      return;
    }

    targetSeason = nextSeason(currentSeason);
    seasonTransitionDuration = transitionDurationFor(currentSeason, targetSeason);
    springLeafFlushDone = (targetSeason != Season::Spring);
    seasonTransitionTime = 0.0f;
    std::cout << "[Season] Transition: " << seasonName(currentSeason)
              << " -> " << seasonName(targetSeason)
              << " (duration: " << seasonTransitionDuration << "s)" << std::endl;
  }

  void saveViewportPpmFile(const std::string& filename) const {
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if(width <= 0 || height <= 0) {
      std::cerr << "[Screenshot] Invalid viewport size (" << width << "x" << height << ")" << std::endl;
      return;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, 0u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream output(filename, std::ios::binary);
    if(!output.is_open()) {
      std::cerr << "[Screenshot] Failed to open file for writing: " << filename << std::endl;
      return;
    }

    // PPM (P6) expects top-to-bottom rows while OpenGL returns bottom-to-top.
    output << "P6\n" << width << " " << height << "\n255\n";
    for(int y = height - 1; y >= 0; --y) {
      const char* row = reinterpret_cast<const char*>(
        pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 3u);
      output.write(row, static_cast<std::streamsize>(static_cast<size_t>(width) * 3u));
    }
    output.close();

    std::cout << "[Screenshot] Saved viewport to " << filename << std::endl;
  }

  void render()
  {
    // Shadow map pass
    glEnable(GL_CULL_FACE);

    shadomMapShader->use();
    int windEnabled = 0;
    float windTimeNow = 0.0f;
    float windStrengthNow = 0.0f;
    glm::vec2 windDirNow = glm::normalize(glm::vec2(0.90f, 0.44f));
    float windGustNow = 0.55f;
    if(plant) {
      windTimeNow = plant->windTime;
      windStrengthNow = plant->windBlend;
      windDirNow = plant->windDirection;
      windGustNow = plant->windGust;
      windEnabled = (windStrengthNow > 1e-4f) ? 1 : 0;
    }
    shadomMapShader->set("windEnabled", windEnabled);
    shadomMapShader->set("windTime", windTimeNow);
    shadomMapShader->set("windStrength", windStrengthNow);
    shadomMapShader->set("windDir", windDirNow);
    shadomMapShader->set("windGust", windGustNow);
    shadomMapShader->set("windAffectsObject", 0);
    for(int i=0; i<(int)lights.size(); ++i) {
      Light &light = lights[i];
      light.setupCameraForShadowMapping(shadomMapShader, scene_center, scene_radius*1.5f);
      light.bindShadowMap();
      shadomMapShader->set("leafSeasonBlend", seasonParams.leafTextureBlend);
      shadomMapShader->set("leafAlphaScale", seasonParams.leafAlphaScale);

      shadomMapShader->set("useLeafAlpha", 0);
      shadomMapShader->set("windAffectsObject", 0);
      shadomMapShader->set("depthMVP", light.depthMVP * planeMat);
      plane->render();
      
      shadomMapShader->set("depthMVP", light.depthMVP * floorMat);
      plane->render();

      if(plant && plant->plantMesh) {
        shadomMapShader->set("useLeafAlpha", 0);
        shadomMapShader->set("windAffectsObject", 0);
        shadomMapShader->set("depthMVP", light.depthMVP * glm::mat4(1.0));
        plant->plantMesh->render();
      }
      if(plant && plant->leafMesh) {
        glDisable(GL_CULL_FACE);
        shadomMapShader->set("useLeafAlpha", 1);
        shadomMapShader->set("windAffectsObject", 0);
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTexture);
        shadomMapShader->set("leafOpacityShadow", static_cast<int>(leafOpacityTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTextureFall);
        shadomMapShader->set("leafOpacityShadowFall", static_cast<int>(leafOpacityTextureFallSlot));
        shadomMapShader->set("depthMVP", light.depthMVP * glm::mat4(1.0));
        plant->leafMesh->render();
        glEnable(GL_CULL_FACE);
      }
      if(plant && plant->fallenLeafMesh && !plant->fallenLeafMesh->triangleIndices().empty()) {
        glDisable(GL_CULL_FACE);
        shadomMapShader->set("useLeafAlpha", 1);
        shadomMapShader->set("windAffectsObject", 0);
        bool keepOldFallenLook = (currentSeason == Season::Spring || targetSeason == Season::Spring);
        float fallenBlend = keepOldFallenLook ? 1.0f : seasonParams.leafTextureBlend;
        float fallenAlpha = keepOldFallenLook ? 1.0f : seasonParams.leafAlphaScale;
        shadomMapShader->set("leafSeasonBlend", fallenBlend);
        shadomMapShader->set("leafAlphaScale", fallenAlpha);
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTexture);
        shadomMapShader->set("leafOpacityShadow", static_cast<int>(leafOpacityTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTextureFall);
        shadomMapShader->set("leafOpacityShadowFall", static_cast<int>(leafOpacityTextureFallSlot));
        shadomMapShader->set("depthMVP", light.depthMVP * glm::mat4(1.0));
        plant->fallenLeafMesh->render();
        shadomMapShader->set("leafSeasonBlend", seasonParams.leafTextureBlend);
        shadomMapShader->set("leafAlphaScale", seasonParams.leafAlphaScale);
        glEnable(GL_CULL_FACE);
      }

      if(saveShadowMapsPpm) {
        std::cout << "Saving shadow_map_" << i << ".ppm..." << std::endl;
        light.shadowMap.savePpmFile(std::string("shadow_map_")+std::to_string(i)+std::string(".ppm"));
        std::cout << "Shadow map " << i << " saved successfully!" << std::endl;
      }
    }
    shadomMapShader->stop();
    saveShadowMapsPpm = false;

    // Main render pass
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_windowWidth, g_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_BACK);

    mainShader->use();

    mainShader->set("camPos", g_cam->getPosition());
    mainShader->set("viewMat", g_cam->computeViewMatrix());
    mainShader->set("projMat", g_cam->computeProjectionMatrix());
    mainShader->set("leafSeasonBlend", seasonParams.leafTextureBlend);
    mainShader->set("leafSeasonTint", seasonParams.leafTint);
    mainShader->set("leafAlphaScale", seasonParams.leafAlphaScale);
    mainShader->set("windEnabled", windEnabled);
    mainShader->set("windTime", windTimeNow);
    mainShader->set("windStrength", windStrengthNow);
    mainShader->set("windDir", windDirNow);
    mainShader->set("windGust", windGustNow);
    mainShader->set("windAffectsObject", 0);

    for(int i=0; i<(int)lights.size(); ++i) {
      Light &light = lights[i];
      
      mainShader->set(std::string("lightSources[")+std::to_string(i)+std::string("].position"), light.position);
      mainShader->set(std::string("lightSources[")+std::to_string(i)+std::string("].color"), light.color);
      mainShader->set(std::string("lightSources[")+std::to_string(i)+std::string("].intensity"), light.intensity);
      mainShader->set(std::string("lightSources[")+std::to_string(i)+std::string("].isActive"), 1);
      
      glActiveTexture(GL_TEXTURE0 + light.shadowMapTexOnGPU);
      glBindTexture(GL_TEXTURE_2D, light.shadowMap.getTextureId());
      mainShader->set(std::string("shadowMaps[")+std::to_string(i)+std::string("]"), static_cast<int>(light.shadowMapTexOnGPU));
      
      mainShader->set(std::string("lightDepthMVP[")+std::to_string(i)+std::string("]"), light.depthMVP);
    }

    // back-wall
    mainShader->set("material.albedo", glm::vec3(0.29, 0.51, 0.82));
    mainShader->set("material.useNormalMap", 1);
    mainShader->set("material.useColorMap", 1);
    mainShader->set("material.useBarkTexture", 0);
    mainShader->set("material.useLeafTexture", 0);
    mainShader->set("windAffectsObject", 0);
    glActiveTexture(GL_TEXTURE0 + normalMapTextureSlot);
    glBindTexture(GL_TEXTURE_2D, normalMapTexture);
    mainShader->set("normalMapTexture", static_cast<int>(normalMapTextureSlot));
    glActiveTexture(GL_TEXTURE0 + colorMapTextureSlot);
    glBindTexture(GL_TEXTURE_2D, colorMapTexture);
    mainShader->set("colorMapTexture", static_cast<int>(colorMapTextureSlot));
    mainShader->set("modelMat", planeMat);
    mainShader->set("normMat", glm::mat3(glm::inverseTranspose(planeMat)));
    plane->render();

    // floor
    mainShader->set("material.albedo", glm::vec3(0.8, 0.8, 0.9));
    mainShader->set("material.useNormalMap", 0);
    mainShader->set("material.useColorMap", 0); 
    mainShader->set("material.useBarkTexture", 0);
    mainShader->set("material.useLeafTexture", 0);
    mainShader->set("windAffectsObject", 0);
    mainShader->set("modelMat", floorMat);
    mainShader->set("normMat", glm::mat3(glm::inverseTranspose(floorMat)));
    plane->render();

    // Plant branches (bark texture)
    if(plant && plant->plantMesh) {
        mainShader->set("material.albedo", glm::vec3(0.4, 0.25, 0.15));
        mainShader->set("material.useNormalMap", 0);
        mainShader->set("material.useColorMap", 0);
        mainShader->set("material.useBarkTexture", 1);
        mainShader->set("material.useLeafTexture", 0);
        mainShader->set("windAffectsObject", 0);
        glActiveTexture(GL_TEXTURE0 + barkTextureSlot);
        glBindTexture(GL_TEXTURE_2D, barkTexture);
        mainShader->set("barkTexture", static_cast<int>(barkTextureSlot));
        mainShader->set("modelMat", glm::mat4(1.0));
        mainShader->set("normMat", glm::mat3(1.0));
        plant->plantMesh->render();
    }

    // Plant leaves (atlas texture + alpha discard)
    if(plant && plant->leafMesh) {
        glDisable(GL_CULL_FACE);
        mainShader->set("material.albedo", glm::vec3(0.2, 0.55, 0.15));
        mainShader->set("material.useNormalMap", 0);
        mainShader->set("material.useColorMap", 0);
        mainShader->set("material.useBarkTexture", 0);
        mainShader->set("material.useLeafTexture", 1);
        mainShader->set("windAffectsObject", 0);
        glActiveTexture(GL_TEXTURE0 + leafColorTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafColorTexture);
        mainShader->set("leafColorTexture", static_cast<int>(leafColorTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafColorTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafColorTextureFall);
        mainShader->set("leafColorTextureFall", static_cast<int>(leafColorTextureFallSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTexture);
        mainShader->set("leafOpacityTexture", static_cast<int>(leafOpacityTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTextureFall);
        mainShader->set("leafOpacityTextureFall", static_cast<int>(leafOpacityTextureFallSlot));
        mainShader->set("modelMat", glm::mat4(1.0));
        mainShader->set("normMat", glm::mat3(1.0));
        plant->leafMesh->render();
        glEnable(GL_CULL_FACE);
    }
    if(plant && plant->fallenLeafMesh && !plant->fallenLeafMesh->triangleIndices().empty()) {
        glDisable(GL_CULL_FACE);
        mainShader->set("material.albedo", glm::vec3(0.2, 0.55, 0.15));
        mainShader->set("material.useNormalMap", 0);
        mainShader->set("material.useColorMap", 0);
        mainShader->set("material.useBarkTexture", 0);
        mainShader->set("material.useLeafTexture", 1);
        mainShader->set("windAffectsObject", 0);
        bool keepOldFallenLook = (currentSeason == Season::Spring || targetSeason == Season::Spring);
        float fallenBlend = keepOldFallenLook ? 1.0f : seasonParams.leafTextureBlend;
        glm::vec3 fallenTint = keepOldFallenLook ? glm::vec3(1.0f, 0.95f, 0.88f) : seasonParams.leafTint;
        float fallenAlpha = keepOldFallenLook ? 1.0f : seasonParams.leafAlphaScale;
        mainShader->set("leafSeasonBlend", fallenBlend);
        mainShader->set("leafSeasonTint", fallenTint);
        mainShader->set("leafAlphaScale", fallenAlpha);
        glActiveTexture(GL_TEXTURE0 + leafColorTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafColorTexture);
        mainShader->set("leafColorTexture", static_cast<int>(leafColorTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafColorTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafColorTextureFall);
        mainShader->set("leafColorTextureFall", static_cast<int>(leafColorTextureFallSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTexture);
        mainShader->set("leafOpacityTexture", static_cast<int>(leafOpacityTextureSlot));
        glActiveTexture(GL_TEXTURE0 + leafOpacityTextureFallSlot);
        glBindTexture(GL_TEXTURE_2D, leafOpacityTextureFall);
        mainShader->set("leafOpacityTextureFall", static_cast<int>(leafOpacityTextureFallSlot));
        mainShader->set("modelMat", glm::mat4(1.0));
        mainShader->set("normMat", glm::mat3(1.0));
        plant->fallenLeafMesh->render();
        mainShader->set("leafSeasonBlend", seasonParams.leafTextureBlend);
        mainShader->set("leafSeasonTint", seasonParams.leafTint);
        mainShader->set("leafAlphaScale", seasonParams.leafAlphaScale);
        glEnable(GL_CULL_FACE);
    }

    mainShader->stop();

    if(saveViewportScreenshotPpm) {
      saveViewportPpmFile(std::string("screenshot_") + std::to_string(screenshotCounter) + ".ppm");
      screenshotCounter += 1;
      saveViewportScreenshotPpm = false;
    }
  }
    
  void subdivideCenterMesh() {
    rhino->subdivideLoop();
    rhino->init();
  }
};

Scene g_scene;

void printHelp()
{
  std::cout <<
    "> Help:" << std::endl <<
    "    Mouse commands:" << std::endl <<
    "    * Left button: rotate camera" << std::endl <<
    "    * Middle button: zoom" << std::endl <<
    "    * Right button: pan camera" << std::endl <<
    "    * Scroll wheel: zoom in/out" << std::endl <<
    "    Keyboard commands:" << std::endl <<
    "    * H: print this help" << std::endl <<
    "    * T: toggle continuous growth animation" << std::endl <<
    "    * G: unlock next growth stage (L-system) / toggle growth (legacy)" << std::endl <<
    "    * W: toggle wind physics" << std::endl <<
    "    * L: subdivide plant mesh (Loop subdivision)" << std::endl <<
    "    * U: undo subdivision" << std::endl <<
    "    * R: restart plant growth from seed" << std::endl <<
    "    * C: advance season (Spring -> Summer -> Fall -> Winter)" << std::endl <<
    "    * S: save viewport screenshot into PPM file" << std::endl <<
    "    * Shift+S: save shadow maps into PPM files" << std::endl <<
    "    * F1: toggle wireframe/surface rendering" << std::endl <<
    "    * ESC: quit the program" << std::endl;
}

void windowSizeCallback(GLFWwindow *window, int width, int height)
{
  g_windowWidth = width;
  g_windowHeight = height;
  g_cam->setAspectRatio(static_cast<float>(width)/static_cast<float>(height));
  glViewport(0, 0, (GLint)width, (GLint)height);
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
  if(action == GLFW_PRESS && key == GLFW_KEY_H) {
    printHelp();
  } else if(action == GLFW_PRESS && key == GLFW_KEY_S) {
    if((mods & GLFW_MOD_SHIFT) != 0) {
      g_scene.saveShadowMapsPpm = true;
    } else {
      g_scene.saveViewportScreenshotPpm = true;
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_C) {
    g_scene.advanceToNextSeason();
  } else if(action == GLFW_PRESS && key == GLFW_KEY_G) {
    if(g_scene.plant) {
      if(g_scene.plant->useLSystem) {
        g_scene.plant->advanceGrowthStage();
        // Let the newly unlocked stage visibly progress even if timer is paused.
        g_scene.plant->update(0.70f);
      } else {
        std::cout << "[Plant] Toggling growth..." << std::endl;
        for(auto& branch : g_scene.plant->branches) {
          branch.isGrowing = !branch.isGrowing;
        }
      }
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_W) {
    if(g_scene.plant) {
      g_scene.plant->toggleWindPhysics();
      std::cout << "[Wind] " << (g_scene.plant->isWindPhysicsEnabled() ? "Enabled" : "Disabled") << std::endl;
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_L) {
    if(g_scene.plant && g_scene.plant->plantMesh) {
        std::cout << "[Plant] Subdividing plant mesh..." << std::endl;
        g_scene.plant->plantMesh->subdivideLoop();
        g_scene.plant->plantMesh->init();
        std::cout << "[Plant] Plant mesh subdivided!" << std::endl;
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_U) {
    if(g_scene.plant && g_scene.plant->plantMesh) {
        g_scene.plant->plantMesh->undo();
        g_scene.plant->plantMesh->init();
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_T) {
    g_appTimerStoppedP = !g_appTimerStoppedP;
  } else if(action == GLFW_PRESS && key == GLFW_KEY_F1) {
    GLint mode[2];
    glGetIntegerv(GL_POLYGON_MODE, mode);
    glPolygonMode(GL_FRONT_AND_BACK, mode[1] == GL_FILL ? GL_LINE : GL_FILL);
  } else if(action == GLFW_PRESS && key == GLFW_KEY_R) {
    if(g_scene.plant) {
        std::cout << "[Plant] Restarting growth..." << std::endl;
        bool keepWind = g_scene.plant->isWindPhysicsEnabled();
        g_scene.plant = std::make_shared<Plant>();
        g_scene.plant->setWindPhysicsEnabled(keepWind);
        g_scene.lastAppliedLeafDensity = -1.0f;
        g_scene.lastAppliedLeafSizeScale = -1.0f;
        g_scene.lastAppliedLeafAtlasCols = -1;
        g_scene.lastAppliedLeafAtlasRows = -1;
        g_scene.lastAppliedLeafVariantCount = -1;
        g_scene.applySeasonToPlant(true);
        std::cout << "[Plant] Growth restarted!" << std::endl;
    }
  } else if(action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
    glfwSetWindowShouldClose(window, true);
  }
}

void cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
  int width, height;
  glfwGetWindowSize(window, &width, &height);
  const float normalizer = static_cast<float>((width + height)/2);
  const float dx = static_cast<float>((g_baseX - xpos) / normalizer);
  const float dy = static_cast<float>((ypos - g_baseY) / normalizer);
  if(g_rotatingP) {
    const glm::vec3 dRot(-dy*M_PI, dx*M_PI, 0.0);
    g_cam->setRotation(g_baseRot + dRot);
  } else if(g_panningP) {
    g_cam->setPosition(g_baseTrans + g_meshScale*glm::vec3(dx, dy, 0.0));
  } else if(g_zoomingP) {
    g_cam->setPosition(g_baseTrans + g_meshScale*glm::vec3(0.0, 0.0, dy));
  }
}

void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
  g_cam->setPosition(g_cam->getPosition() + g_meshScale * glm::vec3(0.0, 0.0, -yoffset * 0.3f));
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
  if(button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if(!g_rotatingP) {
      g_rotatingP = true;
      glfwGetCursorPos(window, &g_baseX, &g_baseY);
      g_baseRot = g_cam->getRotation();
    }
  } else if(button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
    g_rotatingP = false;
  } else if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    if(!g_panningP) {
      g_panningP = true;
      glfwGetCursorPos(window, &g_baseX, &g_baseY);
      g_baseTrans = g_cam->getPosition();
    }
  } else if(button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
    g_panningP = false;
  } else if(button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
    if(!g_zoomingP) {
      g_zoomingP = true;
      glfwGetCursorPos(window, &g_baseX, &g_baseY);
      g_baseTrans = g_cam->getPosition();
    }
  } else if(button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE) {
    g_zoomingP = false;
  }
}

void initGLFW()
{
  if(!glfwInit()) {
    std::cerr << "ERROR: Failed to init GLFW" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
  g_window = glfwCreateWindow(g_windowWidth, g_windowHeight, "IGR202 - Plant Growth Simulation", nullptr, nullptr);
  if(!g_window) {
    std::cerr << "ERROR: Failed to open window" << std::endl;
    glfwTerminate();
    std::exit(EXIT_FAILURE);
  }

  glfwMakeContextCurrent(g_window);
  glfwGetFramebufferSize(g_window, &g_windowWidth, &g_windowHeight);

  glfwSetWindowSizeCallback(g_window, windowSizeCallback);
  glfwSetKeyCallback(g_window, keyCallback);
  glfwSetCursorPosCallback(g_window, cursorPosCallback);
  glfwSetMouseButtonCallback(g_window, mouseButtonCallback);
  glfwSetScrollCallback(g_window, scrollCallback);
}

void clear();
void exitOnCriticalError(const std::string &message)
{
  std::cerr << "> [Critical error]" << message << std::endl;
  std::cerr << "> [Clearing resources]" << std::endl;
  clear();
  std::cerr << "> [Exit]" << std::endl;
  std::exit(EXIT_FAILURE);
}

void initOpenGL()
{
  if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    exitOnCriticalError("[Failed to initialize OpenGL context]");

 #ifdef SUPPORT_GL_DEBUG
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(glDebugOutput, nullptr);
#endif

  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);
  glDepthFunc(GL_LESS);
  glEnable(GL_DEPTH_TEST);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

  try {
    g_scene.mainShader = ShaderProgram::genBasicShaderProgram("src/vertexShader.glsl", "src/fragmentShader.glsl");
    g_scene.mainShader->stop();
  } catch(std::exception &e) {
    exitOnCriticalError(std::string("[Error loading shader program]") + e.what());
  }
  try {
    g_scene.shadomMapShader = ShaderProgram::genBasicShaderProgram("src/vertexShaderShadowMap.glsl", "src/fragmentShaderShadowMap.glsl");
    g_scene.shadomMapShader->stop();
  } catch(std::exception &e) {
    exitOnCriticalError(std::string("[Error loading shader program]") + e.what());
  }
}

void initScene(const std::string &meshFilename)
{
  int width, height;
  glfwGetWindowSize(g_window, &width, &height);
  g_cam = std::make_shared<Camera>();
  g_cam->setAspectRatio(static_cast<float>(width)/static_cast<float>(height));

  {
    g_scene.rhino = std::make_shared<Mesh>();
    try {
      loadOFF(meshFilename, g_scene.rhino);
    } catch(std::exception &e) {
      exitOnCriticalError(std::string("[Error loading mesh]") + e.what());
    }
    g_scene.rhino->init();

    g_scene.plane = std::make_shared<Mesh>();
    g_scene.plane->addPlan();
    g_scene.plane->init();
    g_scene.planeMat = glm::translate(glm::mat4(1.0), glm::vec3(0, 0, -1.0));
    g_scene.floorMat = glm::translate(glm::mat4(1.0), glm::vec3(0, -1.0, 0))*
      glm::rotate(glm::mat4(1.0), (float)(-0.5f*M_PI), glm::vec3(1.0, 0.0, 0.0));

    g_scene.plant = std::make_shared<Plant>();
    g_scene.currentSeason = Season::Spring;
    g_scene.targetSeason = Season::Spring;
    g_scene.seasonTransitionTime = 0.0f;
    g_scene.seasonParams = Scene::paramsForSeason(g_scene.currentSeason);
    g_scene.lastAppliedLeafDensity = -1.0f;
    g_scene.lastAppliedLeafSizeScale = -1.0f;
    g_scene.lastAppliedLeafAtlasCols = -1;
    g_scene.lastAppliedLeafAtlasRows = -1;
    g_scene.lastAppliedLeafVariantCount = -1;
    g_scene.applySeasonToPlant(true);
    std::cout << "[Season] Start season: " << Scene::seasonName(g_scene.currentSeason) << std::endl;
    std::cout << "[Scene] Plant initialized!" << std::endl;
  }

  // Textures
  g_scene.normalMapTexture = loadTextureFromFileToGPU("data/normal.png");
  g_scene.normalMapTextureSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.normalMapTextureSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.normalMapTexture);
  ++g_availableTextureSlot;
  
  g_scene.colorMapTexture = loadTextureFromFileToGPU("data/color.png");
  g_scene.colorMapTextureSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.colorMapTextureSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.colorMapTexture);
  ++g_availableTextureSlot;

  g_scene.barkTexture = loadTextureFromFileToGPU("data/Bark012_2K-JPG_Color.jpg");
  g_scene.barkTextureSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.barkTextureSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.barkTexture);
  ++g_availableTextureSlot;
  std::cout << "[Textures] Bark texture loaded, slot " << g_scene.barkTextureSlot << std::endl;

  g_scene.leafColorTexture = loadTextureFromFileToGPU("data/LeafSet024_2K-JPG_Color.jpg");
  g_scene.leafColorTextureSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.leafColorTextureSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.leafColorTexture);
  ++g_availableTextureSlot;
  std::cout << "[Textures] Leaf color texture loaded, slot " << g_scene.leafColorTextureSlot << std::endl;

  g_scene.leafOpacityTexture = loadTextureFromFileToGPU("data/LeafSet024_2K-JPG_Opacity.jpg");
  g_scene.leafOpacityTextureSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.leafOpacityTextureSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.leafOpacityTexture);
  ++g_availableTextureSlot;
  std::cout << "[Textures] Leaf opacity texture loaded, slot " << g_scene.leafOpacityTextureSlot << std::endl;

  g_scene.leafColorTextureFall = loadTextureFromFileToGPU("data/LeafSet015_2K-JPG/LeafSet015_2K-JPG_Color.jpg");
  g_scene.leafColorTextureFallSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.leafColorTextureFallSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.leafColorTextureFall);
  ++g_availableTextureSlot;
  std::cout << "[Textures] Fall leaf color texture loaded, slot " << g_scene.leafColorTextureFallSlot << std::endl;

  g_scene.leafOpacityTextureFall = loadTextureFromFileToGPU("data/LeafSet015_2K-JPG/LeafSet015_2K-JPG_Opacity.jpg");
  g_scene.leafOpacityTextureFallSlot = g_availableTextureSlot;
  glActiveTexture(GL_TEXTURE0 + g_scene.leafOpacityTextureFallSlot);
  glBindTexture(GL_TEXTURE_2D, g_scene.leafOpacityTextureFall);
  ++g_availableTextureSlot;
  std::cout << "[Textures] Fall leaf opacity texture loaded, slot " << g_scene.leafOpacityTextureFallSlot << std::endl;

  // Lights
  const glm::vec3 pos[3] = {
    glm::vec3(0.0, 1.0, 1.0),
    glm::vec3(0.3, 2.0, 0.4),
    glm::vec3(0.2, 0.4, 2.0),
  };
  const glm::vec3 col[3] = {
    glm::vec3(1.0, 1.0, 1.0),
    glm::vec3(1.0, 1.0, 0.8),
    glm::vec3(1.0, 1.0, 0.8),
  };
  unsigned int shadow_map_width=2000, shadow_map_height=2000;
  for(int i=0; i<3; ++i) {
    g_scene.lights.push_back(Light());
    Light &a_light = g_scene.lights[g_scene.lights.size() - 1];
    a_light.position = pos[i];
    a_light.color = col[i];
    a_light.intensity = 0.5f;
    a_light.shadowMapTexOnGPU = g_availableTextureSlot;
    glActiveTexture(GL_TEXTURE0 + a_light.shadowMapTexOnGPU);
    a_light.allocateShadowMapFbo(shadow_map_width, shadow_map_height);
    ++g_availableTextureSlot;
  }

  glm::vec3 meshCenter;
  float meshRadius;
  g_scene.rhino->computeBoundingSphere(meshCenter, meshRadius);
  g_scene.scene_center = meshCenter;
  g_scene.scene_radius = meshRadius;
  g_meshScale = g_scene.scene_radius;
  g_cam->setPosition(g_scene.scene_center + glm::vec3(0.0, 0.0, 3.0*g_meshScale));
  g_cam->setNear(g_meshScale/100.f);
  g_cam->setFar(6.0*g_meshScale);
}

void init(const std::string &meshFilename)
{
  initGLFW();
  initOpenGL();
  initScene(meshFilename);
}

void clear()
{
  g_cam.reset();
  g_scene.rhino.reset();
  g_scene.plane.reset();
  g_scene.plant.reset();
  g_scene.mainShader.reset();
  g_scene.shadomMapShader.reset();
  glfwDestroyWindow(g_window);
  glfwTerminate();
}

void render()
{
  g_scene.render();
}

void update(float currentTime)
{
  static bool firstFrame = true;
  static float previousTime = 0.0f;
  float dt = 0.0f;
  if(firstFrame) {
    previousTime = currentTime;
    firstFrame = false;
  } else {
    dt = glm::max(0.0f, currentTime - previousTime);
    previousTime = currentTime;
  }

  g_scene.updateSeason(dt);

  if(!g_appTimerStoppedP) {
    g_appTimer += dt;
    
    if(g_scene.plant) {
      g_scene.plant->update(dt);
    }
  }

  // Wind runs independently from growth timer so W shows immediate motion.
  if(g_scene.plant) {
    g_scene.plant->updateWindSimulation(dt);
  }

  // Update fallen leaves after potential branch-mesh rebuilds so detached leaves
  // appear as falling leaves in the same frame.
  if(g_scene.plant) {
    g_scene.plant->updateFallingLeaves(dt);
  }
}

void usage(const char *command)
{
  std::cerr << "Usage : " << command << " [<file.off>]" << std::endl;
  std::exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  if(argc > 2) usage(argv[0]);
  init(argc==1 ? DEFAULT_MESH_FILENAME : argv[1]);
  while(!glfwWindowShouldClose(g_window)) {
    update(static_cast<float>(glfwGetTime()));
    render();
    glfwSwapBuffers(g_window);
    glfwPollEvents();
  }
  clear();
  std::cout << " > Quit" << std::endl;
  return EXIT_SUCCESS;
}
