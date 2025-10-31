package org.barneshut.gpu

import org.lwjgl.BufferUtils
import org.lwjgl.glfw.GLFW.*
import org.lwjgl.glfw.GLFWErrorCallback
import org.lwjgl.opengl.GL
import org.lwjgl.opengl.GL46C.*
import java.nio.FloatBuffer
import kotlin.math.ceil
import kotlin.random.Random

private const val PARTICLE_COUNT = 8192
private const val WORK_GROUP_SIZE = 128

fun main() {
    GpuBarnesHutSimulator().run()
}

class GpuBarnesHutSimulator {
    private var window: Long = 0
    private var computeProgram = 0
    private var renderProgram = 0
    private var particleSsbo = 0
    private var vao = 0

    private var dtLocation = -1
    private var gLocation = -1
    private var softeningLocation = -1
    private var particleCountLocation = -1

    private var projectionLocation = -1

    private val gravityConstant = 6.67430e-3f
    private val softening = 1e-3f

    fun run() {
        initWindow()
        initOpenGL()
        loop()
        cleanup()
    }

    private fun initWindow() {
        GLFWErrorCallback.createPrint(System.err).set()

        if (!glfwInit()) {
            throw IllegalStateException("Unable to initialize GLFW")
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4)
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6)
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE)
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE)

        window = glfwCreateWindow(1280, 720, "GPU Barnes-Hut Simulator", 0, 0)
        if (window == 0L) {
            throw IllegalStateException("Failed to create window")
        }

        glfwMakeContextCurrent(window)
        glfwSwapInterval(1)
    }

    private fun initOpenGL() {
        GL.createCapabilities()

        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_PROGRAM_POINT_SIZE)

        computeProgram = createComputeProgram(loadResource("/shaders/gravity.comp"))
        renderProgram = createRenderProgram(
            loadResource("/shaders/particles.vert"),
            loadResource("/shaders/particles.frag")
        )

        particleSsbo = createParticleBuffer()
        vao = glGenVertexArrays()
        glBindVertexArray(vao)
        glBindBuffer(GL_ARRAY_BUFFER, particleSsbo)
        val stride = java.lang.Float.BYTES * 8
        glVertexAttribPointer(0, 3, GL_FLOAT, false, stride, 0L)
        glEnableVertexAttribArray(0)
        glBindBuffer(GL_ARRAY_BUFFER, 0)
        glBindVertexArray(0)

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particleSsbo)

        dtLocation = glGetUniformLocation(computeProgram, "dt")
        gLocation = glGetUniformLocation(computeProgram, "g")
        softeningLocation = glGetUniformLocation(computeProgram, "softening")
        particleCountLocation = glGetUniformLocation(computeProgram, "particleCount")

        projectionLocation = glGetUniformLocation(renderProgram, "projection")
    }

    private fun loop() {
        var lastTime = glfwGetTime().toFloat()
        val projection = createProjectionMatrix()

        while (!glfwWindowShouldClose(window)) {
            val currentTime = glfwGetTime().toFloat()
            val dt = (currentTime - lastTime).coerceIn(0.0001f, 0.02f)
            lastTime = currentTime

            glUseProgram(computeProgram)
            glUniform1f(dtLocation, dt)
            glUniform1f(gLocation, gravityConstant)
            glUniform1f(softeningLocation, softening)
            glUniform1ui(particleCountLocation, PARTICLE_COUNT)

            val groups = ceil(PARTICLE_COUNT / WORK_GROUP_SIZE.toFloat()).toInt()
            glDispatchCompute(groups, 1, 1)
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT or GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT)

            glClearColor(0.02f, 0.02f, 0.05f, 1.0f)
            glClear(GL_COLOR_BUFFER_BIT)

            glUseProgram(renderProgram)
            glUniformMatrix4fv(projectionLocation, false, projection)
            glBindVertexArray(vao)
            glDrawArrays(GL_POINTS, 0, PARTICLE_COUNT)
            glBindVertexArray(0)

            glfwSwapBuffers(window)
            glfwPollEvents()

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, true)
            }
        }
    }

    private fun cleanup() {
        glDeleteProgram(computeProgram)
        glDeleteProgram(renderProgram)
        glDeleteBuffers(particleSsbo)
        glDeleteVertexArrays(vao)

        glfwDestroyWindow(window)
        glfwTerminate()
        glfwSetErrorCallback(null)?.free()
    }

    private fun createParticleBuffer(): Int {
        val buffer = BufferUtils.createFloatBuffer(PARTICLE_COUNT * 8)
        val random = Random(42)
        for (i in 0 until PARTICLE_COUNT) {
            val radius = random.nextFloat() * 0.6f + 0.05f
            val angle = random.nextFloat() * (Math.PI * 2.0).toFloat()
            val x = radius * kotlin.math.cos(angle)
            val y = radius * kotlin.math.sin(angle)
            val z = (random.nextFloat() - 0.5f) * 0.1f
            val mass = random.nextFloat() * 5f + 0.5f
            val orbitalSpeed = kotlin.math.sqrt(gravityConstant * 200f / radius)
            val vx = -kotlin.math.sin(angle) * orbitalSpeed
            val vy = kotlin.math.cos(angle) * orbitalSpeed
            val vz = 0f
            buffer.put(x).put(y).put(z).put(mass)
            buffer.put(vx).put(vy).put(vz).put(0f)
        }
        buffer.flip()

        val ssbo = glGenBuffers()
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo)
        glBufferData(GL_SHADER_STORAGE_BUFFER, buffer, GL_DYNAMIC_DRAW)
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0)
        return ssbo
    }

    private fun createProjectionMatrix(): FloatBuffer {
        val aspect = 1280f / 720f
        val scale = 1.8f
        val left = -scale * aspect
        val right = scale * aspect
        val bottom = -scale
        val top = scale
        val near = -1f
        val far = 1f

        val matrix = floatArrayOf(
            2f / (right - left), 0f, 0f, 0f,
            0f, 2f / (top - bottom), 0f, 0f,
            0f, 0f, -2f / (far - near), 0f,
            -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1f
        )

        return BufferUtils.createFloatBuffer(16).put(matrix).apply { flip() }
    }

    private fun loadResource(path: String): String {
        val stream = javaClass.getResourceAsStream(path)
            ?: throw IllegalArgumentException("Resource not found: $path")
        stream.use { input ->
            return input.bufferedReader().readText()
        }
    }

    private fun createComputeProgram(source: String): Int {
        val program = glCreateProgram()
        val shader = glCreateShader(GL_COMPUTE_SHADER)
        glShaderSource(shader, source)
        glCompileShader(shader)
        checkShaderCompile(shader, "Compute shader compilation failed")
        glAttachShader(program, shader)
        glLinkProgram(program)
        checkProgramLink(program, "Compute program link failed")
        glDeleteShader(shader)
        return program
    }

    private fun createRenderProgram(vertexSource: String, fragmentSource: String): Int {
        val program = glCreateProgram()
        val vertexShader = glCreateShader(GL_VERTEX_SHADER)
        glShaderSource(vertexShader, vertexSource)
        glCompileShader(vertexShader)
        checkShaderCompile(vertexShader, "Vertex shader compilation failed")

        val fragmentShader = glCreateShader(GL_FRAGMENT_SHADER)
        glShaderSource(fragmentShader, fragmentSource)
        glCompileShader(fragmentShader)
        checkShaderCompile(fragmentShader, "Fragment shader compilation failed")

        glAttachShader(program, vertexShader)
        glAttachShader(program, fragmentShader)
        glLinkProgram(program)
        checkProgramLink(program, "Render program link failed")

        glDeleteShader(vertexShader)
        glDeleteShader(fragmentShader)
        return program
    }

    private fun checkShaderCompile(shader: Int, errorMessage: String) {
        val status = glGetShaderi(shader, GL_COMPILE_STATUS)
        if (status != GL_TRUE) {
            val log = glGetShaderInfoLog(shader)
            throw IllegalStateException("$errorMessage: $log")
        }
    }

    private fun checkProgramLink(program: Int, errorMessage: String) {
        val status = glGetProgrami(program, GL_LINK_STATUS)
        if (status != GL_TRUE) {
            val log = glGetProgramInfoLog(program)
            throw IllegalStateException("$errorMessage: $log")
        }
    }
}
