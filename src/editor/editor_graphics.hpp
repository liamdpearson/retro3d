// A simplified version of graphics.hpp in the src/game folder.

#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>                  // vec3, mat4, basic types
#include <glm/gtc/matrix_transform.hpp> // perspective, lookAt, rotate, radians
#include <glm/gtc/type_ptr.hpp>         // value_ptr (hand a matrix to OpenGL)
#include <glm/gtc/quaternion.hpp>       // quat slerp, mat4_cast

#include <nlohmann/json.hpp> // json parser

#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <map>
#include <utility>
#include <tuple>
#include <algorithm>
#include <cmath>
#include <iostream>



const float PI = 3.14159265358979323;
const int VERTEX_FLOATS = 19;
const float SAMPLE = 1.0f;
const int MAX_BONES = 100;

// How far to lift a shadow ray off the surface it starts on. Too small and
// surfaces self-shadow into speckle; too large and contact shadows detach.
// Tuned for a scene measured in metres — rescale if your units change.
const float SHADOW_BIAS = 1e-3f;

struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    // Defaults to 1, never 0: a default-constructed node is an identity pivot.
    // A zero here silently collapses every descendant to a point.
    float scale = 1.0f;

    // Build the local-to-world matrix for a transform, matching the convention
    // used by drawObj(): translate, then yaw (Y), then pitch (-X), then roll (Z), then scale.
    glm::mat4 matrix() const
    {
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, y, z));
        m = glm::rotate(m, glm::radians(yaw),   glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(pitch), glm::vec3(-1, 0, 0));
        m = glm::rotate(m, glm::radians(roll), glm::vec3(0, 0, 1));
        m = glm::scale(m, glm::vec3(scale));
        return m;
    }

    bool operator==(const Transform& other)
    {
        return (x == other.x && y == other.y && z == other.z
                && yaw == other.yaw && pitch == other.pitch
                && roll == other.roll && scale == other.scale);
    }

    void operator+(const Transform& other)
    {
        x += other.x; y += other.y; z += other.z;
        yaw += other.yaw; pitch += other.pitch; 
        roll += other.roll; scale += other.scale;
    }
    
    Transform& operator=(const Transform&) = default;
};


struct Skeleton
{
    std::vector<glm::mat4> inverseBind;
    std::vector<int> parent;
    std::vector<std::string> names;
    std::vector<glm::mat4> parentWorld;
};


struct BoneTrack
{
    std::vector<glm::vec3> pos;
    std::vector<glm::quat> rot;
    std::vector<glm::vec3> scale;
};

struct Animation
{
    std::string name;      // the FBX anim-stack name, used to select clips by name
    std::vector<BoneTrack> tracks;
    int frameCount = 0;
    float fps = 30.0f;
    float duration = 0.0f; // seconds
};


// A node in the scene graph: a transform, its composed world matrix, and its
// children. Carries no geometry — a bare Object is a pivot / attachment point /
// group, and its transform still composes into everything beneath it. Geometry
// lives in Mesh, which derives from this.
//
// Every walk over the graph is a virtual pair: the base does the recursion and
// nothing else, Mesh overrides it to do the per-mesh work and then chains to the
// base. That way a mesh-less node is never a special case at the call site.
struct Object
{
    std::string name;
    std::string type;

    Transform transform; // local transform

    // World-space transform matrix, recomputed each frame by Draw(). Keeping it
    // as a matrix (rather than decomposing back to yaw/pitch) avoids gimbal lock
    // and preserves any roll produced by composing rotated parents and children.
    glm::mat4 world = glm::mat4(1.0f);
    std::vector<Object*> children = {};


    Object() = default;

    Object(const Transform& t)
        : transform(t) {}

    virtual void Upload();

    // Refresh `world` down the graph. Kept separate from Draw() so the camera
    // has derived its pos/front before clearBG() builds the view matrix — see
    // Object::Compose(). Every frame must Compose() before it Draws().
    virtual void Compose();

    virtual void Draw();

    // Recursion half of the raycast walk. Unlike the bake passes this reads each
    // node's composed `world` directly rather than a parentWorld, so it is only
    // valid after Compose() has run this frame. `closest` seeds/carries the best
    // hit distance and `hit` the object at it — see the free raycast() below.
    virtual void Raycast(const glm::vec3& origin, const glm::vec3& dir,
                         float& closest, Object*& hit);

    virtual bool isLight() const { return false; }

    // Virtual so deleting a Mesh through an Object* still frees its GL handles.
    virtual ~Object() = default;
};


// A scene node that also has geometry: the CPU mesh, its GL handles, and (for
// skinned FBX) a skeleton plus its baked clips.
struct Mesh : Object
{
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int texture = 0;
    GLsizei indexCount = 0;

    Skeleton skeleton;
    std::vector<Animation> animations; // all clips baked from the FBX (one per anim stack)
    int currentAnim = -1;   // index into `animations` of the clip currently playing
    float animTime = 0.0f; // seconds into the current clip added to each frame

    Mesh() = default;

    void Upload() override;

    void Draw() override;

    void Raycast(const glm::vec3& origin, const glm::vec3& dir,
                 float& closest, Object*& hit) override;

    // Select which baked clip plays. Both reset animTime so the new clip starts
    // from its first frame. The string form returns false if no clip matches.
    void SetAnimation(int index);
    bool SetAnimation(const std::string& name);

    ~Mesh() override;
};


