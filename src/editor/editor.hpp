#pragma once

#include "editor_graphics.hpp"

using json = nlohmann::json;



// Initializes camera - values will be written over.
Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

std::vector<Object*> parents;
std::vector<UIElement*> uiElements;
std::vector<std::vector<Object>> oldParents;
Object* curObject = nullptr;
Object* copiedObject = nullptr;
float* curElement = nullptr;
float editMultiplier = 1.0f;
bool rightHeld = false;
bool ctrlHeld = false;
const int HIERARCHYMARGIN = 3;


static void deleteSubtree(Object* obj)
{
    for (Object* child : obj->children) deleteSubtree(child);
    delete obj;
}


void removeObject(Object*& obj)
{
    std::vector<Object*>& siblings = obj->parent ? obj->parent->children : parents;
    auto it = std::find(siblings.begin(), siblings.end(), obj);
    if (it != siblings.end()) siblings.erase(it);
    deleteSubtree(obj);
    obj = nullptr;
}


// Deep-clones one node (not its siblings). Reconstructs the concrete subtype
// via copy construction so type-specific fields (color/intensity/radius,
// objSrc/texSrc/isStatic, FOV...) come along, then gives the clone its own
// independent GL handles — sharing a VAO/texture with the original would
// double-free it the first time either copy is deleted. `children` is cloned
// recursively rather than copied, since the compiler-generated copy
// constructor would otherwise alias the original's child Object* pointers.
static Object* cloneNode(Object* src, Object* parent)
{
    Object* clone = nullptr;

    if (LightMesh* light = dynamic_cast<LightMesh*>(src))
    {
        LightMesh* c = new LightMesh(*light);
        c->VAO = c->VBO = c->EBO = 0;
        c->texture = loadTexture("assets/engine_assets/light/light.png");
        clone = c;
    }
    else if (CameraMesh* cam = dynamic_cast<CameraMesh*>(src))
    {
        CameraMesh* c = new CameraMesh(*cam);
        c->VAO = c->VBO = c->EBO = 0;
        c->texture = loadTexture("assets/engine_assets/camera/camera.png");
        clone = c;
    }
    else if (ObjMesh* obj = dynamic_cast<ObjMesh*>(src))
    {
        ObjMesh* c = new ObjMesh(*obj);
        c->VAO = c->VBO = c->EBO = 0;
        c->texture = loadTexture(obj->texSrc.c_str());
        clone = c;
    }
    else if (FbxMesh* fbx = dynamic_cast<FbxMesh*>(src))
    {
        FbxMesh* c = new FbxMesh(*fbx);
        c->VAO = c->VBO = c->EBO = 0;
        c->texture = loadTexture(fbx->texSrc.c_str());
        clone = c;
    }
    else if (Mesh* mesh = dynamic_cast<Mesh*>(src)) // pivot
    {
        Mesh* c = new Mesh(*mesh);
        c->VAO = c->VBO = c->EBO = 0;
        c->texture = loadTexture("assets/engine_assets/pivot/pivot.png");
        clone = c;
    }
    else
    {
        clone = new Object(*src);
    }

    clone->parent = parent;
    clone->children.clear();
    for (Object* child : src->children)
        clone->children.push_back(cloneNode(child, clone));

    return clone;
}


// Clones `copied`'s subtree and inserts it as a sibling immediately after
// `current`, in whichever list `current` actually lives in (its parent's
// `children`, or `parents` for a root/light).
void copyObject(Object*& copied, Object*& current)
{
    if (!copied || !current) return;

    std::vector<Object*>& siblings = current->parent ? current->parent->children : parents;
    auto it = std::find(siblings.begin(), siblings.end(), current);
    if (it == siblings.end()) return;

    Object* clone = cloneNode(copied, current->parent);
    siblings.insert(it + 1, clone);
    clone->Upload();
}


