#include "collisions.h"


std::vector<Tri> colliders;

// dont ask me how this function works Claude generated it.
//
// Möller–Trumbore for a single triangle, returning the hit distance rather than
// just whether it hit. Two-sided like rayOccluded(); on a hit in front of the
// origin it writes the distance along `dir` to `outT` and returns true. `dir`
// must be normalised for `outT` to be in world units.
static bool rayTriangle(const glm::vec3& origin, const glm::vec3& dir,
                        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                        float& outT)
{
    const float EPS = 1e-6f;

    glm::vec3 e1 = b - a;
    glm::vec3 e2 = c - a;
    glm::vec3 p  = glm::cross(dir, e2);
    float det = glm::dot(e1, p);
    if (glm::abs(det) < EPS) return false;   // ray runs parallel to the triangle

    float invDet = 1.0f / det;
    glm::vec3 tv = origin - a;

    float u = glm::dot(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(tv, e1);
    float v = glm::dot(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = glm::dot(e2, q) * invDet;
    if (t <= EPS) return false;              // behind or on the origin
    outT = t;
    return true;
}

// recursion half of the raycast walk. just here so meshes which 
// are children of empty nodes dont get left out. doesn't need to
// accept parentWorld as an argument because raycasts are shot
// after the first Compose() call, meaning the childrens world
// matrices are initialized.
void Object::Raycast(const glm::vec3& origin, const glm::vec3& dir,
                     float& closest, Object*& hit)
{
    for (Object*& child : children) child->Raycast(origin, dir, closest, hit);
}

// geometry half of the raycast walk. tests every triangle in the mesh
// by finding the triangle corner world positions and calling the
// Möller–Trumbore function to test if the ray hits it. setting the hit
// object as its own if the hit is the closest so far. then it calls the
// Object version of this function which contains the recursion half.
void Mesh::Raycast(const glm::vec3& origin, const glm::vec3& dir,
                   float& closest, Object*& hit)
{
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        glm::vec3 corner[3];
        for (int c = 0; c < 3; c++)
        {
            size_t v = (size_t)indices[i + c] * VERTEX_FLOATS;
            glm::vec3 local(vertices[v + 0], vertices[v + 1], vertices[v + 2]);
            corner[c] = glm::vec3(world * glm::vec4(local, 1.0f));
        }

        float t;
        if (rayTriangle(origin, dir, corner[0], corner[1], corner[2], t) && t < closest)
        {
            closest = t;
            hit = this;
        }
    }

    Object::Raycast(origin, dir, closest, hit);
}

Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist)
{
    glm::vec3 d = glm::normalize(dir);
    float closest = maxDist;
    Object* hit = nullptr;

    for (Object*& obj : parents) obj->Raycast(origin, d, closest, hit);

    return hit;
}

// recursion side same as CollectOccluders
void Object::CollectColliders(const glm::mat4& parentWorld, std::vector<Tri>& out)
{
    glm::mat4 world = parentWorld * transform.matrix();
    for (Object*& child : children) child->CollectColliders(world, out);
}

// same thing as CollectOccluders just checks for collides aswell
void Mesh::CollectColliders(const glm::mat4& parentWorld, std::vector<Tri>& out)
{
    glm::mat4 world = parentWorld * transform.matrix();

    if (isStatic && collides)
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

    Object::CollectColliders(parentWorld, out);
}

// initial call to collect scene colliders
void collectSceneColliders()
{   
    for (Object*& obj : parents) obj->CollectColliders(glm::mat4(1.0f), colliders);
}

// builds the player AABB.
static AABB playerBounds(const Player& player)
{
    glm::vec3 feet(player.transform.x, player.transform.y, player.transform.z);

    AABB box;
    box.min = feet - glm::vec3(player.radius, 0.0f, player.radius);
    box.max = feet + glm::vec3(player.radius, player.height, player.radius);
    return box;
}

// builds each static triangles AABB so it can be checked with the player
static AABB triBounds(const Tri& t)
{
    AABB box;
    box.min = glm::min(glm::min(t.a, t.b), t.c);
    box.max = glm::max(glm::max(t.a, t.b), t.c);
    return box;
}

// Closest point to `p` on the segment ab.  EX:           p
//                                          EX:        <-------a---------b------>
//
//                                      RESULT:        <-------p---------b------>

static glm::vec3 closestPointOnSegment(const glm::vec3& p,
                                       const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 ab = b - a;
    float len2 = glm::dot(ab, ab);
    if (len2 < 1e-12f) return a;   // degenerate segment: both ends are the same point

    return a + ab * glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
}

