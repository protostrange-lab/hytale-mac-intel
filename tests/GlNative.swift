// Sprawdza, czy sam macOS daje kontekst OpenGL 4.1 Core na tej karcie.
// Hytale wymaga GL 4.1, a macOS oferuje dokladnie 4.1 - ani wiecej, ani mniej.
import OpenGL.GL
import OpenGL.GL3
import AppKit

let attrs: [NSOpenGLPixelFormatAttribute] = [
    UInt32(NSOpenGLPFAOpenGLProfile), UInt32(NSOpenGLProfileVersion4_1Core),
    UInt32(NSOpenGLPFAAccelerated),
    UInt32(NSOpenGLPFADoubleBuffer),
    0
]

guard let pf = NSOpenGLPixelFormat(attributes: attrs) else {
    print("FAIL: brak formatu piksela dla 4.1 Core"); exit(1)
}
guard let ctx = NSOpenGLContext(format: pf, share: nil) else {
    print("FAIL: brak kontekstu"); exit(1)
}
ctx.makeCurrentContext()

func str(_ e: GLenum) -> String { String(cString: glGetString(e)!) }
print("GL_VERSION : \(str(GLenum(GL_VERSION)))")
print("GL_RENDERER: \(str(GLenum(GL_RENDERER)))")
print("GL_VENDOR  : \(str(GLenum(GL_VENDOR)))")
print("GLSL       : \(str(GLenum(GL_SHADING_LANGUAGE_VERSION)))")
