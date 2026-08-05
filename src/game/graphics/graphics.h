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

// how much to lift off a vertex before shooting ray at light
const float SHADOW_BIAS = 1e-3f;

struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float scale = 1.0f;

    // builds the local-to-world matrix for a transform
    // translate, then yaw (Y), then pitch (-X), then roll (Z), then scale.
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

    // returns true if the local transforms are equal
    bool operator==(const Transform& other)
    {
        return (x == other.x && y == other.y && z == other.z
                && yaw == other.yaw && pitch == other.pitch
                && roll == other.roll && scale == other.scale);
    }
    
    Transform& operator=(const Transform&) = default;
};

struct Skeleton
{   
    // for converting vertex positions from global to bone local
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

// One bone's local transform at a single instant. Kept as TRS rather than a
// matrix so two poses can be blended joint by joint (slerp on the rotation)
// before the hierarchy walk folds them together — blending the matrices instead
// shears the bones as they cross over.
struct BonePose
{
    glm::vec3 pos{0.0f};
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
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

// a world-space triangle that can block light during the bake
// and or collide with player.
struct Tri { glm::vec3 a, b, c; };

// axis aligned bounding box, used as a cheap first pass when calculating
// which triangles are close to an entity.
struct AABB
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    bool overlaps(const AABB& other) const
    {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y
            && min.z <= other.max.z && max.z >= other.min.z;
    }

    void expand(float margin)
    {
        min -= glm::vec3(margin);
        max += glm::vec3(margin);
    }
};

// a node in the scene graph: contains local transform, world matrix, and
// children vector. carries no geometry.
//
// when walking the scene nodes, this struct is used as the recursive side of
// things. it will call the correct function on its children. while Mesh overrides
// all of these functions it still relies on them when it passes its world to them.
struct Object
{
    std::string name;
    std::string tag;

    Transform transform; // local transform

    // world space transform matrix recalculated each frame by Draw().
    // kept as a matrix to avoid gimbal lock.
    glm::mat4 world = glm::mat4(1.0f);
    std::vector<Object*> children = {};

    // pointer to its parent object, nullptr if it has none.
    Object* parent = nullptr; 


    Object() = default;

    Object(const Transform& t)
        : transform(t) {}

    virtual void Upload();

    // refreshes the world matrices of every object so if any changes
    // were made to an objects local transform they are shown in real
    // time. must be run BEFORE Draw()
    virtual void Compose();

    virtual void Draw();

    // the two bake passes meant to collect static Tris that block light and set vertex 
    // color. both take in parentWorld to calculate world because these run before the 
    // first Compose() call which means only the roots have a valid world.
    virtual void CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out);
    virtual void BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders);

    // collects Tris that are both static and collides same as CollectOccluders except
    // that CollectOccluders cares if an object is static or not. these must be kept seperate
    // else the user wont be able to seperate static colliders with static non colliders.
    virtual void CollectColliders(const glm::mat4& parentWorld, std::vector<Tri>& out);

    virtual void Raycast(const glm::vec3& origin, const glm::vec3& dir,
                         float& closest, Object*& hit);

    // Virtual so deleting a Mesh through an Object* still frees its GL handles.
    virtual ~Object() = default;
};

// invisible capsule that collides with the world and can be a node in scene graph.
struct Player : Object
{
    glm::vec3 velocity{0.0f};
    float radius = 0.3f;
    float height = 1.8f;
    bool grounded = false;
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
    bool collides = false;

    Skeleton skeleton;
    std::vector<Animation> animations; // all clips baked from the FBX (one per anim stack)
    int currentAnim = -1;   // index into `animations` of the clip currently playing
    float animTime = 0.0f; // seconds into the current clip added to each frame
    int nextAnim = -1;

    // Cross-fade state. `lastPose` is the local pose computePose() actually put on
    // screen last frame; SetAnimation() copies it into `blendFrom` so the armature
    // can ease out of it instead of snapping. While blendDuration > 0 the pose is
    // blendFrom -> new clip, weighted by blendElapsed / blendDuration.
    std::vector<BonePose> lastPose;
    std::vector<BonePose> blendFrom;
    float blendDuration = 0.0f; // 0 means "not blending"
    float blendElapsed = 0.0f;

    Mesh() = default;

    void Upload() override;

    void Draw() override;

    void CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out) override;
    void BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders) override;

    void CollectColliders(const glm::mat4& parentWorld, std::vector<Tri>& out) override;
    
    void Raycast(const glm::vec3& origin, const glm::vec3& dir,
                 float& closest, Object*& hit) override;

    // Select which baked clip plays. Both reset animTime so the new clip starts
    // from its first frame. The string form returns false if no clip matches.
    //
    // `blendTime` is how many seconds the armature spends easing out of the pose
    // it is currently in and into the new clip — 0 (the default) snaps, as it
    // always used to. A mesh that has never been drawn has no pose to leave, so
    // its first clip starts instantly whatever blendTime says.
    void SetAnimation(int index, float blendTime = 0.0f, int nextAnim = -1);
    bool SetAnimation(const std::string& name, float blendTime = 0.0f, int nextAnim = -1);

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
extern Player player;
extern float yaw;
extern float pitch;
extern float lastX, lastY;   // last mouse pos (start at screen center)
extern bool  firstMouse;

extern float deltaTime, lastFrame;

extern const char* vertexShaderSrc;
extern const char* fragmentShaderSrc;

extern std::vector<Object*> parents;
extern std::vector<Light*> lights;
extern std::vector<UIElement*> uiElements;
extern std::vector<Tri> occluders;
extern std::vector<Tri> colliders;
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
extern int lightModeLoc, objectLightLoc, textModeLoc;

extern float lightAmbient;

unsigned int compileShader(GLenum type, const char* src);

int initWindow();

void buildShaderProgram();

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

Mesh makeObj(const char* objPath, const char* texPath,
             Transform Transform, bool isStatic, bool collides);

// for animated objects
Mesh makeFbx(const char* objPath, const char* texPath,
             Transform Transform);

UIElement makeUIElement(const char* texPath, float x, float y, float scale);

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