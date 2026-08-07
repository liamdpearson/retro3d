#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <ufbx.h>

#include "graphics.h"


int SW = 0;   // overwritten from the monitor's video mode in main()
int SH = 0;

float yaw   = -90.0f;   // degrees. -90 so we start looking down -Z, not +X
float pitch = 0.0f;
float lastX, lastY;   // last mouse pos (start at screen center)
bool  firstMouse = true;

float deltaTime = 0.0f, lastFrame = 0.0f;  // for frame-rate-independent speed

GLFWwindow* window;

unsigned int vs;
unsigned int fs;
unsigned int shaderProgram;
int modelLoc, viewLoc, projectionLoc;
int boneMatricesLoc;
int lightModeLoc, objectLightLoc, textModeLoc;

std::vector<Tri> occluders;
std::vector<TriAABB> colliders;
std::vector<glm::vec3> lightGrid;

// for finding the bounds box of the scene for light grid
float minX = INFINITY;
float maxX = -INFINITY;
float minY = INFINITY;
float maxY = -INFINITY;
float minZ = INFINITY;
float maxZ = -INFINITY;

// Runs once per vertex. Its only job: set gl_Position, the vertex's final
// position in "clip space". For now we pass our coordinates straight through.
// `layout (location = 0)` ties aPos to attribute slot 0 (we configure slot 0 below).
const char* vertexShaderSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aBoneIndices;
layout (location = 4) in vec4 aBoneWeights;
layout (location = 5) in vec3 aBakedColor;

const int MAX_BONES = 100;   // keep in sync with C++ MAX_BONES

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 boneMatrices[MAX_BONES];

out vec2 TexCoord;
out vec3 Normal;
out vec3 BakedColor;

void main()
{
    float total = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;

    vec4 pos;
    if (total > 0.0001) {
        // Linear-blend skinning: weighted sum of the vertex's bone palette matrices.
        mat4 skin =
            aBoneWeights.x * boneMatrices[int(aBoneIndices.x)] +
            aBoneWeights.y * boneMatrices[int(aBoneIndices.y)] +
            aBoneWeights.z * boneMatrices[int(aBoneIndices.z)] +
            aBoneWeights.w * boneMatrices[int(aBoneIndices.w)];
        pos = skin * vec4(aPos, 1.0);
    } else {
        pos = vec4(aPos, 1.0);   // unskinned (gun/skull/sprite): pass straight through
    }

    gl_Position = projection * view * model * pos;
    TexCoord = aTexCoord;
    BakedColor = aBakedColor;
    Normal = mat3(model) * aNormal;
}
)glsl";

// Runs once per fragment (≈ per pixel the triangle covers). Outputs the color.
const char* fragmentShaderSrc = R"glsl(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 Normal;
in vec3 BakedColor;
uniform sampler2D tex0;

uniform int LightMode;      // 0 = baked per-vertex, 1 = per-object dynamic
uniform vec3 ObjectLight;   // only read when LightMode == 1
uniform int TextMode;       // 1 = glyph atlas

void main()
{
    if (TextMode == 1)
    {
        float cov = texture(tex0, TexCoord).r;
        FragColor = vec4(ObjectLight, cov);   // GL_SRC_ALPHA blend does the AA
        return;
    }
    
    vec4 c = texture(tex0, TexCoord);
    if (c.a < 0.5) discard;   // Doom-style cutout: drop transparent texels entirely

    // Static geometry carries its lighting in the vertex data; movers get one
    // value sampled at their origin, so the whole mesh shades uniformly.
    vec3 lighting = (LightMode == 1) ? ObjectLight : BakedColor;

    FragColor = vec4(c.rgb * lighting, c.a);
}
)glsl";

// ----------------------------------------------------------------------------
// Helper: compile one shader and report any GLSL compile errors.
// (Shaders compile on the GPU driver — if you get a typo, you NEED this log.)
// ----------------------------------------------------------------------------
unsigned int compileShader(GLenum type, const char* src)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return shader;
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

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Bypass the OS pointer-acceleration curve ("enhance pointer precision") and
    // read the mouse's own counts, so a given hand movement always turns the view
    // the same amount regardless of how fast it was made. Only works with the
    // cursor disabled, and only where the platform supports it — GLFW would raise
    // an error instead of ignoring the call, so ask first.
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

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

