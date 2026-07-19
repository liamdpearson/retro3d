#include "graphics.h"

Camera camera{90.0f, glm::vec3(0.0f, 0.0f, 3.0f),
              glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)};

std::vector<Object*> parents;
std::vector<Light*> lights;


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_H && action == GLFW_PRESS) {
        std::cout << camera.pos.x << ' ' << camera.pos.y << ' ' << camera.pos.z << '\n';
    }
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

        gun.transform.yaw += 0.1;

        // Compose first: this refreshes every `world` in the graph and lets the
        // camera derive its pos/front, which clearBG() reads to build `view`.
        // Draw only after both, or the view matrix lags the scene by a frame.
        for (Object*& obj : parents)
        {
            obj->world = obj->transform.matrix();
            obj->Compose();
        }

        clearBG(0.10f, 0.12f, 0.15f, 1.0f); // r, g, b, a

        for (Object*& obj : parents) obj->Draw();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}   