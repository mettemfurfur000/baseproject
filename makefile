CFLAGS += -O0 -Wall -g -MMD
LDFLAGS += -lm -g

# LDFLAGS += -lstdc++

# LDFLAGS += -lz -lunwind

# this trick is required for clang, since it doesn really know what to do with unix paths on windows (which is quite weird)
ifeq ($(OS),Windows_NT)
	MSYSINSTALLDIR := $(shell cd /;pwd -W)
	ROOTDIR := $(MSYSINSTALLDIR)$(CURDIR)
else
	ROOTDIR := $(CURDIR) 
endif

# enables including from various places 
CFLAGS += -I$(ROOTDIR)
CFLAGS += -I$(ROOTDIR)/libs
# not recommended but you could
# CFLAGS += -I$(ROOTDIR)/include

ifeq ($(OS),Windows_NT)
# for sdl builds
#	 CFLAGS += -IC:/msys64/mingw64/include/SDL2 
#	 CFLAGS += -Dmain=SDL_main
	
#	 LDFLAGS += -LC:/msys64/mingw64/lib
#	 LDFLAGS += -llua
	LDFLAGS += -lmingw32
#	 LDFLAGS += -lws2_32
#	 LDFLAGS += -lbacktrace
else
#	 CFLAGS += -I/usr/include/lua5.4/
#	 CFLAGS += -I/usr/include/SDL2

#	 LDFLAGS += -lbacktrace
endif

# general libraries 

# LDFLAGS += -lSDL2main -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer

# opengl stuff
ifeq ($(OS),Windows_NT)
#	 LDFLAGS += -lopengl32 -lepoxy.dll
#	 LDFLAGS += -lWinmm # sum weird time lib that enet uses
else
#	 LDFLAGS += -lGL -lepoxy -llua5.4
endif

SRCS_C := $(shell cd src;find . -name '*.c')
SRCS_CPP := $(shell cd src;find . -name '*.cpp')
SRCS := $(SRCS_C) $(SRCS_CPP)

OBJS_C := $(patsubst %.c,obj/%.o,$(SRCS_C))
OBJS_CPP := $(patsubst %.cpp,obj/%.o,$(SRCS_CPP))
OBJS := $(OBJS_C) $(OBJS_CPP)
OBJS := $(shell echo $(OBJS) | sed 's#/./#/#')

MAIN_C := $(shell cd mains;find . -name '*.c')

DEPS_MAIN_C := $(patsubst %.c,obj/%.d,$(MAIN_C))
DEPS_C := $(patsubst %.c,obj/%.d,$(SRCS_C))
DEPS_CPP := $(patsubst %.cpp,obj/%.d,$(SRCS_CPP))
DEPS := $(DEPS_C) $(DEPS_CPP) $(DEPS_MAIN_C)
# DEPS := $(shell echo $(DEPS) | sed 's#/./#/#')

all: copy_instance app

-include $(DEPS)

# our vector library
# .PHONY: vec
# vec:
# 	gcc -o obj/vec.o -c libs/vec/src/vec.c -Wall -Wextra -Ilibs/vec/src

.PHONY: copy_instance
copy_instance:
	mkdir -p build
	rsync -u --remove-source-files instance/ build/

obj/%.o : src/%.c
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	gcc $(CFLAGS) -c $< -o $@

obj/%.o : src/%.cpp
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	g++ -std=c++23 $(CFLAGS) -c $< -o $@

obj/mains/%.o : mains/%.c
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	gcc $(CFLAGS) -c $< -o $@

obj/mains/%.o : mains/%.cpp
	mkdir -p $(shell echo $@ | sed -r "s/(.+)\/.+/\1/")
	g++ -std=c++23 $(CFLAGS) -c $< -o $@

.PHONY: app
app: $(OBJS) obj/mains/main.o
	g++ ${CFLAGS} -o build/main obj/mains/main.o $< $(LDFLAGS)

# use this when packaging to get all the dll-s used
.PHONY: grab_dlls
grab_dlls:
	-./grab_dlls.sh build/app.exe /mingw64/bin 2

clean:
	rm -rf build/*
	rm -rf obj/*