// Load a PNG/JPG from `path` into a new GL texture and return its id.
unsigned int loadTexture(const char* path)
{
    stbi_set_flip_vertically_on_load(true);
    int tw, th, tch;
    unsigned char* data = stbi_load(path, &tw, &th, &tch, 0);

    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (data) {
        GLenum fmt = (tch == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::fprintf(stderr, "Failed to load texture: %s\n", path);
    }
    stbi_image_free(data);
    return texture;
}

// Pixel size of a PNG/JPG as {width, height}, without decoding the image.
// Returns {0, 0} if the file can't be read.
std::vector<int> textureDimensions(const char* path)
{
    int tw = 0, th = 0, tch = 0;
    if (!stbi_info(path, &tw, &th, &tch)) {
        std::fprintf(stderr, "Failed to read texture dimensions: %s\n", path);
        return std::vector<int>{0, 0};
    }
    return std::vector<int>{tw, th};
}

bool loadOBJ(const char* path,
                    std::vector<float>& outVerts,
                    std::vector<unsigned int>& outIndices)
{
    std::ifstream file(path);
    if (!file) { std::fprintf(stderr, "Cannot open %s\n", path); return false; }

    //unsigned int num_verts = outVerts.size() / 3;

    std::vector<glm::vec3> pos;
    std::vector<glm::vec2> uv;
    std::vector<glm::vec3> normals;

    std::map<std::tuple<int,int,int>, unsigned int> uniqueMap;

    std::string line;
    while(std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            glm::vec3 temp = glm::vec3(x, y, z);
            pos.push_back(temp);
        }
        else if (tag == "vt") {
            float u, v;
            ss >> u >> v;
            glm::vec2 temp = glm::vec2(u, v);
            uv.push_back(temp);
        }
        else if (tag == "vn") {
            float x, y, z;
            ss >> x >> y >> z;
            normals.push_back(glm::vec3(x, y, z));
        }
        else if (tag == "f") {
            std::string vert;
            std::vector<unsigned int> face;
            while (ss >> vert) {
                std::istringstream vs(vert);
                std::string p, t, n;
                std::getline(vs, p, '/');
                std::getline(vs, t, '/');
                std::getline(vs, n, '/');
                int posIdx = std::stoi(p) - 1;
                int uvIdx =  t.empty() ? -1 : std::stoi(t) - 1;
                int nIdx =   n.empty() ? -1 : std::stoi(n) - 1;

                std::tuple<int, int, int> key(posIdx, uvIdx, nIdx);
                auto found = uniqueMap.find(key);
                if (found != uniqueMap.end()) {
                    face.push_back(found->second);
                } else {
                    unsigned int newIndex = (unsigned int)(outVerts.size() / VERTEX_FLOATS);
                    // pos
                    outVerts.push_back(pos[posIdx].x);
                    outVerts.push_back(pos[posIdx].y);
                    outVerts.push_back(pos[posIdx].z);
                    // uv
                    if (uvIdx >= 0) {
                        outVerts.push_back(uv[uvIdx].x);
                        outVerts.push_back(uv[uvIdx].y);
                    } else {
                        outVerts.push_back(0.0f);
                        outVerts.push_back(0.0f);
                    }
                    // normal
                    if (nIdx >= 0) {
                        outVerts.push_back(normals[nIdx].x);
                        outVerts.push_back(normals[nIdx].y);
                        outVerts.push_back(normals[nIdx].z);
                    } else {
                        outVerts.push_back(0.0f); outVerts.push_back(0.0f); outVerts.push_back(0.0f);
                    }
                    // bone indices (4) + bone weights (4) zero for now
                    for (int i = 0; i < 8; i++) outVerts.push_back(0.0f);
                    for (int i = 0; i < 3; i++) outVerts.push_back(1.0f);
                    
                    uniqueMap[key] = newIndex;
                    face.push_back(newIndex);
                }

            }
            // triangulate as a fan — indices are already global, so no +num_verts here
            for (size_t i = 1; i + 1 < face.size(); i++) {
                outIndices.push_back(face[0]);
                outIndices.push_back(face[i]);
                outIndices.push_back(face[i+1]);
            }
        }
        // ignore vt, vn, #, o, g, s, mtllib, usemtl for now
    }
    return true;
}

static glm::mat4 ufbxToGlm(const ufbx_matrix& m)
{
    glm::mat4 r(1.0f);
    r[0] = glm::vec4((float)m.cols[0].x, (float)m.cols[0].y, (float)m.cols[0].z, 0.0f);
    r[1] = glm::vec4((float)m.cols[1].x, (float)m.cols[1].y, (float)m.cols[1].z, 0.0f);
    r[2] = glm::vec4((float)m.cols[2].x, (float)m.cols[2].y, (float)m.cols[2].z, 0.0f);
    r[3] = glm::vec4((float)m.cols[3].x, (float)m.cols[3].y, (float)m.cols[3].z, 1.0f);
    return r;
}

