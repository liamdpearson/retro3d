#pragma once

#include "graphics/graphics.h"
#include "lighting/lighting.h"
#include "collisions/collisions.h"

using json = nlohmann::json;


// Initializes camera - values will be written over.
Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

Player player{};

std::vector<Object*> parents;
std::vector<Light*> lights;
std::vector<UIElement*> uiElements;


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

        Light* light = new Light{glm::vec3{p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>()},
                                 glm::vec3{c.at(0).get<float>(), c.at(1).get<float>(), c.at(2).get<float>()},
                                 j.at("intensity").get<float>(), j.at("radius").get<float>()};
        
        lights.push_back(light);
        return nullptr; // a light isn't a scene node — it never enters the graph
    }
    
    Transform transform{};
    if (j.contains("transform"))
    {
        const json& t = j["transform"]; // x, y, z, yaw, pitch, scale
        transform = Transform{t.at(0).get<float>(), t.at(1).get<float>(), t.at(2).get<float>(),
                              t.at(3).get<float>(), t.at(4).get<float>(), t.at(5).get<float>(),
                              t.at(6).get<float>()};
    }

    Object* node = nullptr;

    if (type == "mesh-obj")
    {
        // C++17 guarantees the returned Mesh is constructed straight into the
        // allocation. Without that elision the temporary's destructor would run
        // and free the GL handles the stored copy still points at.
        node = new Mesh(
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
        node = new Mesh(
            makeFbx(
                j.at("obj src").get<std::string>().c_str(),
                j.at("tex src").get<std::string>().c_str(),
                transform
            )
        );
    }
    else if (type == "camera")
    {
        // The one node we don't allocate: graphics.cpp draws through the global
        // `camera`, so the scene file only places it. Returning it here still
        // gets it into `parents`, which Compose() requires — see Camera::Compose.
        camera.FOV = j.value("fov", camera.FOV);
        camera.transform = transform;
        node = &camera;
    }
    else if (type == "pivot")
    {
        node = new Object(transform); // a bare pivot / attachment point
    }
    else if (type == "player")
    {
        // The player stores its spawn as a 3-float "position" rather than a full
        // "transform", so fill in the local that the tail block below stamps onto
        // the node — assigning player.transform here would just be overwritten.
        const json& p = j.at("position");
        transform = Transform{p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>(),
                              j.at("yaw").get<float>(), 0.0f, 0.0f, 1.0f};
        node = &player;
    }
    else
    {
        fprintf(stderr, "importScene: node '%s' has unknown type '%s', skipped\n",
                name.c_str(), type.c_str());
        return nullptr;
    }

    node->name = name;
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