// Initializes one object (and its subtree) from a scene.json entry. Returns the
// node so the caller can parent it; null means no node was produced — either the
// entry was a light, or its type was unrecognised.
//
// Nodes are heap-allocated because `parents`/`children` hold Object*, so they
// have to outlive this call.
static Object* buildNode(const json& j, Object* parent)
{
    const std::string type = j.value("type", "object");
    const std::string name = j.value("name", "");

    if (type == "light")
    {
        const json& p = j.at("position");
        const json& c = j.at("color");

        LightMesh* light = new LightMesh(
            makeLightMesh(
                "assets/engine_assets/light/light.obj",
                "assets/engine_assets/light/light.png",
                Transform{p.at(0).get<float>(), p.at(1).get<float>(),
                p.at(2).get<float>(), 0.0f, 0.0f, 1.0f},
                glm::vec3{c.at(0).get<float>(), c.at(1).get<float>(), c.at(2).get<float>()},
                j.at("intensity").get<float>(), j.at("radius").get<float>()
            )
        );

        light->name = name;
        light->type = type;
                            
        parents.push_back(light);
        return nullptr; // a light isn't a scene node — it never enters the graph
    }

    Object* node = nullptr;

    Transform transform{};
    if (j.contains("transform"))
    {
        const json& t = j["transform"]; // x, y, z, yaw, pitch, scale
        transform = Transform{t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>(),
                              t.at(3).get<float>(), t.at(4).get<float>(), t.at(5).get<float>(),
                              t.at(6).get<float>()};
    }

    if (type == "mesh-obj")
    {
        // C++17 guarantees the returned Mesh is constructed straight into the
        // allocation. Without that elision the temporary's destructor would run
        // and free the GL handles the stored copy still points at.
        node = new ObjMesh(
            makeObj(
                j.at("obj src").get<std::string>().c_str(),
                j.at("tex src").get<std::string>().c_str(),
                transform, j.value("isStatic", false),
                j.value("collides", false)
            )
        );
    }
    else if (type == "mesh-fbx")
    {
        node = new FbxMesh(
            makeFbx(
                j.at("obj src").get<std::string>().c_str(),
                j.at("tex src").get<std::string>().c_str(),
                transform, j.value("isStatic", false)
            )
        );
    }
    else if (type == "camera")
    {
        node = new CameraMesh(
            makeCameraMesh(
                "assets/engine_assets/camera/camera.obj",
                "assets/engine_assets/camera/camera.png",
                transform, j.at("fov").get<float>()
            )
        );
    }
    else if (type == "pivot")
    {
        node = new Mesh(makeMesh("assets/engine_assets/pivot/pivot.obj",
                                 "assets/engine_assets/pivot/pivot.png",
                                 transform));
    }
    else if (type == "player")
    {
        // The player stores its spawn as a 3-float "position" rather than a full
        // "transform", so fill in the local that the tail block below stamps onto
        // the node — assigning the node directly here would just be overwritten.
        const json& p = j.at("position");
        transform = Transform{p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>(),
                              j.at("yaw").get<float>(), 0.0f, 0.0f, 1.0f};

        node = new Mesh(
            makeMesh(
                "assets/engine_assets/player/player.obj",
                "assets/engine_assets/player/player.png",
                transform
            )
        );
    }
    else
    {
        fprintf(stderr, "importScene: node '%s' has unknown type '%s', skipped\n",
                name.c_str(), type.c_str());
        return nullptr;
    }
    
    node->name = name;
    node->type = type;
    node->parent = parent;
    node->transform = transform;
    node->world = transform.matrix();

    for (const json& child : j.value("children", json::array()))
    {
        Object* c = buildNode(child, node);
        if (c) node->children.push_back(c);
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
            Object* obj = buildNode(node, nullptr);
            if (obj) parents.push_back(obj);
        }
    }
    catch(const json::exception& e)
    {
        // Bad key, bad type, malformed file — all land here. Leaving `parents`
        // partly built is fine: whatever loaded still draws.
        fprintf(stderr, "importScene: '%s': %s\n", path, e.what());
    }
}


// Inverse of buildNode(): buildNode packs every field that isn't a live
// transform into `name` (space-separated: type, display name, then the
// type's fixed fields) purely so it can be recovered here. Splitting on
// whitespace and taking the last K tokens as the fixed fields — with
// whatever sits between the type and them as the display name — undoes
// that packing.
static json objectToJson(Object* node)
{
    json j;

    if (LightMesh* light = dynamic_cast<LightMesh*>(node))
    {
        j["name"]      = light->name;
        j["position"]  = { light->transform.x, light->transform.y, light->transform.z };
        j["color"]     = { light->color.x, light->color.y, light->color.z };
        j["intensity"] = light->intensity;
        j["radius"]    = light->radius;
        j["type"]      = "light";
        return j; // lights carry no transform/children in the scene format
    }

    if (node->type == "player")
    {
        j["name"]      = node->name;
        j["position"]  = { node->transform.x, node->transform.y, node->transform.z };
        j["yaw"]       = node->transform.yaw;
        j["type"]      = "player";

        j["children"] = json::array();
        for (Object* child : node->children)
            j["children"].push_back(objectToJson(child));

        return j;
    }

    j["transform"] = { node->transform.x, node->transform.y, node->transform.z,
                       node->transform.yaw, node->transform.pitch, node->transform.roll,
                       node->transform.scale };

    if (ObjMesh* obj = dynamic_cast<ObjMesh*>(node))
    {
        j["name"]      = obj->name;
        j["obj src"]   = obj->objSrc;
        j["tex src"]   = obj->texSrc;
        j["isStatic"]  = obj->isStatic;
        j["collides"]  = obj->collides;
        j["type"]      = "mesh-obj";
    }
    else if (FbxMesh* fbx = dynamic_cast<FbxMesh*>(node))
    {
        j["name"]      = fbx->name;
        j["obj src"]   = fbx->objSrc;
        j["tex src"]   = fbx->texSrc;
        j["isStatic"]  = fbx->isStatic;
        j["type"]      = "mesh-fbx";
    }
    else if (CameraMesh* cam = dynamic_cast<CameraMesh*>(node))
    {
        j["name"] = cam->name;
        j["fov"]  = cam->FOV;
        j["type"] = "camera";
    }
    else if (node->type == "player") {}
    else // pivot, or anything else with no packed fields
    {
        j["name"] = node->name;
        j["type"] = "pivot";
    }

    j["children"] = json::array();
    for (Object* child : node->children)
        j["children"].push_back(objectToJson(child));

    return j;
}