bool loadFBX(const char* path,
             std::vector<float>& outVerts,
             std::vector<unsigned int>& outIndices,
             Skeleton& outSkel,
             std::vector<Animation>& outAnims)
{
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path, &opts, &error);
    if (!scene)
    {
        std::fprintf(stderr, "Failed to load %s: %s\n", path, error.description.data);
        return false;
    }

    std::map<ufbx_node*, int> boneMap;
    std::vector<ufbx_node*> boneNodes;

    for (size_t ni = 0; ni < scene->nodes.count; ni++)
    {
        ufbx_node* node = scene->nodes.data[ni];
        if (!node->mesh) continue;

        ufbx_mesh* mesh = node->mesh;
        ufbx_matrix geomToWorld = node->geometry_to_world;
        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);

        ufbx_skin_deformer* skin = 
            mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
        std::vector<int> clusterToBone;
        if (skin)
        {
            clusterToBone.resize(skin->clusters.count);
            for (size_t c = 0; c < skin->clusters.count; c++)
            {
                ufbx_skin_cluster* cl = skin->clusters.data[c];
                ufbx_node* boneNode = cl->bone_node;
                auto it = boneMap.find(boneNode);
                int gi;
                if (it != boneMap.end()) {
                    gi = it->second;
                } else {
                    gi = (int)outSkel.inverseBind.size();
                    boneMap[boneNode] = gi;
                    boneNodes.push_back(boneNode);

                    // space fix
                    outSkel.inverseBind.push_back(ufbxToGlm(cl->geometry_to_bone) *
                                                  glm::inverse(ufbxToGlm(geomToWorld)));
                    outSkel.parentWorld.push_back(boneNode->parent
                                                  ? ufbxToGlm(boneNode->parent->node_to_world)
                                                  : glm::mat4(1.0f));
                    outSkel.names.push_back(boneNode ? std::string(boneNode->name.data) : "");
                    outSkel.parent.push_back(-1);
                }
                clusterToBone[c] = gi;
            }
        }

        for (size_t fi = 0; fi < mesh->faces.count; fi++)
        {
            ufbx_face face = mesh->faces.data[fi];
            size_t numTris = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);

            for (size_t i = 0; i < numTris * 3; i++)
            {
                uint32_t corner = tri[i]; // index into the mesh's vertex attributes

                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, corner);
                p = ufbx_transform_position(&geomToWorld, p); // -> world space
                ufbx_vec2 uv = mesh->vertex_uv.exists
                    ? ufbx_get_vertex_vec2(&mesh->vertex_uv, corner)
                    : ufbx_vec2{0.0f, 0.0f};
                ufbx_vec3 n = mesh->vertex_normal.exists
                    ? ufbx_get_vertex_vec3(&mesh->vertex_normal, corner)
                    : ufbx_vec3{0.0f, 0.0f, 1.0f};
                n = ufbx_transform_direction(&geomToWorld, n);

                float bi[4] = {0,0,0,0};
                float bw[4] = {0,0,0,0};
                if (skin)
                {
                    uint32_t vtx = mesh->vertex_indices.data[corner];
                    ufbx_skin_vertex sv = skin->vertices.data[vtx];

                    uint32_t count = sv.num_weights < 4 ? sv.num_weights : 4;
                    float sum = 0.0f;
                    for (uint32_t w = 0; w < count; w++)
                    {
                        ufbx_skin_weight sw = skin->weights.data[sv.weight_begin + w];
                        bi[w] = (float)clusterToBone[sw.cluster_index];
                        bw[w] = (float)sw.weight;
                        sum += (float)sw.weight;
                    }
                    if (sum > 0.0f) for (int w = 0; w < 4; w++) bw[w] /= sum;
                }

                outIndices.push_back((unsigned int)(outVerts.size() / VERTEX_FLOATS));
                outVerts.push_back((float)p.x);
                outVerts.push_back((float)p.y);
                outVerts.push_back((float)p.z);
                outVerts.push_back((float)uv.x);
                outVerts.push_back((float)uv.y);
                outVerts.push_back((float)n.x);
                outVerts.push_back((float)n.y);
                outVerts.push_back((float)n.z);
                outVerts.push_back(bi[0]);           // bone indices
                outVerts.push_back(bi[1]);
                outVerts.push_back(bi[2]);
                outVerts.push_back(bi[3]);
                outVerts.push_back(bw[0]);           // bone weights
                outVerts.push_back(bw[1]);
                outVerts.push_back(bw[2]);
                outVerts.push_back(bw[3]);

                // add 3 more 0's will be filled later with baked color
                outVerts.push_back(1.0f); outVerts.push_back(1.0f); outVerts.push_back(1.0f);
            }
        }
    }

    for (auto& kv : boneMap)
    {
        ufbx_node* boneNode = kv.first;
        int gi = kv.second;
        if (boneNode && boneNode->parent)
        {
            auto pit = boneMap.find(boneNode->parent);
            if (pit != boneMap.end()) outSkel.parent[gi] = pit->second;
        }
    }

    // Bake every anim stack in the file into its own clip. All clips share the
    // same `boneNodes` skeleton, so switching clips at runtime is just picking a
    // different Animation to sample in computePose().
    for (size_t si = 0; si < scene->anim_stacks.count && !boneNodes.empty(); si++)
    {
        ufbx_anim_stack* stack = scene->anim_stacks.data[si];
        ufbx_anim* anim = stack->anim;

        float fps = 30.0f;
        double t0 = stack->time_begin;
        double t1 = stack->time_end;
        double dur = t1 - t0;
        int frames = (int)std::ceil(dur * fps) + 1;
        if (frames < 1) frames = 1;

        Animation out;
        out.name = stack->name.data ? std::string(stack->name.data) : "";
        out.fps = fps;
        out.duration = (float)dur;
        out.frameCount = frames;
        out.tracks.resize(boneNodes.size());

        for (int f = 0; f < frames; f++)
        {
            double t = t0 + (frames > 1 ? dur * (double)f / (double)(frames - 1) : 0.0);
            for (size_t b = 0; b < boneNodes.size(); b++)
            {
                // ufbx returns the node's LOCAL (parent-relative) transform, keyframe-
                // interpolated for us at time t. We re-store it as our own keyframe.
                ufbx_transform lt = ufbx_evaluate_transform(anim, boneNodes[b], t);
                out.tracks[b].pos.push_back(
                    glm::vec3((float)lt.translation.x, (float)lt.translation.y, (float)lt.translation.z));
                out.tracks[b].rot.push_back(   // glm::quat is (w, x, y, z)!
                    glm::quat((float)lt.rotation.w, (float)lt.rotation.x,
                              (float)lt.rotation.y, (float)lt.rotation.z));
                out.tracks[b].scale.push_back(
                    glm::vec3((float)lt.scale.x, (float)lt.scale.y, (float)lt.scale.z));
            }
        }
        std::fprintf(stderr, "loadFBX(%s): baked clip[%zu] '%s' %d frames @ %.0ffps (%.2fs)\n",
                     path, si, out.name.c_str(), frames, fps, (float)dur);
        outAnims.push_back(std::move(out));
    }
    std::fprintf(stderr, "loadFBX(%s): %zu verts, %zu bones\n",
                 path, outVerts.size() / VERTEX_FLOATS, outSkel.inverseBind.size());

    ufbx_free_scene(scene);
    return true;
}   