struct LightMesh : Mesh
{
    glm::vec3 color{1.0f};
    float intensity;
    float radius;

    bool isLight() const override { return true; }
};


struct CameraMesh : Mesh
{
    float FOV;
};


struct ObjMesh : Mesh
{
    bool isStatic;
    std::string objSrc;
    std::string texSrc;
};


struct FbxMesh : Mesh
{
    bool isStatic;
    std::string objSrc;
    std::string texSrc;
};


struct Camera : Object
{
    float FOV;

    glm::vec3 pos;
    glm::vec3 front;
    glm::vec3 up;

    Camera(const float FOV, const glm::vec3 pos,
           glm::vec3 front, glm::vec3 up)
        : FOV(FOV), pos(pos), front(front), up(up) {}

    void Upload() override;

    // Derives pos/front/up from `world`. The camera must be reachable from
    // `parents` for this to run at all — unparent it and the view silently
    // freezes at whatever the constructor set.
    void Compose() override;

    ~Camera() = default;
};


struct UIElement
{
    std::string name;

    float x = 0.0f;
    float y = 0.0f;
    unsigned int width = 1.0f;
    unsigned int height = 1.0f;
    float scale = 1.0f;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int texture = 0;
    GLsizei indexCount = 6;
};


struct Glyph
{
    float u0, v0, u1, v1; // uv coords for char on font atlas
    float w, h;           // glyph quad size
    float xoff, yoff;     // pen -> top-left of the quad (yoff is negative above baseline)
    float xadvance;       // how far the pen moves for the next glyph
};


struct Font
{
    GLuint atlas = 0;
    int atlasW = 0, atlasH = 0;
    float bakePixelHeight = 0.0f;
    float  ascent = 0.0f;       // baseline offset below the top edge
    float  lineHeight = 0.0f;   // pen drop for '\n'
    Glyph  glyphs[96];          // ASCII 32..126
};


struct UIText
{
    glm::vec2   pos;
    float       size = 1.0f;
    std::string text;
    Font*       font = nullptr;
    glm::vec3   color = glm::vec3(1.0f);
    bool        anchorLeft = true;

    // Rebuilt each frame by layoutText(); one call for the whole stirng.
    std::vector<float>        vertices;
    std::vector<unsigned int> indices;
    GLuint VAO = 0, VBO = 0, EBO = 0;
};

extern int SW;
extern int SH;

extern Camera camera;
extern float yaw;
extern float pitch;
extern float lastX, lastY;   // last mouse pos (start at screen center)
extern bool  firstMouse;

extern const float sensitivity;
extern float deltaTime, lastFrame;

extern const char* vertexShaderSrc;
extern const char* fragmentShaderSrc;

extern std::vector<Object*> parents;
extern std::vector<UIElement*> uiElements;

extern GLFWwindow* window;

extern unsigned int vs;
extern unsigned int fs;
extern unsigned int shaderProgram;
extern int modelLoc, viewLoc, projectionLoc;
extern int boneMatricesLoc;
extern int lightModeLoc, objectLightLoc, textModeLoc;

unsigned int compileShader(GLenum type, const char* src);

unsigned int loadTexture(const char* path);

std::vector<int> textureDimensions(const char* path);

bool loadOBJ(const char* path,
            std::vector<float>& outVerts,
            std::vector<unsigned int>& outIndices);

bool loadFBX(const char* path,
             std::vector<float>& outVerts,
             std::vector<unsigned int>& outIndices,
             Skeleton& outSkel,
             std::vector<Animation>& outAnims);

Mesh makeMesh(const char* objPath, const char* texPath,
              Transform transform);

LightMesh makeLightMesh(const char* objPath, const char* texPath,
                      Transform transform, glm::vec3 color,
                      float intensity, float radius);

CameraMesh makeCameraMesh(const char* objPath, const char* texPath,
                          Transform transform, float FOV);

ObjMesh makeObj(const char* objPath, const char* texPath,
             Transform Transform, bool isStatic);

// for animated objects
FbxMesh makeFbx(const char* objPath, const char* texPath,
             Transform Transform, bool isStatic);

UIElement makeUIElement(const char* texPath, float x, float y, float scale);

// Sample all lights at one world-space point, for lighting a whole mover at once.
glm::vec3 sampleLightAt(const glm::vec3& p);

// Cast a ray from `origin` along `dir` (need not be normalised) and return the
// nearest object whose geometry it hits within `maxDist`, or nullptr if none.
// Tests every mesh in `parents`, static and dynamic alike, against its current
// world-space triangles. Must run after this frame's Compose(), since it reads
// each node's composed `world`. Skinned meshes are tested in bind pose, not the
// animated pose (the CPU never sees the skinning the shader does).
Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist);

// Bake vertex lighting for every static object in `parents`, with static
// geometry casting shadows. Call after the scene graph is built and the lights
// are placed, before Upload(). Static objects must not move after baking.
void bakeSceneLighting();

void uploadObject(Mesh &obj);

void uploadUIElement(UIElement &ui);

void uploadUIText(UIText& t);

void layoutText(UIText& t);

void drawObj(Mesh& obj);

void drawUIElement(UIElement& ui);

void drawText(UIText& t);

void clearBG(float r, float g, float b, float a);

// Bracket the screen-space UI pass; see graphics.cpp for the coordinate space.
void beginUI();

void endUI();

Font bakeFont(const char* path, float pixelHeight);