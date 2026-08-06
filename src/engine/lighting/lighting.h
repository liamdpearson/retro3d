#pragma once

#include "../graphics/graphics.h"


extern std::vector<Tri> occluders;

// sample all lights at one world-space point, for lighting a whole mover at once.
glm::vec3 sampleLightAt(const glm::vec3& p);

// bakes the scene's static objects and initializes the light grid for dynamic 
// objects. call this after scene graph is assembled and lights are placed but 
// before the Upload() call because this bakes the light level into the vertices 
// which need to then get uploaded. serves as the starting point for the 
// CollectOccluders and BakeLighting node walks.
void bakeSceneLighting();