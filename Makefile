


.PHONY : shaders app clean

SOURCE_FILES = main.cpp
LINK_DIRS = just-a-vulkan-library C:/Program Files/MSYS2/mingw64/x86_64-w64-mingw32/lib
LINK_LIBS = JAVL glfw3dll
CPP_FLAGS = -std=c++17 -g -static
OUT_FILENAME = fluid_sim.exe

SHADER_DIR=shaders_fluid

CURRENT_PROJECT_FILES = $(wildcard *.cpp *.h)


all : app shaders


just-a-vulkan-library/libJAVL.a : just-a-vulkan-library/vulkan_include_all.h just-a-vulkan-library/*/*
	cd just-a-vulkan-library/.obj && $(MAKE) all


ACCEPTED_SHADER_EXTS = .comp .frag .vert .tese .tesc .geom
SHADER_WILDCARD_STRINGS = $(addprefix $(SHADER_DIR)/*/*, $(ACCEPTED_SHADER_EXTS))
$(info $(wildcard $(SHADER_WILDCARD_STRINGS)))

shaders : $(wildcard $(SHADER_WILDCARD_STRINGS))
	cd $(SHADER_DIR) && python build_shaders.py

$(OUT_FILENAME) : just-a-vulkan-library/libJAVL.a $(CURRENT_PROJECT_FILES)
	g++ $(SOURCE_FILES) $(CPP_FLAGS) $(addprefix -L, $(LINK_DIRS)) $(addprefix -l, $(LINK_LIBS)) -o $(OUT_FILENAME)

app : $(OUT_FILENAME)

ALL_SHADER_BINARIES = $(subst /,\,$(addsuffix ", $(addprefix ", $(wildcard $(SHADER_DIR)/*/*.spv))))

clean :
	if exist $(OUT_FILENAME) del $(OUT_FILENAME)
ifneq ($(ALL_SHADER_BINARIES),)
	del /q $(ALL_SHADER_BINARIES)
endif
