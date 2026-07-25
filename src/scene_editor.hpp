#pragma once

#include "graphics.hpp"

using json = nlohmann::json;

// Initializes camera - values will be written over.
Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

std::unordered_map<std::string, Object*> parents;
std::vector<Light*> lights;
std::vector<UIElement*> uiElements;
Object* curObject = nullptr;
float* curElement = nullptr;
float editMultiplier = 1;
bool rightHeld = false;


// Initializes one object (and its subtree) from a scene.json entry. Returns the
// node so the caller can parent it; null means no node was produced — either the
// entry was a light, or its type was unrecognised.
//
// Nodes are heap-allocated because `parents`/`children` hold Object*, so they
// have to outlive this call.
static Object* buildNode(const json& j)
{
    const std::string type = j.value("type", "object");
    const std::string name = j.value("name", ""); 

    if (type == "light")
    {
        const json& p = j.at("position");

        Mesh* light = new Mesh(makeObj("assets/engine_assets/light/light.obj",
                                        "assets/engine_assets/light/light.png",
                                        Transform{p.at(0).get<float>(), p.at(1).get<float>(),
                                        p.at(2).get<float>(), 0.0f, 0.0f, 1.0f}, false));
        
        parents.insert({"light", light});
        return nullptr; // a light isn't a scene node — it never enters the graph
    }
    
    Transform transform{};
    if (j.contains("transform"))
    {
        const json& t = j["transform"]; // x, y, z, yaw, pitch, scale
        transform = Transform{t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>(),
                              t.at(3).get<float>(), t.at(4).get<float>(), t.at(5).get<float>()};
    }

    Object* node = nullptr;

    if (type == "mesh obj")
    {
        // C++17 guarantees the returned Mesh is constructed straight into the
        // allocation. Without that elision the temporary's destructor would run
        // and free the GL handles the stored copy still points at.
        node = new Mesh(makeObj(j.at("obj src").get<std::string>().c_str(),
                                j.at("tex src").get<std::string>().c_str(),
                                transform, j.value("isStatic", false)));
    }
    else if (type == "mesh fbx")
    {
        node = new Mesh(makeFbx(j.at("obj src").get<std::string>().c_str(),
                                j.at("tex src").get<std::string>().c_str(),
                                transform));
    }
    else if (type == "camera")
    {
        node = new Mesh(makeObj("assets/engine_assets/camera/camera.obj",
                                "assets/engine_assets/camera/camera.png",
                                transform, false));
    }
    else if (type == "object")
    {
        node = new Object(transform); // a bare pivot / attachment point
    }
    else
    {
        fprintf(stderr, "importScene: node '%s' has unknown type '%s', skipped\n",
                name.c_str(), type.c_str());
        return nullptr;
    }

    node->name = name;
    node->transform = transform;
    node->world = transform.matrix();

    for (const json& child : j.value("children", json::array()))
    {
        Object* c = buildNode(child);
        if (c && !node->children.insert({c->name, c}).second)
            fprintf(stderr, "importScene: duplicate child name '%s' under '%s', dropped\n",
                    c->name.c_str(), name.c_str());
    }

    return node;
}


void importScene(const char* path)
{
    std::ifstream file(path);
    if (!file)
    {
        fprintf(stderr, "importScene: could not open '%s'\n", path);
        return;
    }

    try
    {
        json scene;
        file >> scene;

        for (const json& node : scene.at("scene"))
        {
            Object* obj = buildNode(node);
            if (obj && !parents.insert({obj->name, obj}).second)
                fprintf(stderr, "importScene: duplicate root name '%s', dropped\n",
                        obj->name.c_str());
        }
    }
    catch(const json::exception& e)
    {
        // Bad key, bad type, malformed file — all land here. Leaving `parents`
        // partly built is fine: whatever loaded still draws.
        fprintf(stderr, "importScene: '%s': %s\n", path, e.what());
    }
    
}


void mouseCallback(GLFWwindow*, double xpos, double ypos)
{
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }

    float xoffset = (float)(xpos - lastX);
    float yoffset = (float)(lastY - ypos);
    lastX = float(xpos);
    lastY = float(ypos);

    if (curElement && rightHeld)
    {
        *curElement += xoffset * editMultiplier;
        *curElement += yoffset * editMultiplier;
    } else {
        camera.transform.yaw -= xoffset * sensitivity;
        camera.transform.pitch -= yoffset * sensitivity;

        if (camera.transform.pitch > 89.0f) camera.transform.pitch = 89.0f;
        if (camera.transform.pitch < -89.0f) camera.transform.pitch = -89.0f;
    }
}


int initWindow()
{
    if (!glfwInit()) { std::fprintf(stderr, "failed to init GLFW\n"); return -1; }

    GLFWmonitor* moniter = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(moniter);
    SW = mode->width;
    SH = mode->height;
    lastX = SW/2, lastY = SH/2;
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(SW, SH, "Game", moniter, nullptr);
    if (!window) { std::fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return -1; }
    glfwSetWindowPos(window, 0, 0);
    glfwMakeContextCurrent(window);

    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    { std::fprintf(stderr, "Failed to init GLAD\n"); glfwTerminate(); return -1; }

    glEnable(GL_DEPTH_TEST);
    return 0;
}


void buildShaderProgram() {

    // --- build the shader program ---
    vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
    fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    shaderProgram = glCreateProgram();

    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    int linked = 0;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(shaderProgram, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
    }

    modelLoc      = glGetUniformLocation(shaderProgram, "model");
    viewLoc       = glGetUniformLocation(shaderProgram, "view");
    projectionLoc = glGetUniformLocation(shaderProgram, "projection");

    lightModeLoc   = glGetUniformLocation(shaderProgram, "LightMode");
    objectLightLoc = glGetUniformLocation(shaderProgram, "ObjectLight");
    textModeLoc = glGetUniformLocation(shaderProgram, "TextMode");

    // The individual shaders are now baked into the program; we can delete them.
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    // Tell the "tex0" sampler to read from texture unit 0 (set once).
    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "tex0"), 0);

    // Stage 3: bind-pose palette is all-identity, so skinning is a no-op.
    // (Stage 4 replaces this with a per-frame Aⱼ·Bⱼ⁻¹ palette.)
    boneMatricesLoc = glGetUniformLocation(shaderProgram, "boneMatrices");
    std::vector<glm::mat4> identity(MAX_BONES, glm::mat4(1.0f));
    glUniformMatrix4fv(boneMatricesLoc, MAX_BONES, GL_FALSE, glm::value_ptr(identity[0]));
}