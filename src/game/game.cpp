#include "game.h"


const float sensitivity = 0.05f;
float lightAmbient = 0.3f;

std::vector<int> keys_pressed;
std::vector<int> mouse_buttons_pressed;

std::vector<int> keys_released;
std::vector<int> mouse_buttons_released;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS) keys_pressed.push_back(key);
    if (action == GLFW_RELEASE) keys_released.push_back(key);
}

bool keyPressed(int key)
{
    return std::find(keys_pressed.begin(), keys_pressed.end(), key) != keys_pressed.end();
}

bool keyReleased(int key)
{
    return std::find(keys_released.begin(), keys_released.end(), key) != keys_released.end();
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (action == GLFW_PRESS) mouse_buttons_pressed.push_back(button);
    if (action == GLFW_RELEASE) mouse_buttons_released.push_back(button);
}

bool mouseButtonPressed(int button)
{
    return std::find(mouse_buttons_pressed.begin(), mouse_buttons_pressed.end(), button) != mouse_buttons_pressed.end();
}

bool mouseButtonReleased(int button)
{
    return std::find(mouse_buttons_released.begin(), mouse_buttons_released.end(), button) != mouse_buttons_released.end();
}

void mouseMoveCallback(GLFWwindow*, double xpos, double ypos)
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

void movementLogic()
{
    float acceleration = 45.0f * deltaTime; // deltaTime keeps speed steady regardless of FPS

    glm::vec3 moveDir = glm::vec3(0.0f);

    glm::vec3 forward = glm::vec3(
        -sin(glm::radians(player.transform.yaw)),
        0.0f,
        -cos(glm::radians(player.transform.yaw))
    );

    glm::vec3 right = glm::vec3(
        -sin(glm::radians(player.transform.yaw + 90.0f)),
        0.0f,
        -cos(glm::radians(player.transform.yaw + 90.0f))
    );
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        moveDir += forward;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        moveDir -= forward;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        moveDir += right;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        moveDir -= right;
    }
    
    if (glm::length(moveDir) > 0.0f)
        moveDir = glm::normalize(moveDir);

    player.velocity.y -= 9.81f * deltaTime;
    if (player.velocity.y < -50.0f) player.velocity.y = -50.0f;

    float damping = powf(0.005f, deltaTime);
    player.velocity.x *= damping;
    player.velocity.z *= damping;
    player.velocity += moveDir * acceleration;
}


int main()
{
    initWindow();
    initAudio(); // not fatal if it fails — the game just runs silent
    buildShaderProgram();
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseMoveCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);



    importScene("scene.json");

    Object* a = findFirst("gun", parents);

    if (!a)
        std::cout << "couldn't find gun\n";

    Mesh* gun = dynamic_cast<Mesh*>(a);

    if (!gun)
        std::cout << "'gun' is not a Mesh\n";
    
    gun->SetAnimation(0);

    Object* b = findFirst("wasd", parents);

    if (!b)
        std::cout << "couldn't find sound\n";

    AudioSource* sound = dynamic_cast<AudioSource*>(b);

    if (!gun)
        std::cout << "'sound' is not an AudioSource\n";
    
    sound->Play();

    Object* c = findFirst("entity", parents);

    if (!c)
        std::cout << "couldn't find entity\n";

    Entity* entity = dynamic_cast<Entity*>(c);

    if (!entity)
        std::cout << "'entity' is not a Entity\n";


    // --- init ui --- //
    // src, x, y, scale
    UIElement crosshair = makeUIElement("assets/ui/crosshair.png", SW/2, SH/2, 0.05f);
    uiElements.push_back(&crosshair);

    Font uiFont = bakeFont("assets/fonts/arial.ttf", 48.0f);
    UIText fpsLabel{ {20.0f, 20.0f}, 32.0f, "", &uiFont, {1,1,1} };
    uploadUIText(fpsLabel);

    
    bakeSceneLighting();
    collectSceneColliders();

    for (Object*& obj : parents) obj->Upload();
    for (UIElement*& ui : uiElements) uploadUIElement(*ui);
        
    // --- render loop --- //
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (mouseButtonPressed(GLFW_MOUSE_BUTTON_2))
        {
            gun->SetAnimation(1, 0.03f, 0);
            playSound2D("assets/sfx/shot.wav", 0.75f);
            player.velocity -= camera.front * 5.0f;

            float entityDist = glm::distance(camera.pos,
                               glm::vec3{entity->transform.x, entity->transform.y, entity->transform.z});
            if (rayHitEntity(camera.pos, camera.front, *entity, entityDist))
            {
                if (!rayBlockedA(camera.pos, camera.front, entityDist, colliders))
                {
                    entity->velocity.y += 3.0f;
                }
            }
        }
        
        if (keyPressed(GLFW_KEY_SPACE) && player.grounded)
            player.velocity.y += 5.0f;

        mouse_buttons_pressed = {};
        keys_pressed = {};


        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
    
        movementLogic(); // resolve player movement

        entity->velocity.y -= 9.81f * deltaTime;
        if (entity->velocity.y < -50.0f) entity->velocity.y = -50.0f;
        entity->velocity.x += 0.1f * deltaTime;


        glm::vec3 estep = entity->velocity * deltaTime;
        int esubsteps = glm::max(1, (int)glm::ceil(glm::length(estep) / (entity->radius * 0.5f)));

        for (int i = 0; i < esubsteps; i++)
        {
            entity->transform.x += estep.x / esubsteps;
            entity->transform.y += estep.y / esubsteps;
            entity->transform.z += estep.z / esubsteps;

            resolveEntityCollision(*entity);
        }


        
        // split step into smaller steps no larger than half players radius so it cant phase
        glm::vec3 step = player.velocity * deltaTime;
        int substeps = glm::max(1, (int)glm::ceil(glm::length(step) / (player.radius * 0.5f)));

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

        

        // --- collisions --- //

        // Compose first: this refreshes every `world` in the graph and lets the
        // camera derive its pos/front, which clearBG() reads to build `view`.
        // Draw only after both, or the view matrix lags the scene by a frame.
        for (Object*& obj : parents)
        {
            obj->world = obj->transform.matrix();
            obj->Compose();
        }

        // After Compose for the same reason clearBG() is: the camera basis this
        // reads is only valid once the graph has been recomposed, and every
        // AudioSource pushed its new position during that same pass.
        updateAudio(camera.pos, camera.front, camera.up);

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

    shutdownAudio();
    glfwTerminate();
    return 0;
}   