void exportScene(const char* path)
{
    json scene;
    scene["scene"] = json::array();

    for (Object* node : parents)
    {
        // The live fly-camera sits in `parents` for Compose()/raycast bookkeeping
        // only — it isn't scene data (the "camera" *type* above is a placed gizmo
        // built by buildNode, a different object entirely).
        if (node == &camera) continue;

        scene["scene"].push_back(objectToJson(node));
    }

    std::ofstream file(path);
    if (!file)
    {
        fprintf(stderr, "exportScene: could not open '%s' for writing\n", path);
        return;
    }
    file << scene.dump(4);
}


static void appendName(std::string& hierarchy, int margin, Object*& obj)
{
    for (int i = 0; i < margin; i++) hierarchy += " ";
    hierarchy = hierarchy + obj->name + '\n';

    for (Object*& child : obj->children)
    {
        appendName(hierarchy, margin + HIERARCHYMARGIN, child);
    }
}


std::string buildHierarchyString()
{
    std::string hierarchy = "Scene Hierarchy:\n\n";
    for (Object*& obj : parents)
    {
        appendName(hierarchy, 0, obj);
    }

    return hierarchy;
}


std::string buildCurObjectString()
{   
    std::string label;
    if (curObject)
    {
        label = "Selected: " + curObject->name
                + "\nType: " + curObject->type
             + "\n\nX: " + std::to_string(curObject->transform.x) + "\n"
                 + "Y: " + std::to_string(curObject->transform.y) + "\n"
                 + "Z: " + std::to_string(curObject->transform.z) + "\n\n";
        if (curObject->type == "light")
        {
            LightMesh* light = static_cast<LightMesh*>(curObject);

            if (light->color.r > 1.0f) light->color.r = 1.0f;
            if (light->color.g > 1.0f) light->color.g = 1.0f;
            if (light->color.b > 1.0f) light->color.b = 1.0f;
            if (light->color.r < 0.0f) light->color.r = 0.0f;
            if (light->color.g < 0.0f) light->color.g = 0.0f;
            if (light->color.b < 0.0f) light->color.b = 0.0f;
                
            label +="R: " + std::to_string(light->color.r * 255) + "\n"
                  + "G: " + std::to_string(light->color.g * 255) + "\n"
                  + "B: " + std::to_string(light->color.b * 255) + "\n\n"
                  + "Intensity: " + std::to_string(light->intensity) + "\n"
                  + "Radius: " + std::to_string(light->radius);

            return label;
        } else {
            label +=  "Yaw: " + std::to_string(curObject->transform.yaw) + "\n"
                  + "Pitch: " + std::to_string(curObject->transform.pitch) + "\n"
                  +  "Roll: " + std::to_string(curObject->transform.roll) + "\n\n"
                  + "Scale: " + std::to_string(curObject->transform.scale) + "\n\n";
        }

        if (curObject->type == "mesh-obj")
        {
            ObjMesh* obj = static_cast<ObjMesh*>(curObject);
            label += "IsStatic: " + std::to_string(obj->isStatic) + "\n"
                  +  "Collides: " + std::to_string(obj->collides);
        }
        
        else if (curObject->type == "mesh-fbx")
        {
            FbxMesh* fbx = static_cast<FbxMesh*>(curObject);

            label += "IsStatic: " + std::to_string(fbx->isStatic) + "\n\n";
            label += "Animations: { ";
            for (Animation& anim : fbx->animations)
            {
                label += anim.name + " ";
            }
            label += "}";
        }
    } else {
        label = "Selected: none";
    }

    return label;
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