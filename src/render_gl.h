#pragma once
// The GL entry points the native renderer back end (src/render_backend.cpp)
// calls, on the host: src/render_gl.cpp draws with the thread's current
// GL context (the one the opengl32 entry points draw with, so these mix
// with the lifted code's calls in order), its vertex array draws with the
// context's device itself; the back end's oracle links recorders instead
// (tests/render_backend_test.cpp). Arguments are GL's
// (GLboolean as a whole dword, as the original passes it); pointers are
// host pointers, null for a guest null.
//
// Only what the context has: the extensions it does not advertise
// (register combiners, vertex programs, ATI fragment shaders and array
// objects, vertex buffer objects, NV vertex array range, draw range
// elements) keep their qgl pointers, which stay null in this port, so
// the branches that call them are never taken. The renderer's GL error
// checks, which swap the qgl pointers for wrappers that report
// glGetError after each call (such as 0x4B6570), do not see these calls.

#include <cstdint>

namespace recomp::gen {

void glEnable(uint32_t cap);
void glDisable(uint32_t cap);
void glHint(uint32_t target, uint32_t mode);
void glFinish();
void glFlush();
void glDrawBuffer(uint32_t mode);
void glBlendFunc(uint32_t src, uint32_t dst);
void glAlphaFunc(uint32_t func, float ref);
void glDepthFunc(uint32_t func);
void glDepthMask(uint32_t flag);
void glDepthRange(double near_val, double far_val);
void glColorMask(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
void glCullFace(uint32_t mode);
void glPolygonMode(uint32_t face, uint32_t mode);
void glPolygonOffset(float factor, float units);
void glViewport(int32_t x, int32_t y, int32_t w, int32_t h);
void glScissor(int32_t x, int32_t y, int32_t w, int32_t h);
void glStencilFunc(uint32_t func, int32_t ref, uint32_t mask);
void glStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass);
void glFogf(uint32_t pname, float param);
void glFogi(uint32_t pname, int32_t param);
void glFogfv(uint32_t pname, const float* params);
void glClearColor(float r, float g, float b, float a);
void glClear(uint32_t mask);
void glLightf(uint32_t light, uint32_t pname, float param);
void glLightfv(uint32_t light, uint32_t pname, const float* params);
void glLightModelfv(uint32_t pname, const float* params);
void glMatrixMode(uint32_t mode);
void glLoadIdentity();
void glLoadMatrixf(const float* m);
void glPushMatrix();
void glPopMatrix();
void glOrtho(double l, double r, double b, double t, double n, double f);
void glBindTexture(uint32_t target, uint32_t texture);
void glTexEnvi(uint32_t target, uint32_t pname, int32_t param);
void glTexEnvf(uint32_t target, uint32_t pname, float param);
void glTexEnvfv(uint32_t target, uint32_t pname, const float* params);
void glTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t w,
                     int32_t h, uint32_t format, uint32_t type, const void* pixels);
void glCopyTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
                         int32_t x, int32_t y, int32_t w, int32_t h);
void glActiveTextureARB(uint32_t texture);
void glClientActiveTextureARB(uint32_t texture);
void glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void* p);
void glColorPointer(int32_t size, uint32_t type, int32_t stride, const void* p);
void glTexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* p);
void glNormalPointer(uint32_t type, int32_t stride, const void* p);
void glEnableClientState(uint32_t array);
void glDisableClientState(uint32_t array);
void glLockArraysEXT(int32_t first, int32_t count);
void glUnlockArraysEXT();
void glDrawArrays(uint32_t mode, int32_t first, int32_t count);
void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices);
void glArrayElement(int32_t i);
void glBegin(uint32_t mode);
void glEnd();
void glColor3f(float r, float g, float b);
void glNormal3f(float x, float y, float z);
void glLineWidth(float width);
void glColor4ubv(const uint8_t* v);
void glReadPixels(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t format, uint32_t type,
                  void* pixels);

}  // namespace recomp::gen
