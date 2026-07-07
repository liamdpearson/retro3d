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
const int VERTEX_FLOATS = 16;
const int MAX_BONES = 100;

struct Transform
{
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    float scale;

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
    glm::vec3 dir;
    glm::vec3 color;
};


struct Object
{
    Transform transform;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    unsigned int texture = 0;
    GLsizei indexCount = 0;


    // World-space transform matrix, recomputed each frame by Draw(). Keeping it
    // as a matrix (rather than decomposing back to yaw/pitch) avoids gimbal lock
    // and preserves any roll produced by composing rotated parents and children.
    glm::mat4 world = glm::mat4(1.0f);
    std::vector<Object*> children = {};
    bool billboard = false;  // if true, the quad is rotated to face the camera each frame
    Skeleton skeleton;
    std::vector<Animation> animations; // all clips baked from the FBX (one per anim stack)
    int currentAnim = 0;   // index into `animations` of the clip currently playing
    float animTime = 0.0f; // seconds into the current clip added to each frame

    Object() = default;

    void Upload();

    void Draw();

    // Select which baked clip plays. Both reset animTime so the new clip starts
    // from its first frame. The string form returns false if no clip matches.
    void SetAnimation(int index);
    bool SetAnimation(const std::string& name);

    ~Object();
};

extern int SW;
extern int SH;
extern float vps;

extern glm::vec3 cameraPos;
extern glm::vec3 cameraFront;
extern glm::vec3 cameraUp;

extern float yaw;
extern float pitch;
extern float lastX, lastY;   // last mouse pos (start at screen center)
extern bool  firstMouse;

extern const float sensitivity;
extern float deltaTime, lastFrame;

extern const char* vertexShaderSrc;
extern const char* fragmentShaderSrc;

extern std::vector<Object*> parents;

extern GLFWwindow* window;

extern unsigned int vs;
extern unsigned int fs;
extern unsigned int shaderProgram;
extern int modelLoc, viewLoc, projectionLoc;
extern int lightDirLoc, lightColorLoc, lightAmbientLoc;
extern int boneMatricesLoc;

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

Object makeObj(const char* objPath, const char* texPath,
               Transform Transform);

Object makeFbx(const char* objPath, const char* texPath,
               Transform Transform);

Object makeSprite(const char* texPath, Transform transform);

glm::mat4 billboardModel(const glm::vec3& pos, float scale, const glm::vec3& camPos);

void uploadObject(Object &obj);

void buildShaderProgram();

int initWindow();

void drawObj(Object& obj);

void clearBG(float r, float g, float b, float a);