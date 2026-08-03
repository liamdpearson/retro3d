#include "lighting.h"


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

// Sample every light at a single world-space point. Deliberately has no normal
// term: the caller applies one value to a whole mesh, so there is no surface to
// take a lambert against. Brightness therefore falls off with distance alone.
// Keep the attenuation curve identical to bakeObjectLighting()'s, or movers and
// the baked floor beneath them will disagree about how bright the room is.
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

    // Normalize by the brightest channel rather than clamping each one: a
    // per-channel min() pulls the channels toward each other, so an overbright
    // yellow light washes out to white. Scaling keeps the hue and spends the
    // overflow on brightness instead. Must match Mesh::BakeLighting()'s clamp.
    float peak = glm::max(lit.r, glm::max(lit.g, lit.b));
    if (peak > 1.0f) lit /= peak;

    return lit;
}

// Pass 1, recursion half. A mesh-less node contributes no triangles; it still
// composes its transform into the world matrix its children are gathered with,
// so a pivot above static geometry moves that geometry's shadows with it.
void Object::CollectOccluders(const glm::mat4& parentWorld, std::vector<Tri>& out)
{
    glm::mat4 world = parentWorld * transform.matrix();
    for (Object*& child : children) child->CollectOccluders(world, out);
}

// Pass 1, geometry half. Flatten every static triangle in the scene into world
// space. Occlusion is a scene-wide property, so this must run over the whole
// graph before any vertex is baked. Dynamic objects are deliberately skipped:
// they move, and a shadow baked from them would not.
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

    // Chain with parentWorld, NOT the composed world above: the base recomposes
    // this node's transform itself. Passing `world` here would apply this mesh's
    // transform twice to every descendant — silent, and geometrically plausible.
    Object::CollectOccluders(parentWorld, out);
}

// Pass 2, recursion half. See CollectOccluders() above for why this composes.
void Object::BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders)
{
    glm::mat4 world = parentWorld * transform.matrix();
    for (Object*& child : children) child->BakeLighting(world, occluders);
}

// Pass 2, geometry half. Bake per-vertex irradiance into the last 3 floats of
// every static vertex. parentWorld mirrors the composition Draw() does, so
// static children of a static parent bake in their true world position.
void Mesh::BakeLighting(const glm::mat4& parentWorld, const std::vector<Tri>& occluders)
{
    glm::mat4 world = parentWorld * transform.matrix();

    if (isStatic)
    {
        // Inverse-transpose so normals survive any non-uniform scale baked into
        // the geometry by loadFBX. Transform only ever applies uniform scale, but
        // this costs nothing here and is correct either way.
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));

        for (size_t v = 0; v + VERTEX_FLOATS <= vertices.size(); v += VERTEX_FLOATS)
        {
            glm::vec3 localPos(vertices[v + 0], vertices[v + 1], vertices[v + 2]);
            glm::vec3 localNrm(vertices[v + 5], vertices[v + 6], vertices[v + 7]);

            glm::vec3 worldPos = glm::vec3(world * glm::vec4(localPos, 1.0f));
            glm::vec3 n = glm::normalize(normalMat * localNrm);

            glm::vec3 lit(lightAmbient);

            for (const Light* light : lights)
            {
                glm::vec3 toLight = light->pos - worldPos;
                float dist = glm::length(toLight);
                if (dist < 0.0001f) continue;

                glm::vec3 l = toLight / dist;
                float lambert = glm::max(glm::dot(n, l), 0.0f);
                if (lambert <= 0.0f) continue;   // facing away, no contribution

                // Lift the ray off the surface before firing it. Without this the
                // vertex re-hits the very triangles it sits on and every surface
                // shadows itself — the classic acne speckle.
                glm::vec3 origin = worldPos + n * SHADOW_BIAS;
                if (rayOccluded(origin, l, dist, occluders)) continue;

                // 1 + d² rather than d² so a light sitting on a vertex doesn't
                // divide by zero. Not physical, but stable and easy to tune.
                float atten = light->intensity / (1.0f + (dist * dist / light->radius));

                lit += light->color * lambert * atten;
            }

            // Hue-preserving clamp — see the note in sampleLightAt(), and keep
            // the two identical or a mover will tint differently to the floor
            // it stands on wherever the light overflows.
            float peak = glm::max(lit.r, glm::max(lit.g, lit.b));
            if (peak > 1.0f) lit /= peak;

            vertices[v + 16] = lit.r;
            vertices[v + 17] = lit.g;
            vertices[v + 18] = lit.b;
        }

        std::fprintf(stderr, "  baked %zu verts against %zu lights\n",
                     vertices.size() / VERTEX_FLOATS, lights.size());
    }

    // parentWorld, not world — see the note in Mesh::CollectOccluders().
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