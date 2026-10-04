#=============================================================================
# Unreal SDLDrv makefile for Linux.
#
# Revision history:
# 	* Created by Daniel Vogel (based on XDrv).
# 
#=============================================================================

# Unreal directory. (Required by makefile-header.)
UNREAL_DIR	= ../..

# Include global definitions.
include $(UNREAL_DIR)/makefile-header

#-----------------------------------------------------------------------------
# Directories.
#-----------------------------------------------------------------------------

SRC_DIRS	= $(SDLDRV_SRC)
INC_DIRS    = $(SDLDRV_INC)
BUILD_DIR	= $(SDLDRV_BUILD)

#-----------------------------------------------------------------------------
# Compiler/linker options.
#-----------------------------------------------------------------------------

CXX		= $(STD_CXX)
CXXFLAGS    = $(STD_CXXFLAGS) $(EXT_CXXFLAGS) -I$(SDLDRV_INC) -I$(ENGINE_INC) \
              -I$(CORE_INC) -I$(SDL12_INC) -DGPackage=SDLDrv
LDFLAGS     = $(STD_LDFLAGS) $(EXT_LDFLAGS)$(SDLDRV_SONAME) $(SDL_LIBS)
LIBS        = $(STD12_LIBS)
AS          = $(STD_AS)
ASFLAGS     = $(STD_ASFLAGS)

#-----------------------------------------------------------------------------
# Files.
#-----------------------------------------------------------------------------

OBJS =			\
	SDLClient.o	\
	SDLDrv.o		\
	SDLViewport.o

OUT = $(SDLDRV)

#-----------------------------------------------------------------------------
# Rules.
#-----------------------------------------------------------------------------

# Use general rules.
include $(UNREAL_DIR)/makefile-general

#-----------------------------------------------------------------------------
# The End.
#-----------------------------------------------------------------------------
