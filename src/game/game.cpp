#include "game.h"

const float sensitivity = 0.05f;
float lightAmbient = 0.3f;


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && player.grounded) {
        player.velocity.y += 5.0f;
    }
    else if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
        Mesh* knight = static_cast<Mesh*>(parents[1]);
        knight->SetAnimation(0, 0.5f, 1);
    }
    else if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
        Mesh* knight = static_cast<Mesh*>(parents[1]);
        knight->SetAnimation(1, 0.5f, 0);
    }
}


void mouseCallback(GLFWwindow*, double xpos, double ypos)
{
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }

    float xoffset = (float)(xpos - lastX);
    float yoffset = (float)(lastY - ypos);
    lastX = float(xpos);
    lastY = float(ypos);

    player.transform.yaw -= xoffset * sensitivity;
    camera.transform.pitch -= yoffset * sensitivity;

    if (camera.transform.pitch > 89.0f) camera.transform.pitch = 89.0f;
    if (camera.transform.pitch < -89.0f) camera.transform.pitch = -89.0f;
}


int main()
{
    initWindow();
    buildShaderProgram();
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouseCallback);



    importScene("scene.json");


    
    // --- init ui --- //
    // src, x, y, scale
    UIElement crosshair = makeUIElement("assets/crosshair.png", SW/2, SH/2, 0.05f);
    uiElements.push_back(&crosshair);

    Font uiFont = bakeFont("assets/arial.ttf", 48.0f);
    UIText fpsLabel{ {20.0f, 20.0f}, 32.0f, "", &uiFont, {1,1,1} };
    uploadUIText(fpsLabel);



    bakeSceneLighting();
    collectSceneColliders();



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

        float acceleration = 30.0f * deltaTime; // deltaTime keeps speed steady regardless of FPS

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            player.velocity.x -= acceleration * sin(glm::radians(player.transform.yaw));
            player.velocity.z -= acceleration * cos(glm::radians(player.transform.yaw));
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            player.velocity.x += acceleration * sin(glm::radians(player.transform.yaw));
            player.velocity.z += acceleration * cos(glm::radians(player.transform.yaw));
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            player.velocity.x += acceleration * sin(glm::radians(player.transform.yaw - 90));
            player.velocity.z += acceleration * cos(glm::radians(player.transform.yaw - 90));
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            player.velocity.x += acceleration * sin(glm::radians(player.transform.yaw + 90));
            player.velocity.z += acceleration * cos(glm::radians(player.transform.yaw + 90));
        }


        player.velocity.y -= 9.81f * deltaTime;
        if (player.velocity.y < -50.0f) player.velocity.y = -50.0f;

        float damping = powf(0.005f, deltaTime);
        player.velocity.x *= damping;
        player.velocity.z *= damping;
        
        // Split this frame's motion into steps no longer than half the capsule's
        // radius. resolvePlayerCollision() only fixes an overlap that still
        // exists when it runs, so a step big enough to clear a surface entirely
        // lands past it with nothing left to detect — the classic way to fall
        // through a floor after building up speed.
        glm::vec3 step = player.velocity * deltaTime;
        int substeps = glm::max(1, (int)glm::ceil(glm::length(step) / (player.radius * 0.5f)));

        // resolvePlayerCollision() clears `grounded` on entry, so the last
        // substep would otherwise be the only one that could report it — and it
        // usually can't, since the previous substep pushed the capsule out to
        // exactly contact distance. Accumulate across the whole move instead.
        bool groundedAny = false;

        for (int i = 0; i < substeps; i++)
        {
            player.transform.x += step.x / substeps;
            player.transform.y += step.y / substeps;
            player.transform.z += step.z / substeps;

            // After each move, before Compose: the camera hangs off the player,
            // so correcting the position here means the view follows it this frame.
            resolvePlayerCollision(player);
            if (player.grounded) groundedAny = true;
        }

        player.grounded = groundedAny;

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

        fpsLabel.text = "FPS: " + std::to_string(int(1/deltaTime));

        if (player.grounded) fpsLabel.text += "\ngrounded";

        drawText(fpsLabel);

        endUI();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}   