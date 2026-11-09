// The native back end's GL entry points (src/render_gl.h) on the thread's
// current gl::Context, with the conversions the opengl32 entry points
// make: integer parameters as floats, a GLboolean's low byte, vector
// parameters of 4 components for colours and positions and 1 otherwise,
// colour bytes over 255.
//
// glDrawArrays and glDrawElements draw with the device itself (DrawFf):
// the client arrays as its streams and the state from the context
// (gl::Context::ArrayDraw), the vertices the indices span as they are,
// with no gathering or renumbering. Draws whose arrays the device does
// not take as they are go through the context.

#include "render_gl.h"

#include <algorithm>
#include <cstring>

#include "runtime/log.h"
#include "win32/opengl32.h"

namespace recomp::gen {
namespace {

#define GL_OR_RETURN               \
  gl::Context* g = hle::CurrentGl(); \
  if (!g) return

// glLightfv's and glLightModelfv's components (the spot direction 3).
void LightParams(uint32_t pname, const float* p, float v[4]) {
  int n = 1;
  switch (pname) {
    case gl::SPOT_DIRECTION: n = 3; break;
    case gl::AMBIENT: case gl::DIFFUSE: case gl::SPECULAR: case gl::POSITION:
    case gl::EMISSION: case gl::AMBIENT_AND_DIFFUSE: case gl::LIGHT_MODEL_AMBIENT:
      n = 4;
      break;
  }
  for (int i = 0; i < 4; i++) v[i] = i < n ? p[i] : 0;
}

}  // namespace

void glEnable(uint32_t cap) { GL_OR_RETURN; g->SetEnabled(cap, true); }
void glDisable(uint32_t cap) { GL_OR_RETURN; g->SetEnabled(cap, false); }
// Hints may be ignored; finishing and flushing have nothing to wait for;
// there is one colour buffer pair.
void glHint(uint32_t, uint32_t) {}
void glFinish() {}
void glFlush() {}
void glDrawBuffer(uint32_t) {}
void glBlendFunc(uint32_t src, uint32_t dst) { GL_OR_RETURN; g->BlendFunc(src, dst); }
void glAlphaFunc(uint32_t func, float ref) { GL_OR_RETURN; g->AlphaFunc(func, ref); }
void glDepthFunc(uint32_t func) { GL_OR_RETURN; g->DepthFunc(func); }
void glDepthMask(uint32_t flag) { GL_OR_RETURN; g->DepthMask(flag & 0xFF); }
void glDepthRange(double near_val, double far_val) {
  GL_OR_RETURN;
  g->DepthRange(near_val, far_val);
}
void glColorMask(uint32_t r, uint32_t gr, uint32_t b, uint32_t a) {
  GL_OR_RETURN;
  g->ColorMask(r & 0xFF, gr & 0xFF, b & 0xFF, a & 0xFF);
}
void glCullFace(uint32_t mode) { GL_OR_RETURN; g->CullFace(mode); }
void glPolygonMode(uint32_t face, uint32_t mode) { GL_OR_RETURN; g->PolygonMode(face, mode); }
void glPolygonOffset(float factor, float units) { GL_OR_RETURN; g->PolygonOffset(factor, units); }
void glViewport(int32_t x, int32_t y, int32_t w, int32_t h) { GL_OR_RETURN; g->Viewport(x, y, w, h); }
void glScissor(int32_t x, int32_t y, int32_t w, int32_t h) { GL_OR_RETURN; g->Scissor(x, y, w, h); }
void glStencilFunc(uint32_t func, int32_t ref, uint32_t mask) {
  GL_OR_RETURN;
  g->StencilFunc(func, ref, mask);
}
void glStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) {
  GL_OR_RETURN;
  g->StencilOp(fail, zfail, zpass);
}
void glFogf(uint32_t pname, float param) {
  GL_OR_RETURN;
  const float v[4] = {param};
  g->Fog(pname, v);
}
void glFogi(uint32_t pname, int32_t param) {
  GL_OR_RETURN;
  const float v[4] = {float(param)};
  g->Fog(pname, v);
}
void glFogfv(uint32_t pname, const float* params) {
  GL_OR_RETURN;
  float v[4];
  for (int i = 0; i < 4; i++) v[i] = params[pname == gl::FOG_COLOR ? i : 0];
  g->Fog(pname, v);
}
void glClearColor(float r, float gr, float b, float a) { GL_OR_RETURN; g->ClearColor(r, gr, b, a); }
void glClear(uint32_t mask) { GL_OR_RETURN; g->Clear(mask); }
void glLightf(uint32_t light, uint32_t pname, float param) {
  GL_OR_RETURN;
  const float v[4] = {param};
  g->Light(light, pname, v);
}
void glLightfv(uint32_t light, uint32_t pname, const float* params) {
  GL_OR_RETURN;
  float v[4];
  LightParams(pname, params, v);
  g->Light(light, pname, v);
}
void glLightModelfv(uint32_t pname, const float* params) {
  GL_OR_RETURN;
  float v[4];
  LightParams(pname, params, v);
  g->LightModel(pname, v);
}
void glMatrixMode(uint32_t mode) { GL_OR_RETURN; g->MatrixMode(mode); }
void glLoadIdentity() { GL_OR_RETURN; g->LoadIdentity(); }
void glLoadMatrixf(const float* m) {
  GL_OR_RETURN;
  float v[16];
  std::memcpy(v, m, sizeof v);
  g->LoadMatrix(v);
}
void glPushMatrix() { GL_OR_RETURN; g->PushMatrix(); }
void glPopMatrix() { GL_OR_RETURN; g->PopMatrix(); }
void glOrtho(double l, double r, double b, double t, double n, double f) {
  GL_OR_RETURN;
  g->Ortho(l, r, b, t, n, f);
}
void glBindTexture(uint32_t target, uint32_t texture) { GL_OR_RETURN; g->BindTexture(target, texture); }
void glTexEnvi(uint32_t target, uint32_t pname, int32_t param) {
  GL_OR_RETURN;
  const float v[4] = {float(param)};
  g->TexEnv(target, pname, v);
}
void glTexEnvf(uint32_t target, uint32_t pname, float param) {
  GL_OR_RETURN;
  const float v[4] = {param};
  g->TexEnv(target, pname, v);
}
void glTexEnvfv(uint32_t target, uint32_t pname, const float* params) {
  GL_OR_RETURN;
  float v[4];
  for (int i = 0; i < 4; i++) v[i] = params[pname == gl::TEXTURE_ENV_COLOR ? i : 0];
  g->TexEnv(target, pname, v);
}
void glTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t w, int32_t h,
                     uint32_t format, uint32_t type, const void* pixels) {
  GL_OR_RETURN;
  g->TexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}
