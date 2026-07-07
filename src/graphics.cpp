#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <ufbx.h>

#include "graphics.h"

int SW = 0;   // overwritten from the monitor's video mode in main()
int SH = 0;
float vps = 1.0f; // viewport scale

glm::vec3 cameraPos   = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

float yaw   = -90.0f;   // degrees. -90 so we start looking down -Z, not +X
float pitch = 0.0f;
float lastX, lastY;   // last mouse pos (start at screen center)
bool  firstMouse = true;

const float sensitivity = 0.1f;
float deltaTime = 0.0f, lastFrame = 0.0f;  // for frame-rate-independent speed

GLFWwindow* window;

unsigned int vs;
unsigned int fs;
unsigned int shaderProgram;
int modelLoc, viewLoc, projectionLoc;
int boneMatricesLoc;

int lightDirLoc, lightColorLoc, lightAmbientLoc;

glm::vec3 lightDir{0.0f, 0.0f, 1.0f};
glm::vec3 lightColor{1.0f, 1.0f, 1.0f};
float lightAmbient = 0.1f;

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

const int MAX_BONES = 100;   // keep in sync with C++ MAX_BONES

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 boneMatrices[MAX_BONES];

out vec2 TexCoord;
out vec3 Normal;

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
    Normal = mat3(model) * aNormal;
}
)glsl";


// Runs once per fragment (≈ per pixel the triangle covers). Outputs the color.
const char* fragmentShaderSrc = R"glsl(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 Normal;
uniform sampler2D tex0;
uniform vec3 LightDir;
uniform vec3 LightColor;
uniform float LightAmbient;

void main()
{
    vec3 n = normalize(Normal);
    vec3 l = normalize(-LightDir);

    vec4 c = texture(tex0, TexCoord);
    if (c.a < 0.5) discard;   // Doom-style cutout: drop transparent texels entirely

    float diffuse = max(dot(n, l), 0.0);
    vec3 lighting = LightAmbient + diffuse * LightColor;

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


void mouseCallback(GLFWwindow*, double xpos, double ypos)
{
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }

    float xoffset = (float)(xpos - lastX);
    float yoffset = (float)(lastY - ypos);
    lastX = float(xpos);
    lastY = float(ypos);

    yaw += xoffset * sensitivity;
    pitch += yoffset * sensitivity;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
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


Object makeObj(const char* objPath, const char* texPath,
               Transform transform)
{
    Object obj;
    loadOBJ(objPath, obj.vertices, obj.indices);
    obj.transform = transform;
    obj.world = transform.matrix();
    obj.texture = loadTexture(texPath);
    obj.indexCount = (GLsizei)obj.indices.size();
    
    return obj;
}


Object makeFbx(const char* objPath, const char* texPath,
               Transform transform)
{
    Object obj;
    loadFBX(objPath, obj.vertices, obj.indices, obj.skeleton, obj.animations);
    obj.transform = transform;
    obj.world = transform.matrix();
    obj.texture = loadTexture(texPath);
    obj.indexCount = (GLsizei)obj.indices.size();

    return obj;
}


// A billboard sprite: a 1x1 quad in the XY plane, facing +Z, centered on origin.
// No OBJ file — the geometry is just two triangles. The transform's yaw/pitch are
// ignored (billboardModel rebuilds the rotation each frame); only pos and scale matter.
// Layout matches loadOBJ: 5 floats per vertex (x, y, z, u, v).
Object makeSprite(const char* texPath, Transform transform)
{
    Object obj;
    obj.vertices = {
        // x      y     z     u     v      nx    ny    nz    b0    b1    b2    b3    w0    w1    w2    w3
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    };
    obj.indices = { 0, 1, 2,  2, 3, 0 };
    obj.transform = transform;
    obj.world = transform.matrix();
    obj.texture = loadTexture(texPath);
    obj.indexCount = (GLsizei)obj.indices.size();
    obj.billboard = true;
    return obj;
}


// Build the model matrix for a Doom-style cylindrical billboard. The quad yaws to
// face the camera but stays vertical (no tilt when you look up/down).
glm::mat4 billboardModel(const glm::vec3& pos, float scale, const glm::vec3& camPos)
{
    // Direction from the sprite to the camera, flattened onto the XZ plane so the
    // sprite only spins about the vertical axis.
    glm::vec3 toCam = camPos - pos;
    toCam.y = 0.0f;
    toCam = glm::normalize(toCam);

    glm::vec3 up    = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, toCam));

    // Drop the basis vectors straight into the matrix columns (right, up, forward),
    // pre-scaled, with pos as the translation column. This is T * R * S in one step.
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right  * scale, 0.0f);
    model[1] = glm::vec4(up     * scale, 0.0f);
    model[2] = glm::vec4(toCam  * scale, 0.0f);
    model[3] = glm::vec4(pos,           1.0f);
    return model;
}


void uploadObject(Object &obj)
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

    glBindVertexArray(0); // stop recording (optional tidy-up)
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

    lightDirLoc     = glGetUniformLocation(shaderProgram, "LightDir");
    lightColorLoc   = glGetUniformLocation(shaderProgram, "LightColor");
    lightAmbientLoc = glGetUniformLocation(shaderProgram, "LightAmbient");

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

    window = glfwCreateWindow(SW, SH, "OpenGL", moniter, nullptr);
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


