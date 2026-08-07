#pragma once

#include "../graphics/graphics.h"


Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist);

void resolvePlayerCollision(Player& player);

void resolveEntityCollision(Entity& entity);

void collectSceneColliders();