Mesh makeObj(const char* objPath, const char* texPath,
             Transform transform, bool isStatic, bool collides)
{
    Mesh obj;
    loadOBJ(objPath, obj.vertices, obj.indices);
    obj.isStatic = isStatic;
    obj.collides = collides;
    obj.transform = transform;
    obj.world = transform.matrix();
    obj.texture = loadTexture(texPath);
    obj.indexCount = (GLsizei)obj.indices.size();
    
    return obj;
}

// always dynamic because fbxs are for animations in my case
Mesh makeFbx(const char* objPath, const char* texPath,
             Transform transform)
{
    Mesh obj;
    loadFBX(objPath, obj.vertices, obj.indices, obj.skeleton, obj.animations);
    obj.transform = transform;
    obj.world = transform.matrix();
    obj.texture = loadTexture(texPath);
    obj.indexCount = (GLsizei)obj.indices.size();

    return obj;
}

static float getEntityHeight(const std::vector<float>& vertices)
{
    float max = 0.0f;
    float min = 0.0f;

    for (int i = 0; i < vertices.size(); i += VERTEX_FLOATS)
    {
        if (vertices[i + 1] > max) max = vertices[i + 1];
        if (vertices[i + 1] < min) min = vertices[i + 1];
    }

    return max - min;
}

Entity makeEntity(const char* objPath, const char* texPath,
                  Transform transform, float ratiohr)
{
    Entity entity;
    loadFBX(objPath, entity.vertices, entity.indices, entity.skeleton, entity.animations);
    entity.transform = transform;
    entity.world = transform.matrix();
    entity.texture = loadTexture(texPath);
    entity.indexCount = (GLsizei)entity.indices.size();

    entity.height = getEntityHeight(entity.vertices) * transform.scale;
    entity.radius = entity.height * ratiohr;
    
    std::fprintf(stderr, "Entity created with radius: %f and height %f\n",
                entity.radius, entity.height);

    return entity;
}

UIElement makeUIElement(const char* texPath, float x, float y, float scale)
{
    UIElement ui;

    std::vector<int> dimensions = textureDimensions(texPath);
    unsigned int width = dimensions[0], height = dimensions[1];

    ui.vertices = std::vector<float>{
        x - width/2 * scale, y + height/2 * scale, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        x + width/2 * scale, y + height/2 * scale, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        x + width/2 * scale, y - height/2 * scale, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        x - width/2 * scale, y - height/2 * scale, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f
    };
    ui.indices = std::vector<unsigned int>{
        0, 1, 2,
        0, 2, 3
    };

    ui.x = x; ui.y = y; ui.scale = scale;
    ui.width = width; ui.height = height;
    ui.texture = loadTexture(texPath);

    return ui;
}

void uploadObject(Mesh &obj)
{
    glGenVertexArrays(1, &obj.VAO);
    glGenBuffers(1, &obj.VBO);
    glGenBuffers(1, &obj.EBO);

    glBindVertexArray(obj.VAO); // start recording into this VAO

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obj.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
             obj.indices.size() * sizeof(unsigned int),
             obj.indices.data(), GL_STATIC_DRAW);

    // VBO: upload the vertex bytes to GPU memory.
    glBindBuffer(GL_ARRAY_BUFFER, obj.VBO);
    glBufferData(GL_ARRAY_BUFFER,
             obj.vertices.size() * sizeof(float),
             obj.vertices.data(), GL_STATIC_DRAW); // change GL_STATIC_DRAW to GL_DYNAMIC_DRAW if tri will warp in 3d space
    
    const GLsizei stride = VERTEX_FLOATS * sizeof(float);
    // 0: pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    // 1: uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 2: normal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // 3: bone indices (stored as floats, cast to int later)
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // 4: bone weights
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // 5: baked color
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, (void*)(16 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0); // stop recording (optional tidy-up)
}

void uploadUIElement(UIElement &ui)
{
    glGenVertexArrays(1, &ui.VAO);
    glGenBuffers(1, &ui.VBO);
    glGenBuffers(1, &ui.EBO);

    glBindVertexArray(ui.VAO); // start recording into this VAO

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
             ui.indices.size() * sizeof(unsigned int),
             ui.indices.data(), GL_STATIC_DRAW);

    // VBO: upload the vertex bytes to GPU memory.
    glBindBuffer(GL_ARRAY_BUFFER, ui.VBO);
    glBufferData(GL_ARRAY_BUFFER,
             ui.vertices.size() * sizeof(float),
             ui.vertices.data(), GL_DYNAMIC_DRAW); // drawUIElement() rewrites these positions every frame
    
    const GLsizei stride = VERTEX_FLOATS * sizeof(float);
    // 0: pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    // 1: uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 2: normal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // 3: bone indices (stored as floats, cast to int later)
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // 4: bone weights
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // 5: baked color
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, (void*)(16 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0); // stop recording (optional tidy-up)
}

void uploadUIText(UIText& t)
{
    glGenVertexArrays(1, &t.VAO);
    glGenBuffers(1, &t.VBO);
    glGenBuffers(1, &t.EBO);
    glBindVertexArray(t.VAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t.EBO);   // recorded into the VAO
    glBindBuffer(GL_ARRAY_BUFFER, t.VBO);
    
    const GLsizei stride = VERTEX_FLOATS * sizeof(float);
    // 0: pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    // 1: uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 2: normal
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // 3: bone indices (stored as floats, cast to int later)
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // 4: bone weights
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // 5: baked color
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, (void*)(16 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0);
}

