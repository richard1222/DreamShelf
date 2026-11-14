PS3DEV ?= /opt/ps3sdk
PSL1GHT ?= $(PS3DEV)
PORTLIBS := $(PS3DEV)/portlibs/ppu
APPID := THMS00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
CXX := $(PS3DEV)/ppu/bin/ppu-g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL2 -Isrc -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib -Wl,--gc-sections
LIBS := -lSDL2 -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnet -lgcm_sys -lrsx -lsysutil -lio -laudio -lrt -llv2 -lsysmodule -lsysfs -lm
SOURCES := src/main.cxx src/ui.cxx src/catalog.cxx src/catalog_cache.cxx src/common/pad.cxx src/common/watchdog.cxx src/launcher/font_text.cxx
OBJECTS := $(SOURCES:src/%.cxx=build/%.o)
.PHONY: all pkg
all: build/themeshelf.elf
build/%.o: src/%.cxx
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/themeshelf.elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@
pkg: build/DreamShelf-v0.1.4.pkg
build/DreamShelf-v0.1.4.pkg: build/themeshelf.elf assets/ICON0.PNG assets/cacert.pem LICENSE README.md
	mkdir -p build/pkg/USRDIR
	cp assets/ICON0.PNG build/pkg/ICON0.PNG
	cp assets/cacert.pem LICENSE README.md build/pkg/USRDIR/
	ppu-strip $< -o build/themeshelf.strip.elf
	sprxlinker build/themeshelf.strip.elf
	make_self_npdrm build/themeshelf.strip.elf build/pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	sfo --title "DreamShelf" --appid "$(APPID)" -f $(PS3DEV)/bin/sfo.xml build/pkg/PARAM.SFO
	pkg --contentid $(CONTENTID) build/pkg/ build/DreamShelf-v0.1.4.debug.pkg
	cp build/DreamShelf-v0.1.4.debug.pkg $@
	package_finalize $@
-include $(OBJECTS:.o=.d)