// Not in the context, and fatal as the opengl32 entry point without a
// handler is (RB_ShowImages and the screen copy command call it).
void glCopyTexSubImage2D(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                         int32_t) {
  FatalF("glCopyTexSubImage2D is not implemented");
}
void glActiveTextureARB(uint32_t texture) { GL_OR_RETURN; g->ActiveTexture(texture); }
void glClientActiveTextureARB(uint32_t texture) { GL_OR_RETURN; g->ClientActiveTexture(texture); }
void glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void* p) {
  GL_OR_RETURN;
  g->VertexPointer(size, type, stride, p);
}
void glColorPointer(int32_t size, uint32_t type, int32_t stride, const void* p) {
  GL_OR_RETURN;
  g->ColorPointer(size, type, stride, p);
}
void glTexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* p) {
  GL_OR_RETURN;
  g->TexCoordPointer(size, type, stride, p);
}
void glNormalPointer(uint32_t type, int32_t stride, const void* p) {
  GL_OR_RETURN;
  g->NormalPointer(type, stride, p);
}
void glEnableClientState(uint32_t array) { GL_OR_RETURN; g->SetClientState(array, true); }
void glDisableClientState(uint32_t array) { GL_OR_RETURN; g->SetClientState(array, false); }
// Compiled vertex arrays are a hint that the arrays stay unchanged.
void glLockArraysEXT(int32_t, int32_t) {}
void glUnlockArraysEXT() {}
void glDrawArrays(uint32_t mode, int32_t first, int32_t count) {
  GL_OR_RETURN;
  gfx::FfDraw d;
  const gfx::FfState* state;
  if (count > 0 && first >= 0 &&
      g->ArrayDraw(mode, uint32_t(first), uint32_t(first + count - 1), &d, &state))
    return g->Device()->DrawFf(*state, d);
  g->DrawArrays(mode, first, count);
}
// The back end's indices are unsigned shorts.
void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices) {
  GL_OR_RETURN;
  if (count > 0 && type == gl::UNSIGNED_SHORT) {
    const uint16_t* idx = static_cast<const uint16_t*>(indices);
    auto [lo, hi] = std::minmax_element(idx, idx + count);
    gfx::FfDraw d;
    const gfx::FfState* state;
    if (g->ArrayDraw(mode, *lo, *hi, &d, &state)) {
      d.indices = idx;
      d.index_count = uint32_t(count);
      return g->Device()->DrawFf(*state, d);
    }
  }
  g->DrawElements(mode, count, type, indices);
}
void glArrayElement(int32_t i) { GL_OR_RETURN; g->ArrayElement(i); }
void glBegin(uint32_t mode) { GL_OR_RETURN; g->Begin(mode); }
void glEnd() { GL_OR_RETURN; g->End(); }
void glColor3f(float r, float gr, float b) { GL_OR_RETURN; g->Color(r, gr, b, 1); }
void glNormal3f(float x, float y, float z) { GL_OR_RETURN; g->Normal(x, y, z); }
// Not in the context either (the debug draws' lines call it).
void glLineWidth(float) { FatalF("glLineWidth is not implemented"); }
void glColor4ubv(const uint8_t* v) {
  GL_OR_RETURN;
  g->Color(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
}
void glReadPixels(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t format, uint32_t type,
                  void* pixels) {
  GL_OR_RETURN;
  g->ReadPixels(x, y, w, h, format, type, pixels);
}

}  // namespace recomp::gen