// Zero-filled 19-float vertex: only pos + uv are read in text mode.
static void pushGlyphQuad(std::vector<float>& v, std::vector<unsigned int>& idx,
                          float x0, float y0, float x1, float y1, const Glyph& g)
{
    unsigned int base = (unsigned int)(v.size() / VERTEX_FLOATS);
    auto vert = [&](float px, float py, float u, float w){
        v.insert(v.end(), { px, py, 0, u, w, 0,0,0, 0,0,0,0, 0,0,0,0, 1,1,1 });
    };
    vert(x0, y0, g.u0, g.v0);   // top-left
    vert(x1, y0, g.u1, g.v0);   // top-right
    vert(x1, y1, g.u1, g.v1);   // bottom-right
    vert(x0, y1, g.u0, g.v1);   // bottom-left
    idx.insert(idx.end(), { base+0, base+1, base+2, base+0, base+2, base+3 });
}

// Walks the string, placing each glyph along the baseline. size is a straight
// multiplier over bake pixels, so size == bakePixelHeight renders at 1:1.
void layoutText(UIText& t)
{
    t.vertices.clear();
    t.indices.clear();
    if (!t.font) return;

    const Font& f = *t.font;
    float s = t.size / f.bakePixelHeight;   // <-- treat size as a pixel height
    float baseY = t.pos.y + f.ascent * s;   // baseline sits `ascent` below the top

    // Sums xadvance over [begin, end) to get a line's total width, so a
    // right-anchored line can start far enough left to end at t.pos.x.
    auto lineWidth = [&](size_t begin, size_t end) {
        float w = 0.0f;
        for (size_t i = begin; i < end; i++)
        {
            unsigned char ch = t.text[i];
            if (ch >= 32 && ch <= 126) w += f.glyphs[ch - 32].xadvance * s;
        }
        return w;
    };
    auto lineStartX = [&](size_t begin) {
        if (t.anchorLeft) return t.pos.x;
        size_t end = t.text.find('\n', begin);
        return t.pos.x - lineWidth(begin, end == std::string::npos ? t.text.size() : end);
    };

    size_t lineStart = 0;
    float penX = lineStartX(lineStart);

    for (size_t i = 0; i < t.text.size(); i++)
    {
        unsigned char ch = t.text[i];
        if (ch == '\n')
        {
            baseY += f.lineHeight * s;
            lineStart = i + 1;
            penX = lineStartX(lineStart);
            continue;
        }
        if (ch < 32 || ch > 126) continue;   // outside the baked range

        const Glyph& g = f.glyphs[ch - 32];
        if (g.w > 0 && g.h > 0)               // space advances but has no quad
        {
            float x0 = penX + g.xoff * s;
            float y0 = baseY + g.yoff * s;
            pushGlyphQuad(t.vertices, t.indices, x0, y0, x0 + g.w * s, y0 + g.h * s, g);
        }
        penX += g.xadvance * s;
    }
}

// Sample `an` at time t (seconds, wrapped into the clip) into per-bone local TRS.
// `out` is sized by the caller to the skeleton's bone count; bones the clip has no
// track for keep whatever `out` already held.
static void sampleClip(const Animation& an, float t, std::vector<BonePose>& out)
{
    // Which two frames to blend, and by how much.
    float dur = an.duration > 0.0f ? an.duration : 1.0f;
    float wrapped = std::fmod(t, dur);
    if (wrapped < 0.0f) wrapped += dur;
    float frameF = wrapped * an.fps;
    int   f0 = (int)std::floor(frameF) % an.frameCount;
    int   f1 = (f0 + 1) % an.frameCount;
    float a  = frameF - std::floor(frameF);

    int n = (int)std::min(out.size(), an.tracks.size());
    for (int b = 0; b < n; b++)
    {
        const BoneTrack& tr = an.tracks[b];
        out[b].pos   = glm::mix  (tr.pos[f0],   tr.pos[f1],   a);
        out[b].rot   = glm::slerp(tr.rot[f0],   tr.rot[f1],   a);
        out[b].scale = glm::mix  (tr.scale[f0], tr.scale[f1], a);
    }
}

