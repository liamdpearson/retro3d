#pragma once

#include "../graphics/graphics.h"

// checks every single triangle in the scene even non static objects for hits
// returns the closest hit within a maxDist. takes in an origin point and a
// direction vector this is the start of the raycast walk above.
Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist);

// Push the player's capsule out of anything in `colliders` it overlaps, and set
// `grounded` if it came to rest on a floor-ish surface. Call once per frame,
// after this frame's movement has been applied and before Compose(), so the
// camera hanging off the player follows the corrected position in the same frame.
void resolvePlayerCollision(Player& player);

void collectSceneColliders();