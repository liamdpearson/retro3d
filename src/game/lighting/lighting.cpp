#include "lighting.h"

// dont ask me how this function works Claude generated it.
//
// Möller–Trumbore. Two-sided on purpose: the back face of a closed mesh blocks
// light just as well as the front, so there's no winding cull here. Returns on
// the first hit — a shadow ray only cares *whether* something blocks, not what.
static bool rayOccluded(const glm::vec3& origin, const glm::vec3& dir,
                        float maxDist, const std::vector<Tri>& tris)
{
    const float EPS = 1e-6f;

    for (const Tri& t : tris)
    {
        glm::vec3 e1 = t.b - t.a;
        glm::vec3 e2 = t.c - t.a;
        glm::vec3 p  = glm::cross(dir, e2);
        float det = glm::dot(e1, p);
        if (glm::abs(det) < EPS) continue;   // ray runs parallel to the triangle

        float invDet = 1.0f / det;
        glm::vec3 tv = origin - t.a;

        float u = glm::dot(tv, p) * invDet;
        if (u < 0.0f || u > 1.0f) continue;

        glm::vec3 q = glm::cross(tv, e1);
        float v = glm::dot(dir, q) * invDet;
        if (v < 0.0f || u + v > 1.0f) continue;

        float hit = glm::dot(e2, q) * invDet;
        if (hit > EPS && hit < maxDist) return true;   // blocked before the light
    }
    return false;
}


// shoots rays at each light in the scene and adds the 
glm::vec3 sampleLightAt(const glm::vec3& p)
{
    glm::vec3 lit(lightAmbient);

    for (const Light* light : lights)
    {
        glm::vec3 toLight = light->pos - p;
        float dist = glm::length(toLight);
        glm::vec3 l = toLight / dist;
        
        if (rayOccluded(p, l, dist, occluders)) continue;

        float atten = light->intensity / (1.0f + (dist * dist / light->radius));
        lit += light->color * atten;
    }

    // if r, g, or b is over 1.0f then divide them all by the largest one. this
    // will force the values between 0 and 1 while still preserving the hue
    float peak = glm::max(lit.r, glm::max(lit.g, lit.b));
    if (peak > 1.0f) lit /= peak;

    return lit;
}

// the recursion half of the occluders pass
void Object::CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out)
{
    glm::mat4 world = parentWorld * transform.matrix();
    for (Object*& child : children) child->CollectOccluders(world, out);
}

// the geometry half of the occluders pass
void Mesh::CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out)
{
    glm::mat4 world = parentWorld * transform.matrix();

    if (isStatic)
    {
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            Tri t;
            glm::vec3* corner[3] = { &t.a, &t.b, &t.c };
            for (int c = 0; c < 3; c++)
            {
                size_t v = (size_t)indices[i + c] * VERTEX_FLOATS;
                glm::vec3 local(vertices[v + 0], vertices[v + 1], vertices[v + 2]);
                *corner[c] = glm::vec3(world * glm::vec4(local, 1.0f));
            }
            out.push_back(t);
        }
    }

    // pass parentWorld not world because you already apply the local transform
    // matrix above and you must apply it in the Object version because not every
    // object is a mesh.
    Object::CollectOccluders(parentWorld, out);
}

// the recursion half of the lighting bake
void Object::BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders)
{
    glm::mat4 world = parentWorld * transform.matrix();
    for (Object*& child : children) child->BakeLighting(world, occluders);
}

