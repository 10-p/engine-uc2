#=============================================================================
# Unreal GUI makefile for non-Windows platforms.
#=============================================================================

# Unreal directory. (Required by makefile-header.)
UNREAL_DIR		= ../..

# Include global definitions.
include $(UNREAL_DIR)/makefile-header

#-----------------------------------------------------------------------------
# Directories.
#-----------------------------------------------------------------------------

SRC_DIRS		= $(GUI_SRC)
INC_DIRS		= $(GUI_INC)
BUILD_DIR		= $(GUI_BUILD)

#-----------------------------------------------------------------------------
# Compiler/linker options.
#-----------------------------------------------------------------------------

CXX			= $(STD_CXX)
CXXFLAGS		= $(STD_CXXFLAGS) $(EXT_CXXFLAGS) -I$(GUI_INC) \
                    -I$(CORE_INC) -I$(ENGINE_INC) -I$(XGAME_INC) \
                    -I$(IPDRV_INC) \
                    -DGPackage=GPackageGUI -DThisPackage=GUI
LDFLAGS			= $(STD_LDFLAGS) $(EXT_LDFLAGS)$(GUI_SONAME)
LIBS			= $(STD_LIBS) $(CORE) $(IPDRV) $(XGAME)
AS			= $(STD_AS)
ASFLAGS			= $(STD_ASFLAGS)

#-----------------------------------------------------------------------------
# Files.
#-----------------------------------------------------------------------------

OBJS =				\
	GUI.o    		\
	UnGUICore.o  \
    UnGUI.o

OUT = $(GUI)

#-----------------------------------------------------------------------------
# Rules.
#-----------------------------------------------------------------------------

# Use general rules.
include $(UNREAL_DIR)/makefile-general

#-----------------------------------------------------------------------------
# The End.
#-----------------------------------------------------------------------------