// Sample obj's baked clip at obj.animTime and fill `palette` with each bone's
// skinning matrix Aⱼ·Bⱼ⁻¹. No skeleton/clip -> identity palette (bind pose).
//
// Takes obj by non-const ref because it caches the pose it produced on the mesh
// (and retires a finished cross-fade) — the next SetAnimation() blends out of it.
static void computePose(Mesh& obj, std::vector<glm::mat4>& palette)
{
    const Skeleton&  sk = obj.skeleton;
    int n = (int)sk.inverseBind.size();
    palette.assign(n, glm::mat4(1.0f));
    if (n == 0 || obj.currentAnim < 0 || obj.currentAnim >= (int)obj.animations.size())
        return;
    const Animation& an = obj.animations[obj.currentAnim];
    if (an.frameCount == 0) return;

    // 1) sample the clip, then ease out of the pose the armature was left in when
    //    the clip was switched. The target keeps advancing while the fade runs, so
    //    the new clip plays from its first frame rather than waiting on the blend.
    std::vector<BonePose> pose(n);
    sampleClip(an, obj.animTime, pose);

    if (obj.blendDuration > 0.0f && (int)obj.blendFrom.size() == n)
    {
        float w = std::clamp(obj.blendElapsed / obj.blendDuration, 0.0f, 1.0f);
        for (int b = 0; b < n; b++)
        {
            pose[b].pos   = glm::mix  (obj.blendFrom[b].pos,   pose[b].pos,   w);
            pose[b].rot   = glm::slerp(obj.blendFrom[b].rot,   pose[b].rot,   w);
            pose[b].scale = glm::mix  (obj.blendFrom[b].scale, pose[b].scale, w);
        }
        if (w >= 1.0f)   // fade done — stop paying for it every frame
        {
            obj.blendDuration = 0.0f;
            obj.blendElapsed = 0.0f;
            obj.blendFrom.clear();
        }
    }

    // Remember what actually went on screen, including a half-finished fade, so
    // switching clips mid-blend starts from the pose the eye last saw.
    obj.lastPose = pose;

    // 2) local TRS -> local matrix
    std::vector<glm::mat4> local(n);
    for (int b = 0; b < n; b++)
    {
        local[b] = glm::translate(glm::mat4(1.0f), pose[b].pos)
                 * glm::mat4_cast(pose[b].rot)
                 * glm::scale(glm::mat4(1.0f), pose[b].scale);
    }

    // 3) walk the hierarchy -> world matrices. Bones aren't guaranteed parent-first,
    //    so resolve iteratively until each bone's parent is ready.
    std::vector<glm::mat4> world(n);
    std::vector<char>      done(n, 0);
    int remaining = n;
    while (remaining > 0)
    {
        int progressed = 0;
        for (int b = 0; b < n; b++)
        {
            if (done[b]) continue;
            int par = sk.parent[b];
            if (par < 0)        { world[b] = sk.parentWorld[b] * local[b]; done[b] = 1; remaining--; progressed = 1; }
            else if (done[par]) { world[b] = world[par]        * local[b]; done[b] = 1; remaining--; progressed = 1; }
        }
        if (!progressed) break;   // guard against a broken parent chain
    }

    // 4) palette = animated world * inverse bind
    for (int b = 0; b < n; b++) palette[b] = world[b] * sk.inverseBind[b];
}

// Sample the baked light grid at an arbitrary world point, trilinearly blending
// the eight cells around it so a mover crossing a cell boundary fades instead of
// popping.
//
// bakeSceneLighting() fills the grid at 1-unit spacing with x as the outer loop
// and z as the inner one, so a cell sits at (gx * ny + gy) * nz + gz — the
// multiplier for an axis is the size of everything nested inside it, not that
// axis's own extent. Indices are grid-relative: cell 0 is at (minX, minY, minZ),
// which is negative in world space for most scenes.
//
// Points outside the baked bounds clamp to the edge cell.
static glm::vec3 gridLightAt(const glm::vec3& p)
{
    // No bake (or a bake that found no occluders to size the grid from) — stay
    // fullbright rather than reading off the end of an empty vector.
    if (lightGrid.empty()) return glm::vec3(1.0f);

    const int nx = (int)(maxX - minX) + 1;
    const int ny = (int)(maxY - minY) + 1;
    const int nz = (int)(maxZ - minZ) + 1;

    // Clamp in grid space, before splitting into index and fraction, so an
    // out-of-bounds point gets weights that agree with its clamped indices.
    float fx = std::clamp(p.x - minX, 0.0f, (float)(nx - 1));
    float fy = std::clamp(p.y - minY, 0.0f, (float)(ny - 1));
    float fz = std::clamp(p.z - minZ, 0.0f, (float)(nz - 1));

    int x0 = (int)std::floor(fx), x1 = std::min(x0 + 1, nx - 1);
    int y0 = (int)std::floor(fy), y1 = std::min(y0 + 1, ny - 1);
    int z0 = (int)std::floor(fz), z1 = std::min(z0 + 1, nz - 1);

    float tx = fx - x0, ty = fy - y0, tz = fz - z0;

    auto cell = [&](int gx, int gy, int gz) -> const glm::vec3& {
        return lightGrid[(gx * ny + gy) * nz + gz];
    };

    // Collapse z, then y, then x.
    glm::vec3 c00 = glm::mix(cell(x0, y0, z0), cell(x0, y0, z1), tz);
    glm::vec3 c01 = glm::mix(cell(x0, y1, z0), cell(x0, y1, z1), tz);
    glm::vec3 c10 = glm::mix(cell(x1, y0, z0), cell(x1, y0, z1), tz);
    glm::vec3 c11 = glm::mix(cell(x1, y1, z0), cell(x1, y1, z1), tz);

    glm::vec3 c0 = glm::mix(c00, c01, ty);
    glm::vec3 c1 = glm::mix(c10, c11, ty);

    return glm::mix(c0, c1, tx);
}

