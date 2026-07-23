#include "graphics.h"

using json = nlohmann::json;

// Initializes camera - values will be written over.
Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

std::unordered_map<std::string, Object*> parents;
std::vector<Light*> lights;
std::vector<UIElement*> uiElements;
Object* currently_selected;
float* cur_element = nullptr;


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.x);
    }
    else if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.y);
    }
    else if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.z);
    }
    else if (key == GLFW_KEY_4 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.yaw);
    }
    else if (key == GLFW_KEY_5 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.pitch);
    }
    else if (key == GLFW_KEY_6 && action == GLFW_PRESS) {
        cur_element = &(currently_selected->transform.scale);
    }

}


void removeChild(std::vector<Object*>& children, const Object* child)
{
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}


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


int main()
{
    initWindow();
    buildShaderProgram();
    glfwSetKeyCallback(window, key_callback);

    importScene("scene.json");
    parents.insert({"editor_cam", &camera});
    
    // --- init ui elements --- //
    // src, x, y, scale
    UIElement crosshair = makeUIElement("assets/crosshair.png", SW/2, SH/2, 0.05f);
    uiElements.push_back(&crosshair);

    for (const auto& [name, obj] : parents) obj->Upload();
    for (UIElement*& ui : uiElements) uploadUIElement(*ui);
        
    // --- render loop --- //
    while (!glfwWindowShouldClose(window))
    {
        // Polled at the top so this frame acts on this frame's input. Polling
        // after the draw meant the input block below ran on key state gathered
        // at the end of the previous iteration.
        glfwPollEvents();

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        float speed = 7.5f * deltaTime; // deltaTime keeps speed steady regardless of FPS
        

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            camera.transform.x -= speed * sin(glm::radians(camera.transform.yaw));
            camera.transform.z -= speed * cos(glm::radians(camera.transform.yaw));
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            camera.transform.x += speed * sin(glm::radians(camera.transform.yaw));
            camera.transform.z += speed * cos(glm::radians(camera.transform.yaw));
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            camera.transform.x += speed * sin(glm::radians(camera.transform.yaw - 90));
            camera.transform.z += speed * cos(glm::radians(camera.transform.yaw - 90));
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            camera.transform.x += speed * sin(glm::radians(camera.transform.yaw + 90));
            camera.transform.z += speed * cos(glm::radians(camera.transform.yaw + 90));
        }
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        camera.transform.y += speed;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)   camera.transform.y -= speed;

        if (cur_element) {
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) *cur_element += 0.1f;
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) *cur_element -= 0.1f;
        }

        // Compose first: this refreshes every `world` in the graph and lets the
        // camera derive its pos/front, which clearBG() reads to build `view`.
        // Draw only after both, or the view matrix lags the scene by a frame.
        for (const auto& [name, obj] : parents)
        {
            obj->world = obj->transform.matrix();
            obj->Compose();
        }

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            currently_selected = raycast(camera.pos, camera.front, 100.0f);
            cur_element = nullptr;
        }
        clearBG(0.10f, 0.12f, 0.15f, 1.0f); // r, g, b, a

        for (const auto& [name, obj] : parents) obj->Draw();

        beginUI();
        for (UIElement*& ui : uiElements) drawUIElement(*ui);
        endUI();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}   