// dont ask me how this function works Claude generated it.
//
// Closest point to `p` on triangle t. The branches walk the triangle's Voronoi
// regions, so this is correct whether the nearest feature is the face interior,
// one of the three edges, or one of the three corners — which is exactly the
// distinction a box test can't make and why it runs second.
static glm::vec3 closestPointOnTriangle(const glm::vec3& p, const Tri& t)
{
    glm::vec3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;

    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return t.a;                   // corner A

    glm::vec3 bp = p - t.b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return t.b;                     // corner B

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)                 // edge AB
        return t.a + ab * (d1 / (d1 - d3));

    glm::vec3 cp = p - t.c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return t.c;                     // corner C

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)                 // edge AC
        return t.a + ac * (d2 / (d2 - d6));

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)   // edge BC
        return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    float denom = 1.0f / (va + vb + vc);                        // face interior
    return t.a + ab * (vb * denom) + ac * (vc * denom);
}

// the minimum y component of a triangles normal to be considered the ground
static const float GROUND_NORMAL_Y = 0.7f;

// resolves the players collisions with the static world.
//
// starts by checking if the players AABB overlaps with any of the triangle's 
// AABB's. if so it calculates the depth and direction of where the player is
// colliding and pushes player out of it. runs this check and push loop multiple
// times per frame just in case one triangle pushed player into another triangle.
// if tri normal y is greater than GROUND_NORMAL_Y then only push up for that 
// tri. (so player doesnt slide off slopes that humans could walk on)
void resolvePlayerCollision(Player& player)
{
    const int MAX_ITERATIONS = 4;
    const float radiusSq = player.radius * player.radius;

    player.grounded = false;

    for (int iter = 0; iter < MAX_ITERATIONS; iter++)
    {
        // Rebuilt per pass rather than per push: a push inside this pass leaves
        // the box slightly stale, and the next pass is what picks that up.
        AABB playerBox = playerBounds(player);
        playerBox.expand(0.01f);

        bool hitAny = false;

        for (const Tri& t : colliders)
        {
            if (!playerBox.overlaps(triBounds(t))) continue;

            glm::vec3 rawNormal = glm::cross(t.b - t.a, t.c - t.a);
            float normalLenSq = glm::dot(rawNormal, rawNormal);
            if (normalLenSq < 1e-12f) continue;   // degenerate tri, no surface to push off

            glm::vec3 faceNormal = rawNormal / glm::sqrt(normalLenSq);

            // The capsule's axis: the segment that, swept by `radius`, traces the
            // capsule exactly — so it is inset by the radius at each cap.
            glm::vec3 feet(player.transform.x, player.transform.y, player.transform.z);
            glm::vec3 base = feet + glm::vec3(0.0f, player.radius, 0.0f);
            glm::vec3 tip  = feet + glm::vec3(0.0f, player.height - player.radius, 0.0f);
            if (tip.y < base.y) tip = base;   // player wider than tall: degenerate to a sphere

            // Reduce capsule-vs-triangle to sphere-vs-triangle: find where the
            // axis crosses the triangle's plane, clamp that into the triangle,
            // then take the point on the axis nearest it as the sphere center.
            glm::vec3 axis = tip - base;
            float denom = glm::dot(faceNormal, axis);

            glm::vec3 reference;
            if (glm::abs(denom) < 1e-6f)
                reference = t.a;   // axis parallel to the plane; any point on it serves
            else
                reference = base + axis * glm::clamp(glm::dot(faceNormal, t.a - base) / denom,
                                                     0.0f, 1.0f);

            reference = closestPointOnTriangle(reference, t);

            glm::vec3 center  = closestPointOnSegment(reference, base, tip);
            glm::vec3 contact = closestPointOnTriangle(center, t);

            glm::vec3 delta = center - contact;
            float distSq = glm::dot(delta, delta);
            if (distSq >= radiusSq) continue;   // clear of this triangle

            glm::vec3 pushDir;
            float depth;
            if (distSq > 1e-12f)
            {
                float dist = glm::sqrt(distSq);
                pushDir = delta / dist;
                depth   = player.radius - dist;
            }
            else
            {
                // Center landed exactly on the surface, so `delta` carries no
                // direction. Fall back to the face normal.
                pushDir = faceNormal;
                depth   = player.radius;
            }
            
            player.transform.y += pushDir.y * depth;
            
            if (pushDir.y > GROUND_NORMAL_Y) {
                player.grounded = true;
                // Cancel only the component of motion heading into the surface for y
                player.velocity.y -= pushDir.y * glm::min(0.0f, glm::dot(player.velocity.y, pushDir.y));
            } else {
                // only push player on x and z if its not a ground tri
                player.transform.x += pushDir.x * depth;
                player.transform.z += pushDir.z * depth;

                // Cancel only the component of motion heading into the surface
                player.velocity -= pushDir * glm::min(0.0f, glm::dot(player.velocity, pushDir));
                
            }
            hitAny = true;
        }

        if (!hitAny) break;
    }
}