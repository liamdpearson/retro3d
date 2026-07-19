#include "graphics.h"

Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 3.0f),
              glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

std::vector<Object*> parents;
std::vector<Light*> lights;
bool paused = false;
bool editing = true;


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        paused = !paused;
    }
    else if (key == GLFW_KEY_H && action == GLFW_PRESS) {
        std::cout << camera.pos.x << ' ' << camera.pos.y << ' ' << camera.pos.z << '\n';
    }
}


void update()
{

}

void removeChild(std::vector<Object*>& children, const Object* child)
{
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}


int main()
{
    initWindow();
    buildShaderProgram();
    glfwSetKeyCallback(window, key_callback);

    // --- init camera transform --- //
    camera.transform.z += 0.1;

    // --- init objects --- //
    // 3d obj src, tex src, transform, static boolean
    Mesh gun = makeObj("assets/gun/gun.obj", "assets/gun/gun.png",
                       Transform{0, 0, -1,  0, 0, 1.0f}, false);

    Mesh level0 = makeObj("assets/level0/level0.obj", "assets/level0/level0.jpg",
                          Transform{0, 0, 0,  0, 0, 1.0f}, true);

    // removeChild(gun.children, &skull);
    parents.push_back(&level0);
    parents.push_back(&camera);
    camera.children.push_back(&gun);

    
    // --- init lights --- //
    // pos, color, intensity, radius
    Light light = Light{glm::vec3{0.0f, 5.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}, 5.0f, 15.0f};

    lights.push_back(&light);


    bakeSceneLighting();
    for (Object*& obj : parents) obj->Upload();
        
    // --- render loop --- //
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        float speed = 7.5f * deltaTime; // deltaTime keeps speed steady regardless of FPS
        

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        
        clearBG(0.10f, 0.12f, 0.15f, 1.0f); // r, g, b, a

        for (Object*& obj : parents)
        {
            obj->world = obj->transform.matrix();
            obj->Draw();
        }

        if (editing)
        {
            // if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.pos += speed * camera.front;
            // if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.pos -= speed * camera.front;
            // if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            //     camera.pos -= glm::normalize(glm::cross(camera.front, camera.up)) * speed;
            // if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            //     camera.pos += glm::normalize(glm::cross(camera.front, camera.up)) * speed;
            // if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)        camera.pos += speed * camera.up;
            // if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)   camera.pos -= speed * camera.up;
        } else {
            // apply game movement here
            // apply camera filters here
            if (!paused) update();
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}   