import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GLCapabilities;
import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL20.GL_SHADING_LANGUAGE_VERSION;
import static org.lwjgl.opengl.GL30.GL_NUM_EXTENSIONS;
import static org.lwjgl.system.MemoryUtil.NULL;

public class GlTest {
    public static void main(String[] args) {
        GLFWErrorCallback.createPrint(System.err).set();
        if (!glfwInit()) { System.out.println("FAIL: glfwInit"); System.exit(1); }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

        long win = glfwCreateWindow(1280, 720, "Hytale stack test", NULL, NULL);
        if (win == NULL) { System.out.println("FAIL: nie powstalo okno GL 4.1 Core"); glfwTerminate(); System.exit(2); }

        glfwMakeContextCurrent(win);
        glfwSwapInterval(0);
        GLCapabilities caps = GL.createCapabilities();

        System.out.println("GL_VERSION : " + glGetString(GL_VERSION));
        System.out.println("GL_RENDERER: " + glGetString(GL_RENDERER));
        System.out.println("GL_VENDOR  : " + glGetString(GL_VENDOR));
        System.out.println("GLSL       : " + glGetString(GL_SHADING_LANGUAGE_VERSION));
        System.out.println("rozszerzen : " + glGetInteger(GL_NUM_EXTENSIONS));
        System.out.println("OpenGL 3.2 : " + caps.OpenGL32);
        System.out.println("OpenGL 4.0 : " + caps.OpenGL40);
        System.out.println("OpenGL 4.1 : " + caps.OpenGL41);

        // przepustowosc: clear + swap bez vsync, 3 sekundy
        int frames = 0;
        long t0 = System.nanoTime();
        while (System.nanoTime() - t0 < 3_000_000_000L) {
            glClearColor(0.1f, 0.3f + (frames % 60) / 200f, 0.5f, 1f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glfwSwapBuffers(win);
            glfwPollEvents();
            frames++;
        }
        double secs = (System.nanoTime() - t0) / 1e9;
        System.out.printf("klatki     : %d w %.2fs = %.0f fps (clear+swap, bez vsync)%n", frames, secs, frames / secs);
        System.out.println("blad GL    : " + glGetError());
        System.out.println("\nOK: Java + LWJGL + OpenGL 4.1 Core dziala pod Wine");
        glfwDestroyWindow(win);
        glfwTerminate();
    }
}
