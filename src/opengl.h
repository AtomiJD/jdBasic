#pragma once

#ifdef OPENGL

class VM;

// Registers GL.WINDOW / GL.CLOSE / GL.CLEAR / GL.FLIP / GL.VIEWPORT /
// GL.ENABLE / GL.DISABLE. Phase 1: context + clear + swap only.
void register_opengl_builtins(VM& vm);

#endif