void drawObj(Mesh& obj)
{
    glm::mat4 model;
    // The world matrix already encodes translation, rotation, and scale.
    model = obj.world;

    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    // Static meshes already carry their lighting per-vertex. Movers get one value
    // sampled at their world origin — column 3 of the world matrix is its
    // translation, which is the object's origin without needing to decompose.
    if (obj.isStatic)
    {
        glUniform1i(lightModeLoc, 0);
    }
    else
    {
        glm::vec3 origin(obj.world[3][0], obj.world[3][1], obj.world[3][2]);
        glm::vec3 lit = gridLightAt(origin);

        glUniform3fv(objectLightLoc, 1, glm::value_ptr(lit));
        glUniform1i(lightModeLoc, 1);
    }

    // Skinned meshes advance their clip and upload a fresh bone palette. Unskinned
    // meshes leave the palette alone — their zero-weight verts take the shader's
    // pass-through branch, so stale palette data never touches them.
    if (!obj.skeleton.inverseBind.empty())
    {
        obj.animTime += deltaTime;
        if (obj.nextAnim != -1) {
            if (obj.animations[obj.currentAnim].duration - obj.animTime <= 0)
                { obj.SetAnimation(obj.nextAnim, 0.5f); obj.nextAnim = -1; }
        }
        if (obj.blendDuration > 0.0f) obj.blendElapsed += deltaTime;
        std::vector<glm::mat4> palette;
        computePose(obj, palette);
        GLsizei count = (GLsizei)std::min((size_t)MAX_BONES, palette.size());
        glUniformMatrix4fv(boneMatricesLoc, count, GL_FALSE, glm::value_ptr(palette[0]));
    }

    glBindTexture(GL_TEXTURE_2D, obj.texture);
    glBindVertexArray(obj.VAO);
    glDrawElements(GL_TRIANGLES, obj.indexCount, GL_UNSIGNED_INT, 0);
}

void drawUIElement(UIElement& ui)
{
    // update location
    ui.vertices[0] = ui.x - ui.width/2 * ui.scale;
    ui.vertices[1] = ui.y + ui.height/2 * ui.scale;

    ui.vertices[19] = ui.x + ui.width/2 * ui.scale;
    ui.vertices[20] = ui.y + ui.height/2 * ui.scale;

    ui.vertices[38] = ui.x + ui.width/2 * ui.scale;
    ui.vertices[39] = ui.y - ui.height/2 * ui.scale;

    ui.vertices[57] = ui.x - ui.width/2 * ui.scale;
    ui.vertices[58] = ui.y - ui.height/2 * ui.scale;

    // Push the rewritten positions to the GPU — the CPU-side array alone
    // changes nothing on screen; the VBO still holds last upload's bytes.
    glBindBuffer(GL_ARRAY_BUFFER, ui.VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
             ui.vertices.size() * sizeof(float),
             ui.vertices.data());

    glUniform1i(lightModeLoc, 1);

    glBindTexture(GL_TEXTURE_2D, ui.texture);
    glBindVertexArray(ui.VAO);
    glDrawElements(GL_TRIANGLES, ui.indexCount, GL_UNSIGNED_INT, 0);
}

// Rebuilds geometry each call (the string may have changed) and draws the whole
// line in one call. Must be issued between beginUI() and endUI().
void drawText(UIText& t)
{
    layoutText(t);
    if (t.indices.empty()) return;

    glBindVertexArray(t.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, t.VBO);
    glBufferData(GL_ARRAY_BUFFER, t.vertices.size() * sizeof(float),
                 t.vertices.data(), GL_DYNAMIC_DRAW);            // orphan + refill
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, t.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, t.indices.size() * sizeof(unsigned int),
                 t.indices.data(), GL_DYNAMIC_DRAW);

    glUniform1i(textModeLoc, 1);
    glUniform3f(objectLightLoc, t.color.r, t.color.g, t.color.b);  // reused as text tint

    glBindTexture(GL_TEXTURE_2D, t.font->atlas);
    glDrawElements(GL_TRIANGLES, (GLsizei)t.indices.size(), GL_UNSIGNED_INT, 0);

    glUniform1i(textModeLoc, 0);   // back to normal UI for whatever draws next
}

void clearBG(float r, float g, float b, float a)
{   
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);
    glViewport(0, 0, SW, SH);

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
        
    glm::mat4 projection = glm::perspective(
        glm::radians(camera.FOV),            // vertical field of view
        (float)fbw / (float)fbh,             // aspect ratio
        0.01f, 1000.0f);                       // near & far clip planes
    
    glm::mat4 view = glm::lookAt(
        camera.pos,         // camera position (Step 5 fills these in)
        camera.pos + camera.front,         // look at the origin (where the triangle is)
        camera.up);        // "up" direction

    glm::mat4 model = glm::mat4(1.0f);       // identity: triangle stays at the origin

    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc,       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(modelLoc,      1, GL_FALSE, glm::value_ptr(model));

    glActiveTexture(GL_TEXTURE0);
}

// Switches the shared shader over to 2D screen-space drawing. Call once after
// the 3D Draw() pass; everything issued afterwards is UI until the next frame's
// clearBG() puts the perspective/view matrices back.
//
// The ortho box is built with top > bottom on purpose, so quad positions are
// plain pixel coordinates with the origin at the top-left corner of the screen
// and +y running down — the convention every UI layout is easier to author in.
// The extent is SW/SH rather than the framebuffer size so it matches the
// viewport clearBG() set; the two are the same here because the window is
// fullscreen at the monitor's video mode.
void beginUI()
{
    glUseProgram(shaderProgram);

    glm::mat4 projection = glm::ortho(
        0.0f, (float)SW,     // left, right
        (float)SH, 0.0f,     // bottom, top  (flipped: y grows downward)
        -1.0f, 1.0f);        // near & far — z is just a sort key for UI

    glm::mat4 view  = glm::mat4(1.0f);   // no camera: the screen *is* the space
    glm::mat4 model = glm::mat4(1.0f);   // drawUI() overwrites this per element

    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc,       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(modelLoc,      1, GL_FALSE, glm::value_ptr(model));

    // Fullbright: UI must not pick up the scene's baked light or the grid.
    glUniform1i(lightModeLoc,  1);
    glUniform3f(objectLightLoc, 1.0f, 1.0f, 1.0f);

    // UI draws in submission order, back to front, and always over the scene.
    glDisable(GL_DEPTH_TEST);

    // Harmless until the fragment shader stops discarding on alpha, but this is
    // the state soft-edged UI needs, so set it here rather than remembering to.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
}

