#include "editor.hpp"

using json = nlohmann::json;


static void removeChild(std::vector<Object*>& children, const Object* child)
{
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
        curElement = &(curObject->transform.x);
        editMultiplier = 0.05f;
    }
    else if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
        curElement = &(curObject->transform.y);
        editMultiplier = 0.05f;
    }
    else if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
        curElement = &(curObject->transform.z);
        editMultiplier = 0.05f;
    }
    else if (key == GLFW_KEY_Y && action == GLFW_PRESS) {
        curElement = &(curObject->transform.yaw);
        editMultiplier = 1.0f;
    }
    else if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        curElement = &(curObject->transform.pitch);
        editMultiplier = 1.0f;
    }
    else if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        curElement = &(curObject->transform.roll);
        editMultiplier = 1.0f;
    }
    else if (key == GLFW_KEY_S && action == GLFW_PRESS) {
        if (ctrlHeld) {
            exportScene("scene.json");
        } else {
            curElement = &(curObject->transform.scale);
            editMultiplier = 0.1f;
        }
    }
}


int main()
{
    initWindow();
    buildShaderProgram();
    glfwSetKeyCallback(window, key_callback);

    importScene("scene.json");
    parents.push_back(&camera);
    
    // --- init ui elements --- //
    // src, x, y, scale
    UIElement crosshair = makeUIElement("assets/crosshair.png", SW/2, SH/2, 0.05f);
    uiElements.push_back(&crosshair);

    Font uiFont = bakeFont("assets/arial.ttf", 48.0f);
    UIText curObjectLabel{ {20.0f, 20.0f}, 32.0f, "", &uiFont, {1,1,1} };
    uploadUIText(curObjectLabel);

    for (Object*& obj : parents) obj->Upload();
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
        
        ctrlHeld = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ? true : false;

        // Compose first: this refreshes every `world` in the graph and lets the
        // camera derive its pos/front, which clearBG() reads to build `view`.
        // Draw only after both, or the view matrix lags the scene by a frame.
        for (Object*& obj : parents)
        {
            obj->world = obj->transform.matrix();
            obj->Compose();
        }

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            curObject = raycast(camera.pos, camera.front, 100.0f);
            curElement = nullptr;
        }
        rightHeld = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                     ? true : false;


        clearBG(0.10f, 0.12f, 0.15f, 1.0f); // r, g, b, a

        for (Object*& obj : parents) obj->Draw();

        beginUI();

        for (UIElement*& ui : uiElements) drawUIElement(*ui);

        if (curObject)
        {
            curObjectLabel.text = "Selected: " + curObject->name + '\n'
                                + "X: " + std::to_string(curObject->transform.x) + '\n'
                                + "Y: " + std::to_string(curObject->transform.y) + '\n'
                                + "Z: " + std::to_string(curObject->transform.z) + '\n'
                                + "Yaw: " + std::to_string(curObject->transform.yaw) + '\n'
                                + "Pitch: " + std::to_string(curObject->transform.pitch) + '\n'
                                + "Scale: " + std::to_string(curObject->transform.scale);
        } else {
            curObjectLabel.text = "Selected: none";
        }
        drawText(curObjectLabel);

        endUI();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}   