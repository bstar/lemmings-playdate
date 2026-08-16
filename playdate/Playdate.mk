HEAP_SIZE = 8388208
STACK_SIZE = 61800
PRODUCT ?= Lemmings.pdx
SDK = $(PLAYDATE_SDK_PATH)
VPATH += src:core
SRC = src/main.c core/lp_pack.c core/lp_game.c core/lp_music.c core/lp_adlib.c \
	core/lp_progress.c
CPP_SRC = core/lp_dbopl.cpp core/dbopl_core.cpp
UINCDIR = core
UASRC =
UDEFS =
UADEFS =
ULIBDIR =
ULIBS = -Wl,--start-group -lstdc++ -lm -lc -lgcc -lnosys -Wl,--end-group
include $(SDK)/C_API/buildsupport/common.mk

# The SDK's legacy common.mk only knows C sources. Freeze its C object list,
# then add the C++ DBOPL objects while still appending their sources to the
# one-shot Simulator link.
CPP_OBJS := $(addprefix $(OBJDIR)/,$(CPP_SRC:.cpp=.o))
OBJS := $(OBJS) $(CPP_OBJS)
SRC += $(CPP_SRC)
CXX = $(GCC)$(TRGT)g++ -g3
# The SDK's one-shot Simulator link omits $(DEFS), so UDEFS would reach only
# the device build. Carry it on the compiler itself to keep both consistent.
SIMCOMPILER = clang -g $(UDEFS)

$(OBJDIR)/%.o : %.cpp | MKOBJDIR MKDEPDIR
	mkdir -p `dirname $@`
	$(CXX) -std=c++14 -fno-exceptions -fno-rtti -c \
		$(filter-out -Wstrict-prototypes,$(CPFLAGS)) -O3 -I . $(INCDIR) $< -o $@

# common.mk expanded the ELF prerequisites before CPP_OBJS existed.
$(OBJDIR)/pdex.elf: $(CPP_OBJS)