// the geometry half of the lighting bake
void Mesh::BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders)
{
    glm::mat4 world = parentWorld * transform.matrix();

    if (isStatic)
    {
        // create normal transformation matrix which is to guard against non-uniform scaling
        // altering the normals. however, currently non-uniform scaling isnt in this engine.
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));

        for (size_t v = 0; v + VERTEX_FLOATS <= vertices.size(); v += VERTEX_FLOATS)
        {
            glm::vec3 localPos(vertices[v + 0], vertices[v + 1], vertices[v + 2]);
            glm::vec3 localNrm(vertices[v + 5], vertices[v + 6], vertices[v + 7]);

            glm::vec3 worldPos = glm::vec3(world * glm::vec4(localPos, 1.0f)); // get world pos
            glm::vec3 n = glm::normalize(normalMat * localNrm);                // get world norm

            glm::vec3 lit(lightAmbient);

            for (const Light* light : lights)
            {
                glm::vec3 toLight = light->pos - worldPos;
                float dist = glm::length(toLight);
                if (dist < 0.0001f) continue;

                glm::vec3 l = toLight / dist;
                float lambert = glm::max(glm::dot(n, l), 0.0f);
                if (lambert <= 0.0f) continue;   // facing away, no contribution

                // lift ray off surface so vert doesnt hit itself
                glm::vec3 origin = worldPos + n * SHADOW_BIAS;
                if (rayOccluded(origin, l, dist, occluders)) continue;

                // 1 + d² rather than d² so a light sitting on a vertex doesn't
                // divide by zero. Not physical, but stable and easy to tune.
                float atten = light->intensity / (1.0f + (dist * dist / light->radius));

                lit += light->color * lambert * atten;
            }

            // hue preservation see sample light at
            float peak = glm::max(lit.r, glm::max(lit.g, lit.b));
            if (peak > 1.0f) lit /= peak;

            vertices[v + 16] = lit.r;
            vertices[v + 17] = lit.g;
            vertices[v + 18] = lit.b;
        }

        std::fprintf(stderr, "  baked %zu verts against %zu lights\n",
                     vertices.size() / VERTEX_FLOATS, lights.size());
    }

    // parentWorld not world. see the note in Mesh::CollectOccluders().
    Object::BakeLighting(parentWorld, occluders);
}

// bakes the scene's static objects and initializes the light grid for dynamic 
// objects. call this after scene graph is assembled and lights are placed but 
// before the Upload() call because this bakes the light level into the vertices 
// which need to then get uploaded. serves as the starting point for the 
// CollectOccluders and BakeLighting node walks.
void bakeSceneLighting()
{
    double start = glfwGetTime();

    for (Object*& obj : parents) obj->CollectOccluders(glm::mat4(1.0f), occluders);

    std::fprintf(stderr, "bake: %zu occluder tris\n", occluders.size());

    for (Object*& obj : parents) obj->BakeLighting(glm::mat4(1.0f), occluders);

    // create light grid dimensions for dynamic objects
    for (Tri tri : occluders)
    {
        std::vector<glm::vec3> vertices{tri.a, tri.b, tri.c};
        for (glm::vec3& v : vertices)
        {
            if (v.x < minX) minX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.z < minZ) minZ = v.z;

            if (v.x > maxX) maxX = v.x;
            if (v.y > maxY) maxY = v.y;
            if (v.z > maxZ) maxZ = v.z;
        }
    }

    std::cout << "Scene Dimensions(-X X -Y Y -Z Z):\n";
    std::cout << minX << ' ' << maxX << ' ' << minY << ' ' << maxY << ' ' << minZ << ' ' << maxZ << '\n';

    minX = floor(minX) - 1; minY = floor(minY) - 1; minZ = floor(minZ) - 1;
    maxX = floor(maxX) + 2; maxY = floor(maxY) + 2; maxZ = floor(maxZ) + 2;

    std::cout << "Light Grid Dimensions(-X X -Y Y -Z Z):\n";
    std::cout << minX << ' ' << maxX << ' ' << minY << ' ' << maxY << ' ' << minZ << ' ' << maxZ << '\n';

    // build light grid
    for (int x = minX; x < maxX + 1; x++)
    {
        for (int y = minY; y < maxY + 1; y++)
        {
            for (int z = minZ; z < maxZ + 1; z++)
            {
                glm::vec3 lit = sampleLightAt(glm::vec3{x, y, z});
                lightGrid.push_back(lit);
            }
        }
    }
    
    std::fprintf(stderr, "bake: done in %.2fs\n", glfwGetTime() - start);
}