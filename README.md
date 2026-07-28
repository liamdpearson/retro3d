# Retro3D Game Engine
A simple game engine to make games that look like they are from the 90's

## How to Use

### Create your Scene
Add your scene objects inside of the "scene" list in scene.json.

The supported types are as follows:
1. "pivot" - An invisible object meant to group together its parents children.
2. "mesh obj" - An object with a mesh from an obj file. Cannot have animations.
3. "mesh fbx" - An object with a mesh from an fbx file. Can be animated.
4. "light" - A global light. Cannot be a parent or a child of another object.
5. "camera" - Each scene should only have one. Controls where the camera starts.

Here is an example of a scene:

*scene image* use your imagination for now

