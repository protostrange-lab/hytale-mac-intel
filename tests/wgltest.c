/* Czy aplikacja Windows dostaje pod Wine kontekst OpenGL 4.1 Core?
 *
 * Hytale wymaga OpenGL 4.1, a macOS oferuje dokladnie 4.1. Pytanie jest wiec,
 * czy Wine potrafi ten kontekst podac dalej - i czy trafi na karte dyskretna,
 * czy na uklad zintegrowany (ktory jest ponizej minimum gry).
 *
 * Budowanie:  x86_64-w64-mingw32-gcc -O2 -o wgltest.exe wgltest.c -lopengl32 -lgdi32
 * Uruchomienie: wine wgltest.exe
 */
#include <windows.h>
#include <stdio.h>

typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_FLAGS_ARB         0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002

#define GL_VENDOR   0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION  0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NUM_EXTENSIONS   0x821D
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_SAMPLES      0x8D57

typedef const unsigned char* (WINAPI *PFNGLGETSTRING)(unsigned int);
typedef void (WINAPI *PFNGLGETINTEGERV)(unsigned int, int*);

int main(void) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "glt";
    wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("glt", "gl", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240,
                              NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("FAIL: brak okna\n"); return 1; }
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf) { printf("FAIL: ChoosePixelFormat\n"); return 1; }
    SetPixelFormat(dc, pf, &pfd);

    /* Najpierw kontekst zgodnosciowy - macOS daje w nim tylko 2.1. */
    HGLRC legacy = wglCreateContext(dc);
    if (!legacy) { printf("FAIL: wglCreateContext\n"); return 1; }
    wglMakeCurrent(dc, legacy);

    HMODULE gl = LoadLibraryA("opengl32.dll");
    PFNGLGETSTRING   glGetString   = (PFNGLGETSTRING)GetProcAddress(gl, "glGetString");
    PFNGLGETINTEGERV glGetIntegerv = (PFNGLGETINTEGERV)GetProcAddress(gl, "glGetIntegerv");

    printf("--- kontekst zgodnosciowy (legacy) ---\n");
    printf("GL_VERSION : %s\n", glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VENDOR  : %s\n", glGetString(GL_VENDOR));
    printf("GLSL       : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    PFNWGLCREATECONTEXTATTRIBSARB wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!wglCreateContextAttribsARB) {
        printf("\nFAIL: brak wglCreateContextAttribsARB (bez kontekstu Core)\n");
        return 2;
    }

    /* A teraz to, o co chodzi: 4.1 Core, forward compatible - jak macOS wymaga. */
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 1,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
    };
    HGLRC core = wglCreateContextAttribsARB(dc, NULL, attribs);
    if (!core) {
        printf("\nFAIL: nie udalo sie utworzyc kontekstu 4.1 Core (err=%lu)\n", GetLastError());
        return 3;
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);
    wglMakeCurrent(dc, core);

    printf("\n--- kontekst 4.1 CORE (to czego potrzebuje Hytale) ---\n");
    printf("GL_VERSION : %s\n", glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VENDOR  : %s\n", glGetString(GL_VENDOR));
    printf("GLSL       : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    int n = 0, maxtex = 0, maxsamp = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
    glGetIntegerv(GL_MAX_SAMPLES, &maxsamp);
    printf("rozszerzen : %d\n", n);
    printf("max tekstura: %d\n", maxtex);
    printf("max MSAA   : %d\n", maxsamp);

    printf("\nOK: kontekst 4.1 Core dziala\n");
    return 0;
}
