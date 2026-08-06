#pragma once

#include "../graphics/graphics.h"


extern std::vector<Tri> colliders;

Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist);

void resolvePlayerCollision(Player& player);

void collectSceneColliders();