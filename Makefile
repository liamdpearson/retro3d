# Engine build.
#
# Works from Git Bash, PowerShell or cmd — see "shell portability" below for why
# that needs saying. Always run it from the repo root though: asset and scene
# paths are relative to the working directory.
#
#   mingw32-make                  — build both exes (game.exe + editor.exe)
#   mingw32-make game             — build game.exe only
#   mingw32-make editor           — build editor.exe only
#   mingw32-make run              — build + launch game.exe
#   mingw32-make run-editor       — build + launch editor.exe
#   mingw32-make clean            — remove both exes and all object files
#
# Add -j8 (or however many cores you have) to build in parallel — now that each
# source compiles to its own object file, that is close to a linear speedup.
#
# Each exe compiles its own entry point plus the engine + vendored deps:
#   src/engine/*                   — the shared engine (game only, for now)
#   src/editor/editor_graphics.cpp — the editor's own engine copy
#   third_party/glad/src/glad.c    — the generated GL function loader
#   third_party/ufbx/ufbx.c        — the FBX loader
#   third_party/miniaudio/miniaudio.c — the audio backend
# and links against GLFW + the Windows OpenGL/GDI system libs.
#
# Objects land in build/, mirroring the source path (build/src/game/game.cpp.o)
# so two sources with the same basename in different directories can't collide.
# The vendored objects in COMMON_OBJS are built once and linked into both exes.

CXX      := g++
CXXFLAGS := -std=c++17 -g \
            -Ithird_party/glad/include \
            -Ithird_party/glfw/include \
            -Ithird_party/miniaudio \
            -Ithird_party/ufbx \
            -Ithird_party

# Kept separate from CXXFLAGS so the third_party objects can switch it off
# below — miniaudio and ufbx are large amalgamated C files and their warnings
# would otherwise bury the ones from our own code.
WARNINGS := -Wall -Wextra

# -MMD writes a .d file listing each object's headers; -MP adds a phony target
# per header so deleting one doesn't wedge the build. Without this, editing
# graphics.h would leave every object that includes it stale — a silent failure.
DEPFLAGS := -MMD -MP

LDFLAGS  := -Lthird_party/glfw/lib
# Link order matters with static libs: glfw3 first, then the system libs it needs.
LDLIBS   := -lglfw3 -lopengl32 -lgdi32

BUILD_DIR := build

# --- sources --- #

# Vendored deps, compiled identically for both exes so the objects are shared.
COMMON_SRCS := third_party/glad/src/glad.c \
               third_party/ufbx/ufbx.c \
               third_party/miniaudio/miniaudio.c

ENGINE_SRCS := src/engine/graphics/graphics.cpp \
               src/engine/lighting/lighting.cpp \
               src/engine/collisions/collisions.cpp \
               src/engine/audio/audio.cpp

GAME_SRCS   := src/game/game.cpp $(ENGINE_SRCS)
EDITOR_SRCS := src/editor/editor.cpp src/editor/editor_graphics.cpp

# --- objects --- #

# .o is appended to the full source name rather than replacing the extension,
# so foo.c and foo.cpp in one directory would map to distinct objects.
COMMON_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
GAME_OBJS   := $(patsubst %,$(BUILD_DIR)/%.o,$(GAME_SRCS))   $(COMMON_OBJS)
EDITOR_OBJS := $(patsubst %,$(BUILD_DIR)/%.o,$(EDITOR_SRCS)) $(COMMON_OBJS)

ALL_OBJS := $(sort $(GAME_OBJS) $(EDITOR_OBJS))
DEPS     := $(ALL_OBJS:.o=.d)

GAME_TARGET   := game.exe
EDITOR_TARGET := editor.exe

# --- shell portability --- #
#
# mingw32-make runs recipes through sh.exe only when it can find one on PATH.
# Launched from Git Bash it does; launched from PowerShell or cmd it silently
# falls back to cmd.exe, where `mkdir -p build/src/game` fails with "The syntax
# of the command is incorrect" — cmd's mkdir has no -p and reads a forward slash
# as a switch. Git Bash leaves SHELL as a full path, the cmd fallback leaves it
# as a bare "sh.exe", so the presence of a slash tells us which one we got.
#
# Both branches use `=` not `:=` so $@ expands per-recipe rather than here.
ifeq ($(findstring /,$(SHELL)),)
    OBJDIR = $(subst /,\,$(patsubst %/,%,$(dir $@)))
    MKDIR  = @if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"
    CLEAN  = @if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
    CLEAN += & if exist $(GAME_TARGET) del /q $(GAME_TARGET)
    CLEAN += & if exist $(EDITOR_TARGET) del /q $(EDITOR_TARGET)
else
    MKDIR = @mkdir -p $(dir $@)
    CLEAN = @rm -rf $(BUILD_DIR) $(GAME_TARGET) $(EDITOR_TARGET)
endif

# --- rules --- #

all: game editor

game: $(GAME_TARGET)

editor: $(EDITOR_TARGET)

$(GAME_TARGET): $(GAME_OBJS)
	$(CXX) $(GAME_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(EDITOR_TARGET): $(EDITOR_OBJS)
	$(CXX) $(EDITOR_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# The .c sources are compiled with $(CXX), not $(CC) — g++ treats .c as C++,
# which is how glad/ufbx have always been built here. miniaudio compiles clean
# as C++ too. Switching them to gcc is a behaviour change, not a cleanup.
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR)
	$(CXX) $(CXXFLAGS) $(WARNINGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR)
	$(CXX) $(CXXFLAGS) $(WARNINGS) $(DEPFLAGS) -c $< -o $@

# Silence -Wall -Wextra for the vendored objects only. Drop this line to get
# the old behaviour back.
$(COMMON_OBJS): WARNINGS :=

run: $(GAME_TARGET)
	./$(GAME_TARGET)

run-editor: $(EDITOR_TARGET)
	./$(EDITOR_TARGET)

clean:
	$(CLEAN)

# Pulls in the generated header dependencies. Leading '-' so the first build,
# when no .d files exist yet, doesn't warn.
-include $(DEPS)

.PHONY: all game editor run run-editor clean
