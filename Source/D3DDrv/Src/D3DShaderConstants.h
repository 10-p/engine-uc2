/*=============================================================================
	D3DShaderConstants.h: vertex shader constant register defines.
	Copyright 2003 Scion Studios, Inc. All Rights Reserved.

	Revision history:
		* Created by Sam Zamani
=============================================================================*/

#ifndef _SHADER_CONSTANTS_H
#define _SHADER_CONSTANTS_H

#ifdef _XBOX

#include "ShaderConstants.vsh"

#else

#define VSC_BONEBASE			18
#define VSC_FIXEDCONST0			0
#define VSC_CLIP_MATRIX			1
#define VSC_AMBIENT_COLOR		5
#define VSC_DISTORTION_SCALE	6
#define VSC_PROJ_DIRECTIONS		6

#define VSC_LIGHT_BASE				7
#define VSC_LIGHT_DIR_OFFSET		0
#define VSC_LIGHT_POS_OFFSET		1
#define VSC_LIGHT_DIFFUSE_OFFSET	2
#define VSC_LIGHT_SIZE				3
#define VSC_HARD_LIGHT_BASE			VSC_LIGHT_BASE

#define VSC_MVIEW_MATRIX			11

#define VSC_COLOR_REMAP 6

#endif //_XBOX

#endif //_SHADER_CONSTANTS_H