# obj-creator build.
# Run from Git Bash:  mingw32-make        (build)
#                     mingw32-make run    (build + launch)
#                     mingw32-make clean  (remove the exe)
#
# We compile two translation units together:
#   src/game.cpp                 — your program
#   third_party/glad/src/glad.c  — the generated GL function loader
# and link against GLFW + the Windows OpenGL/GDI system libs.

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g \
            -Ithird_party/glad/include \
            -Ithird_party/glfw/include \
			-Ithird_party/ufbx \
            -Ithird_party
LDFLAGS  := -Lthird_party/glfw/lib
# Link order matters with static libs: glfw3 first, then the system libs it needs.
LDLIBS   := -lglfw3 -lopengl32 -lgdi32

SRCS := src/game.cpp src/graphics.cpp third_party/glad/src/glad.c third_party/ufbx/ufbx.c
TARGET := game.exe

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
