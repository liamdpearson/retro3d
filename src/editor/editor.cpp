#include "editor.hpp"

using json = nlohmann::json;


static void handleLightInput(int key, LightMesh*& light) {
    switch (key)
    {
        case GLFW_KEY_1:
            curElement = &light->transform.x;
            editMultiplier = 0.05f;
            return;
        
        case GLFW_KEY_2:
            curElement = &light->transform.y;
            editMultiplier = 0.05f;
            return;
        
        case GLFW_KEY_3:
            curElement = &light->transform.z;
            editMultiplier = 0.05f;
            return;

        case GLFW_KEY_4:
            curElement = &light->color.r;
            editMultiplier = 0.0015f;
            return;
        
        case GLFW_KEY_5:
            curElement = &light->color.g;
            editMultiplier = 0.0015f;
            return;
        
        case GLFW_KEY_6:
            curElement = &light->color.b;
            editMultiplier = 0.0015f;
            return;
        
        case GLFW_KEY_7:
            curElement = &light->intensity;
            editMultiplier = 0.05f;
            return;

        case GLFW_KEY_8:
            curElement = &light->radius;
            editMultiplier = 0.05f;
            return;
    }
}


static void handleOtherInput(int key) {
    switch (key)
    {
        case GLFW_KEY_1:
            curElement = &curObject->transform.x;
            editMultiplier = 0.05f;
            return;
        
        case GLFW_KEY_2:
            curElement = &curObject->transform.y;
            editMultiplier = 0.05f;
            return;
        
        case GLFW_KEY_3:
            curElement = &curObject->transform.z;
            editMultiplier = 0.05f;
            return;

        case GLFW_KEY_4:
            curElement = &curObject->transform.yaw;
            editMultiplier = 1.0f;
            return;
        
        case GLFW_KEY_5:
            curElement = &curObject->transform.pitch;
            editMultiplier = 1.0f;
            return;
        
        case GLFW_KEY_6:
            curElement = &curObject->transform.roll;
            editMultiplier = 1.0f;
            return;
        
        case GLFW_KEY_7:
            curElement = &curObject->transform.scale;
            editMultiplier = 0.05f;
            return;
    }
}



void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS) return;

    if (ctrlHeld) {
        switch (key)
        {
            case GLFW_KEY_S:
                exportScene("scene.json");
                return;

            case GLFW_KEY_C:
                copiedObject = curObject;
                return;

            case GLFW_KEY_V:
                addObject(copiedObject, curObject);
                return;
        }
        
    }

    

    if (curObject)
    {
        if (key == GLFW_KEY_DELETE) {
            removeObject(curObject);
            return;
        }

        if (curObject->isLight()) {
            LightMesh* light = static_cast<LightMesh*>(curObject);
            handleLightInput(key, light);
        } else {
            handleOtherInput(key);
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

    Font uiFont = bakeFont("assets/terminal.ttf", 48.0f);

    UIText hierarchyLabel{ {20.0f, 20.0f}, 32.0f, "", &uiFont, {1,1,1}, true };
    uploadUIText(hierarchyLabel);

    UIText curObjectLabel{ {SW - 20.0f, 20.0f}, 32.0f, "", &uiFont, {1,1,1}, false };
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
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS && !ctrlHeld) {
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

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            curObject = raycast(camera.pos, camera.front, 100.0f);
            curElement = nullptr;
        }
        rightHeld = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                     ? true : false;

    


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

        beginUI();

        for (UIElement*& ui : uiElements) drawUIElement(*ui);

        if (curObject)
        {
            curObjectLabel.text = "Selected: " + curObject->name
                              + "\nType: " + curObject->type
                            + "\n\nX: " + std::to_string(curObject->transform.x) + "\n"
                                + "Y: " + std::to_string(curObject->transform.y) + "\n"
                                + "Z: " + std::to_string(curObject->transform.z) + "\n\n";
            if (curObject->isLight())
            {
                LightMesh* light = static_cast<LightMesh*>(curObject);

                if (light->color.r > 1.0f) light->color.r = 1.0f;
                if (light->color.g > 1.0f) light->color.g = 1.0f;
                if (light->color.b > 1.0f) light->color.b = 1.0f;
                if (light->color.r < 0.0f) light->color.r = 0.0f;
                if (light->color.g < 0.0f) light->color.g = 0.0f;
                if (light->color.b < 0.0f) light->color.b = 0.0f;
                
                curObjectLabel.text +="R: " + std::to_string(light->color.r) + "\n"
                                    + "G: " + std::to_string(light->color.g) + "\n"
                                    + "B: " + std::to_string(light->color.b) + "\n\n"
                                    + "Intensity: " + std::to_string(light->intensity) + "\n"
                                    + "Radius: " + std::to_string(light->radius);
            } else {
                curObjectLabel.text +=  "Yaw: " + std::to_string(curObject->transform.yaw) + "\n"
                                    + "Pitch: " + std::to_string(curObject->transform.pitch) + "\n"
                                    +  "Roll: " + std::to_string(curObject->transform.roll) + "\n\n"
                                    + "Scale: " + std::to_string(curObject->transform.scale);
            }                  
        } else {
            curObjectLabel.text = "Selected: none";
        }
        drawText(curObjectLabel);

        hierarchyLabel.text = buildHierarchyString();
        drawText(hierarchyLabel);

        endUI();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}   