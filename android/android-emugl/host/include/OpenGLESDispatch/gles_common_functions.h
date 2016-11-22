// Auto-generated with: android/scripts/gen-entries.py --mode=funcargs android/android-emugl/host/libs/libOpenGLESDispatch/gles_common.entries --output=android/android-emugl/host/include/OpenGLESDispatch/gles_common_functions.h
// DO NOT EDIT THIS FILE

#ifndef GLES_COMMON_FUNCTIONS_H
#define GLES_COMMON_FUNCTIONS_H

#include <GLES/gl.h>
// Return types must be single words, see GLDispatch.cpp
typedef const GLubyte* GLconstubyteptr;
#define LIST_GLES_COMMON_FUNCTIONS(X) \
  X(void, glActiveTexture, (GLenum texture), (texture)) \
  X(void, glBindBuffer, (GLenum target, GLuint buffer), (target, buffer)) \
  X(void, glBindTexture, (GLenum target, GLuint texture), (target, texture)) \
  X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor), (sfactor, dfactor)) \
  X(void, glBlendEquation, (GLenum mode), (mode)) \
  X(void, glBlendEquationSeparate, (GLenum modeRGB, GLenum modeAlpha), (modeRGB, modeAlpha)) \
  X(void, glBlendFuncSeparate, (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha), (srcRGB, dstRGB, srcAlpha, dstAlpha)) \
  X(void, glBufferData, (GLenum target, GLsizeiptr size, const GLvoid * data, GLenum usage), (target, size, data, usage)) \
  X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid * data), (target, offset, size, data)) \
  X(void, glClear, (GLbitfield mask), (mask)) \
  X(void, glClearColor, (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha), (red, green, blue, alpha)) \
  X(void, glClearDepth, (GLclampd depth), (depth)) \
  X(void, glClearStencil, (GLint s), (s)) \
  X(void, glColorMask, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha), (red, green, blue, alpha)) \
  X(void, glCompressedTexImage2D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid * data), (target, level, internalformat, width, height, border, imageSize, data)) \
  X(void, glCompressedTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid * data), (target, level, xoffset, yoffset, width, height, format, imageSize, data)) \
  X(void, glCopyTexImage2D, (GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border), (target, level, internalFormat, x, y, width, height, border)) \
  X(void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height), (target, level, xoffset, yoffset, x, y, width, height)) \
  X(void, glCullFace, (GLenum mode), (mode)) \
  X(void, glDeleteBuffers, (GLsizei n, const GLuint * buffers), (n, buffers)) \
  X(void, glDeleteTextures, (GLsizei n, const GLuint * textures), (n, textures)) \
  X(void, glDepthFunc, (GLenum func), (func)) \
  X(void, glDepthMask, (GLboolean flag), (flag)) \
  X(void, glDepthRange, (GLclampd zNear, GLclampd zFar), (zNear, zFar)) \
  X(void, glDisable, (GLenum cap), (cap)) \
  X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count)) \
  X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const GLvoid * indices), (mode, count, type, indices)) \
  X(void, glEnable, (GLenum cap), (cap)) \
  X(void, glFinish, (), ()) \
  X(void, glFlush, (), ()) \
  X(void, glFrontFace, (GLenum mode), (mode)) \
  X(void, glGenBuffers, (GLsizei n, GLuint * buffers), (n, buffers)) \
  X(void, glGenTextures, (GLsizei n, GLuint * textures), (n, textures)) \
  X(void, glGetBooleanv, (GLenum pname, GLboolean * params), (pname, params)) \
  X(void, glGetBufferParameteriv, (GLenum buffer, GLenum parameter, GLint * value), (buffer, parameter, value)) \
  X(GLenum, glGetError, (), ()) \
  X(void, glGetFloatv, (GLenum pname, GLfloat * params), (pname, params)) \
  X(void, glGetIntegerv, (GLenum pname, GLint * params), (pname, params)) \
  X(GLconstubyteptr, glGetString, (GLenum name), (name)) \
  X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param), (target, pname, param)) \
  X(void, glTexParameterfv, (GLenum target, GLenum pname, const GLfloat * params), (target, pname, params)) \
  X(void, glGetTexParameterfv, (GLenum target, GLenum pname, GLfloat * params), (target, pname, params)) \
  X(void, glGetTexParameteriv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glGetTexLevelParameteriv, (GLenum target, GLint level, GLenum pname, GLint * params), (target, level, pname, params)) \
  X(void, glHint, (GLenum target, GLenum mode), (target, mode)) \
  X(GLboolean, glIsBuffer, (GLuint buffer), (buffer)) \
  X(GLboolean, glIsEnabled, (GLenum cap), (cap)) \
  X(GLboolean, glIsTexture, (GLuint texture), (texture)) \
  X(void, glLineWidth, (GLfloat width), (width)) \
  X(void, glPolygonOffset, (GLfloat factor, GLfloat units), (factor, units)) \
  X(void, glPixelStorei, (GLenum pname, GLint param), (pname, param)) \
  X(void, glReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid * pixels), (x, y, width, height, format, type, pixels)) \
  X(void, glSampleCoverage, (GLclampf value, GLboolean invert), (value, invert)) \
  X(void, glScissor, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height)) \
  X(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask), (func, ref, mask)) \
  X(void, glStencilMask, (GLuint mask), (mask)) \
  X(void, glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass), (fail, zfail, zpass)) \
  X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * pixels), (target, level, internalformat, width, height, border, format, type, pixels)) \
  X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param), (target, pname, param)) \
  X(void, glTexParameteriv, (GLenum target, GLenum pname, const GLint * params), (target, pname, params)) \
  X(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid * pixels), (target, level, xoffset, yoffset, width, height, format, type, pixels)) \
  X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height)) \
  X(void, glPushAttrib, (GLbitfield mask), (mask)) \
  X(void, glPushClientAttrib, (GLbitfield mask), (mask)) \
  X(void, glPopAttrib, (), ()) \
  X(void, glPopClientAttrib, (), ()) \


#endif  // GLES_COMMON_FUNCTIONS_H
