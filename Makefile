# Engine build.
# Run from Git Bash:
#   mingw32-make                  — build both exes (game.exe + scene_editor.exe)
#   mingw32-make game             — build game.exe only
#   mingw32-make scene_editor     — build scene_editor.exe only
#   mingw32-make run              — build + launch game.exe
#   mingw32-make run-editor       — build + launch scene_editor.exe
#   mingw32-make clean            — remove both exes
#
# Each exe compiles its own entry point plus the shared engine + vendored deps:
#   src/graphics.cpp             — the engine
#   third_party/glad/src/glad.c  — the generated GL function loader
#   third_party/ufbx/ufbx.c      — the FBX loader
# and links against GLFW + the Windows OpenGL/GDI system libs.

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g \
            -Ithird_party/glad/include \
            -Ithird_party/glfw/include \
			-Ithird_party/ufbx \
            -Ithird_party
LDFLAGS  := -Lthird_party/glfw/lib
# Link order matters with static libs: glfw3 first, then the system libs it needs.
LDLIBS   := -lglfw3 -lopengl32 -lgdi32

# Shared engine + vendored deps compiled into both exes.
COMMON_SRCS := src/graphics.cpp third_party/glad/src/glad.c third_party/ufbx/ufbx.c

GAME_SRCS   := src/game.cpp $(COMMON_SRCS)
GAME_TARGET := game.exe

EDITOR_SRCS   := src/scene_editor.cpp $(COMMON_SRCS)
EDITOR_TARGET := scene_editor.exe

all: game scene_editor

game: $(GAME_TARGET)

scene_editor: $(EDITOR_TARGET)

$(GAME_TARGET): $(GAME_SRCS)
	$(CXX) $(CXXFLAGS) $(GAME_SRCS) -o $(GAME_TARGET) $(LDFLAGS) $(LDLIBS)

$(EDITOR_TARGET): $(EDITOR_SRCS)
	$(CXX) $(CXXFLAGS) $(EDITOR_SRCS) -o $(EDITOR_TARGET) $(LDFLAGS) $(LDLIBS)

run: $(GAME_TARGET)
	./$(GAME_TARGET)

run-editor: $(EDITOR_TARGET)
	./$(EDITOR_TARGET)

clean:
	rm -f $(GAME_TARGET) $(EDITOR_TARGET)

.PHONY: all game scene_editor run run-editor clean