// Undoes beginUI()'s state so the next frame's 3D pass starts clean. The
// matrices don't need restoring — clearBG() unconditionally reuploads all three.
void endUI()
{
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// A mesh-less node has nothing of its own to upload; it just carries the walk.
void Object::Upload()
{
    for (Object*& child : children) child->Upload();
}

void Mesh::Upload()
{
    uploadObject(*this);
    Object::Upload();
}

void Camera::Upload()
{
    Object::Upload();
}

void Mesh::SetAnimation(int index, float blendTime, int nextAnim)
{
    if (index < 0 || index >= (int)animations.size()) return;

    // Take the pose that's on screen right now as the start of the fade. lastPose
    // is only filled once computePose() has run, so a mesh that has never been
    // drawn (or one with no skeleton) has nothing to leave and just snaps.
    if (blendTime > 0.0f && !lastPose.empty())
    {
        blendFrom = lastPose;
        blendDuration = blendTime;
        blendElapsed = 0.0f;
    }
    else
    {
        blendFrom.clear();
        blendDuration = 0.0f;
        blendElapsed = 0.0f;
    }

    currentAnim = index;
    animTime = 0.0f;   // restart the new clip from its first frame
    this->nextAnim= nextAnim;
}

bool Mesh::SetAnimation(const std::string& name, float blendTime, int nextAnim)
{
    for (int i = 0; i < (int)animations.size(); i++)
    {
        if (animations[i].name == name)
        {
            SetAnimation(i, blendTime, nextAnim);
            return true;
        }
    }
    return false;
}

// Composition pass. `world` is set by the caller for roots (see main's loop) and
// by the parent for children, so a mesh-less pivot still folds its transform
// into everything beneath it — that's what makes attachment points work.
//
// This is a separate walk from Draw() because the camera derives its pos/front
// here, and clearBG() needs those to build `view` before the first mesh is
// drawn. Fused into Draw(), the view matrix would always trail the scene it is
// viewing by one frame.
void Object::Compose()
{
    for (Object*& child : this->children)
    {
        child->world = this->world * child->transform.matrix();
        child->Compose();
    }
}

void Camera::Compose()
{
    // Read the camera's orientation straight out of the composed world matrix
    // rather than rebuilding it from yaw/pitch. `world` already carries every
    // parent's rotation, so a camera parented to a mover inherits its motion —
    // including roll, which a yaw/pitch pair can't represent.
    //
    // Local axes follow the GL convention: forward is -Z, up is +Y. Directions
    // go through mat3 to drop the translation, and are normalized because a
    // scaled parent (or the node's own transform.scale) leaks into the basis.
    glm::mat3 basis(world);

    pos   = glm::vec3(world[3]);
    front = glm::normalize(basis * glm::vec3(0.0f, 0.0f, -1.0f));
    up    = glm::normalize(basis * glm::vec3(0.0f, 1.0f,  0.0f));

    Object::Compose();
}

// Draw only — every `world` in the graph is already up to date by the time this
// runs. Recursion and nothing else on the base; Mesh overrides it to draw.
void Object::Draw()
{
    for (Object*& child : this->children) child->Draw();
}

void Mesh::Draw()
{
    drawObj(*this);
    Object::Draw();
}

Mesh::~Mesh()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteTextures(1, &texture);
}

Font bakeFont(const char* path, float pixelHeight)
{
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());

    Font font;
    font.bakePixelHeight = pixelHeight;
    font.atlasW = 512;
    font.atlasH = 512;

    // stb rasterises ASCII 32..126 into one 8-bit coverage bitmap and fills cdata
    // with pixel rects + metrics. 512x512 comfortably holds a ~48px ASCII set;
    // bump it if BakeFontBitmap returns <= 0 (ran out of atlas room).
    std::vector<unsigned char> bitmap(font.atlasW * font.atlasH);
    stbtt_bakedchar cdata[96];
    stbtt_BakeFontBitmap(ttf.data(), 0, pixelHeight,
                         bitmap.data(), font.atlasW, font.atlasH,
                         32, 96, cdata);

    // Vertical metrics for baseline placement and line spacing.
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf.data(), 0);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    float sc = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    font.ascent     = asc * sc;
    font.lineHeight = (asc - desc + gap) * sc;

    // Convert stb's pixel rects into normalised UVs + our Glyph metrics.
    for (int i = 0; i < 96; ++i)
    {
        const stbtt_bakedchar& c = cdata[i];
        Glyph& g = font.glyphs[i];
        g.u0 = c.x0 / (float)font.atlasW;  g.v0 = c.y0 / (float)font.atlasH;
        g.u1 = c.x1 / (float)font.atlasW;  g.v1 = c.y1 / (float)font.atlasH;
        g.w  = (float)(c.x1 - c.x0);       g.h  = (float)(c.y1 - c.y0);
        g.xoff = c.xoff;                   g.yoff = c.yoff;
        g.xadvance = c.xadvance;
    }

    // Upload the coverage atlas as a single-channel texture. No stbi flip is in
    // play here (we bypass loadTexture), so row 0 is the top and v0 = top edge,
    // which matches beginUI()'s y-down ortho.
    glGenTextures(1, &font.atlas);
    glBindTexture(GL_TEXTURE_2D, font.atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // 1 byte/texel: rows aren't 4-aligned
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 font.atlasW, font.atlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return font;
}