/*=============================================================================
	UC2GLRawFuncs.h: entry points of the GL library UNDER the binding.

	For the desktop binding these are the same functions as the main table's. For the GL4ES
	binding they are OpenGL ES 2 / WebGL itself, called around gl4es for the few things gl4es
	does not do the way this driver needs (M11, docs/porting/analysis/web-renderer.md,
	decision 2): depth-stencil storage and its attachment to a framebuffer, the hardware's
	own extension string and shader precision. Every use restores the raw state it changes
	(gl4es caches bindings), see UUC2GLRenderDevice::CreateDepthStencil*.

	GL_RAW(ret,func,parms) declares UUC2GLRenderDevice::Raw_<func>.
=============================================================================*/

GL_RAW(const GLubyte*,glGetString,(GLenum))
GL_RAW(void,glGetIntegerv,(GLenum,GLint*))
GL_RAW(GLenum,glGetError,(void))
GL_RAW(void,glActiveTexture,(GLenum))
GL_RAW(void,glBindTexture,(GLenum,GLuint))
GL_RAW(void,glTexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid*))
GL_RAW(void,glTexParameteri,(GLenum,GLenum,GLint))
GL_RAW(void,glTexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const GLvoid*))
GL_RAW(void,glPixelStorei,(GLenum,GLint))
GL_RAW(void,glGenRenderbuffers,(GLsizei,GLuint*))
GL_RAW(void,glDeleteRenderbuffers,(GLsizei,const GLuint*))
GL_RAW(void,glBindRenderbuffer,(GLenum,GLuint))
GL_RAW(void,glRenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei))
GL_RAW(void,glFramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint))
GL_RAW(void,glFramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint))
GL_RAW(void,glGetShaderPrecisionFormat,(GLenum,GLenum,GLint*,GLint*))

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
