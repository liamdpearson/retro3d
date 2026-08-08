#pragma once

#include "../graphics/graphics.h"


Object* raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist);

bool rayBlockedA(const glm::vec3& origin, const glm::vec3& dir,
                 float maxDist, const std::vector<TriAABB>& tris);

bool rayBlockedB(const glm::vec3& origin, const glm::vec3& destination,
                 const std::vector<TriAABB>& tris);

bool rayHitEntity(const glm::vec3& origin, const glm::vec3& dir,
                  const Entity& entity, float maxDist);

void collectSceneColliders();

void resolvePlayerCollision(Player& player);

void resolveEntityCollision(Entity& entity);

