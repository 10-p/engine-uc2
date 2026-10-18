/*=============================================================================
	UC2GLRawFuncs.h: entry points of the GL library under the binding.

	For the desktop binding these are the same functions as the main table's. For the GL4ES
	binding they are OpenGL ES / WebGL itself, called around gl4es for the things gl4es does
	not do the way this driver needs: depth-stencil storage and its attachment to a
	framebuffer, the engine's texture uploads, gl4es' "texture 0" stand-in, the hardware's
	own extension string and shader precision. Every use restores the raw state it changes
	(gl4es caches bindings), e.g. UUC2GLRenderDevice::CreateDepthStencil*.

	GL_RAW(ret,func,parms) declares UUC2GLRenderDevice::Raw_<func>.
=============================================================================*/

GL_RAW(const GLubyte*,glGetString,(GLenum))
GL_RAW(void,glGetIntegerv,(GLenum,GLint*))
GL_RAW(GLenum,glGetError,(void))
GL_RAW(void,glActiveTexture,(GLenum))
GL_RAW(void,glBindTexture,(GLenum,GLuint))
GL_RAW(void,glGenTextures,(GLsizei,GLuint*))
GL_RAW(void,glTexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid*))
GL_RAW(void,glTexParameteri,(GLenum,GLenum,GLint))
GL_RAW(void,glTexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const GLvoid*))
GL_RAW(void,glPixelStorei,(GLenum,GLint))
GL_RAW(void,glCompressedTexImage2D,(GLenum,GLint,GLenum,GLsizei,GLsizei,GLint,GLsizei,const GLvoid*))
GL_RAW(void,glGenRenderbuffers,(GLsizei,GLuint*))
GL_RAW(void,glDeleteRenderbuffers,(GLsizei,const GLuint*))
GL_RAW(void,glBindRenderbuffer,(GLenum,GLuint))
GL_RAW(void,glRenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei))
GL_RAW(void,glBindFramebuffer,(GLenum,GLuint))
GL_RAW(void,glFramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint))
GL_RAW(void,glFramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint))
GL_RAW(void,glGetShaderPrecisionFormat,(GLenum,GLenum,GLint*,GLint*))

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
