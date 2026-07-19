#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>                  // vec3, mat4, basic types
#include <glm/gtc/matrix_transform.hpp> // perspective, lookAt, rotate, radians
#include <glm/gtc/type_ptr.hpp>         // value_ptr (hand a matrix to OpenGL)
#include <glm/gtc/quaternion.hpp>       // quat slerp, mat4_cast

#include <cstdio>
#include <vector>
#include <string>
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
    // Defaults to 1, never 0: a default-constructed node is an identity pivot.
    // A zero here silently collapses every descendant to a point.
    float scale = 1.0f;

    // Build the local-to-world matrix for a transform, matching the convention
    // used by drawObj(): translate, then yaw (Y), then pitch (-X), then scale.
    glm::mat4 matrix() const
    {
        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(x, y, z));
        m = glm::rotate(m, glm::radians(yaw),   glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(pitch), glm::vec3(-1, 0, 0));
        m = glm::scale(m, glm::vec3(scale));
        return m;
    }

    bool operator==(const Transform& other)
    {
        return (x == other.x && y == other.y && z == other.z
                && yaw == other.yaw && pitch == other.pitch
                && scale == other.scale);
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


struct Light
{
    glm::vec3 pos;
    glm::vec3 color;
    float intensity;
    float radius;
};


// A world-space triangle that can block light during the bake. Defined only in
// graphics.cpp — it appears here so the bake walk can be declared on the scene
// node, and an incomplete type is fine behind a reference.
struct Tri;


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
    Transform transform;

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

    // The two bake passes. Both take parentWorld explicitly rather than reading
    // `world`: the bake runs before the first Draw(), so `world` is only valid on
    // roots and every child's is still identity. See bakeSceneLighting().
    virtual void CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out);
    virtual void BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders);

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

    bool isStatic = false;

    Skeleton skeleton;
    std::vector<Animation> animations; // all clips baked from the FBX (one per anim stack)
    int currentAnim = 0;   // index into `animations` of the clip currently playing
    float animTime = 0.0f; // seconds into the current clip added to each frame

    Mesh() = default;

    void Upload() override;

    void Draw() override;

    void CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out) override;
    void BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders) override;

    // Select which baked clip plays. Both reset animTime so the new clip starts
    // from its first frame. The string form returns false if no clip matches.
    void SetAnimation(int index);
    bool SetAnimation(const std::string& name);

    ~Mesh() override;
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

extern int SW;
extern int SH;
extern float vps;

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
extern std::vector<Light*> lights;
extern std::vector<Tri> occluders;
extern std::vector<glm::vec3> lightGrid;

extern float minX;
extern float maxX;
extern float minY;
extern float maxY;
extern float minZ;
extern float maxZ;

extern GLFWwindow* window;

extern unsigned int vs;
extern unsigned int fs;
extern unsigned int shaderProgram;
extern int modelLoc, viewLoc, projectionLoc;
extern int boneMatricesLoc;
extern int lightModeLoc, objectLightLoc;

unsigned int compileShader(GLenum type, const char* src);

void framebufferSizeCallback(GLFWwindow*, int width, int height);

unsigned int loadTexture(const char* path);

void mouseCallback(GLFWwindow*, double xpos, double ypos);

bool loadOBJ(const char* path,
            std::vector<float>& outVerts,
            std::vector<unsigned int>& outIndices);

bool loadFBX(const char* path,
             std::vector<float>& outVerts,
             std::vector<unsigned int>& outIndices,
             Skeleton& outSkel,
             std::vector<Animation>& outAnims);

Mesh makeObj(const char* objPath, const char* texPath,
             Transform Transform, bool isStatic);

// for animated objects
Mesh makeFbx(const char* objPath, const char* texPath,
             Transform Transform);

// Sample all lights at one world-space point, for lighting a whole mover at once.
glm::vec3 sampleLightAt(const glm::vec3& p);

// Bake vertex lighting for every static object in `parents`, with static
// geometry casting shadows. Call after the scene graph is built and the lights
// are placed, before Upload(). Static objects must not move after baking.
void bakeSceneLighting();

void uploadObject(Mesh &obj);

void buildShaderProgram();

int initWindow();

void drawObj(Mesh& obj);

void clearBG(float r, float g, float b, float a);