// Sample obj's baked clip at obj.animTime and fill `palette` with each bone's
// skinning matrix Aⱼ·Bⱼ⁻¹. No skeleton/clip -> identity palette (bind pose).
static void computePose(const Object& obj, std::vector<glm::mat4>& palette)
{
    const Skeleton&  sk = obj.skeleton;
    int n = (int)sk.inverseBind.size();
    palette.assign(n, glm::mat4(1.0f));
    if (n == 0 || obj.currentAnim < 0 || obj.currentAnim >= (int)obj.animations.size())
        return;
    const Animation& an = obj.animations[obj.currentAnim];
    if (an.frameCount == 0) return;

    // Which two frames to blend, and by how much.
    float dur = an.duration > 0.0f ? an.duration : 1.0f;
    float t   = std::fmod(obj.animTime, dur);
    if (t < 0.0f) t += dur;
    float frameF = t * an.fps;
    int   f0 = (int)std::floor(frameF) % an.frameCount;
    int   f1 = (f0 + 1) % an.frameCount;
    float a  = frameF - std::floor(frameF);

    // 1) interpolate local TRS -> local matrix (slerp for rotation, lerp for pos/scale)
    std::vector<glm::mat4> local(n);
    for (int b = 0; b < n; b++)
    {
        const BoneTrack& tr = an.tracks[b];
        glm::vec3 p = glm::mix (tr.pos[f0],   tr.pos[f1],   a);
        glm::quat q = glm::slerp(tr.rot[f0],  tr.rot[f1],   a);
        glm::vec3 s = glm::mix (tr.scale[f0], tr.scale[f1], a);
        local[b] = glm::translate(glm::mat4(1.0f), p)
                 * glm::mat4_cast(q)
                 * glm::scale(glm::mat4(1.0f), s);
    }

    // 2) walk the hierarchy -> world matrices. Bones aren't guaranteed parent-first,
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

    // 3) palette = animated world * inverse bind
    for (int b = 0; b < n; b++) palette[b] = world[b] * sk.inverseBind[b];
}


void drawObj(Object& obj)
{
    glm::mat4 model;
    if (obj.billboard) {
        // Rebuilt every frame so the quad faces the camera (Doom sprite). Pull
        // world position from the translation column and uniform scale from a
        // basis column length of the world matrix.
        glm::vec3 worldPos   = glm::vec3(obj.world[3]);
        float     worldScale = glm::length(glm::vec3(obj.world[0]));
        model = billboardModel(worldPos, worldScale, cameraPos);
    } else {
        // The world matrix already encodes translation, rotation, and scale.
        model = obj.world;
    }
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    // Skinned meshes advance their clip and upload a fresh bone palette. Unskinned
    // meshes leave the palette alone — their zero-weight verts take the shader's
    // pass-through branch, so stale palette data never touches them.
    if (!obj.skeleton.inverseBind.empty())
    {
        obj.animTime += deltaTime;
        std::vector<glm::mat4> palette;
        computePose(obj, palette);
        GLsizei count = (GLsizei)std::min((size_t)MAX_BONES, palette.size());
        glUniformMatrix4fv(boneMatricesLoc, count, GL_FALSE, glm::value_ptr(palette[0]));
    }

    glBindTexture(GL_TEXTURE_2D, obj.texture);
    glBindVertexArray(obj.VAO);
    glDrawElements(GL_TRIANGLES, obj.indexCount, GL_UNSIGNED_INT, 0);
}


void clearBG(float r, float g, float b, float a)
{   
    int vpW = std::round(SW * vps);
    int vpH = std::round(SH * vps);

    int vpX = (SW - vpW);
    int vpY = (SH - vpH);

    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shaderProgram);
    glViewport(vpX, vpY, vpW, vpH);

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
        
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),                 // vertical field of view
        (float)fbw / (float)fbh,             // aspect ratio
        0.1f, 100.0f);                       // near & far clip planes
    
    glm::mat4 view = glm::lookAt(
        cameraPos,         // camera position (Step 5 fills these in)
        cameraPos + cameraFront,         // look at the origin (where the triangle is)
        cameraUp);        // "up" direction

    glm::mat4 model = glm::mat4(1.0f);       // identity: triangle stays at the origin

    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc,       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(modelLoc,      1, GL_FALSE, glm::value_ptr(model));

    glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDir));
    glUniform3fv(lightColorLoc, 1, glm::value_ptr(lightColor));
    glUniform1f(lightAmbientLoc, lightAmbient);
    glActiveTexture(GL_TEXTURE0);
}

void Object::Upload()
{
    uploadObject(*this);
    for (Object* child : children) child->Upload();
}

void Object::SetAnimation(int index)
{
    if (index < 0 || index >= (int)animations.size()) return;
    currentAnim = index;
    animTime = 0.0f;   // restart the new clip from its first frame
}

bool Object::SetAnimation(const std::string& name)
{
    for (int i = 0; i < (int)animations.size(); i++)
    {
        if (animations[i].name == name)
        {
            SetAnimation(i);
            return true;
        }
    }
    return false;
}

void Object::Draw()
{
    drawObj(*this);
    for (Object* child : this->children)
    {
        child->world = this->world * child->transform.matrix();
        child->Draw();
    }
}

Object::~Object()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteTextures(1, &texture);
}