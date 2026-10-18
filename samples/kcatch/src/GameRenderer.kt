package dev.henge.kcatch

import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GameRenderer(private val game: Game) : GLSurfaceView.Renderer {
    private var program = 0
    private var uRect = 0
    private var uRes = 0
    private var uColor = 0
    private var frames = 0
    private var lastLog = 0L

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        program = link(compile(GLES20.GL_VERTEX_SHADER, VERTEX), compile(GLES20.GL_FRAGMENT_SHADER, FRAGMENT))
        GLES20.glBindAttribLocation(program, 0, "p")
        GLES20.glLinkProgram(program)
        uRect = GLES20.glGetUniformLocation(program, "r")
        uRes = GLES20.glGetUniformLocation(program, "res")
        uColor = GLES20.glGetUniformLocation(program, "c")

        val quad = floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f)
        val buf = ByteBuffer.allocateDirect(quad.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        buf.put(quad)
        buf.position(0)
        val vbo = IntArray(1)
        GLES20.glGenBuffers(1, vbo, 0)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[0])
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, quad.size * 4, buf, GLES20.GL_STATIC_DRAW)
        Log.i("kcatch", "GL_VERSION ${GLES20.glGetString(GLES20.GL_VERSION)} | GL_RENDERER ${GLES20.glGetString(GLES20.GL_RENDERER)}")
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        Log.i("kcatch", "surface ${width}x$height")
        GLES20.glViewport(0, 0, width, height)
        game.resize(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        game.update()
        val flash = if (game.flash > 0f) 0.35f else 0f
        GLES20.glClearColor(game.bgR + flash, game.bgG, game.bgB, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glUniform2f(uRes, game.width, game.height)
        GLES20.glEnableVertexAttribArray(0)
        GLES20.glVertexAttribPointer(0, 2, GLES20.GL_FLOAT, false, 0, 0)

        val s = game.itemSize
        for (item in game.items) rect(item.x - s / 2, item.y, s, s, item.r, item.g, item.b)
        rect(game.paddleX - game.paddleW / 2, game.paddleY, game.paddleW, game.paddleH, 0.95f, 0.55f, 0.2f)
        number(game.score, game.width * 0.05f, game.height * 0.14f, game.width * 0.045f)
        for (i in 0 until game.lives)
            rect(game.width * 0.95f - (i + 1) * game.width * 0.06f, game.height * 0.145f, game.width * 0.045f, game.width * 0.045f, 0.9f, 0.2f, 0.25f)
        if (game.lives <= 0) {
            rect(game.width * 0.2f, game.height * 0.45f, game.width * 0.6f, game.height * 0.1f, 0.9f, 0.2f, 0.25f)
            rect(game.width * 0.23f, game.height * 0.47f, game.width * 0.54f, game.height * 0.06f, 0.05f, 0.05f, 0.1f)
        }

        frames++
        val now = android.os.SystemClock.uptimeMillis()
        if (now - lastLog >= 1000) {
            Log.i("kcatch", "fps $frames score ${game.score} lives ${game.lives}")
            frames = 0
            lastLog = now
        }
    }

    private fun rect(x: Float, y: Float, w: Float, h: Float, r: Float, g: Float, b: Float) {
        GLES20.glUniform4f(uRect, x, y, w, h)
        GLES20.glUniform4f(uColor, r, g, b, 1f)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
    }

    /** Seven-segment digits: a b c d e f g. */
    private fun digit(d: Int, x: Float, y: Float, s: Float) {
        val t = s * 0.18f
        val m = SEGMENTS[d]
        if (m and 1 != 0) rect(x, y, s, t, 1f, 1f, 1f)
        if (m and 2 != 0) rect(x + s - t, y, t, s, 1f, 1f, 1f)
        if (m and 4 != 0) rect(x + s - t, y + s, t, s, 1f, 1f, 1f)
        if (m and 8 != 0) rect(x, y + 2 * s - t, s, t, 1f, 1f, 1f)
        if (m and 16 != 0) rect(x, y + s, t, s, 1f, 1f, 1f)
        if (m and 32 != 0) rect(x, y, t, s, 1f, 1f, 1f)
        if (m and 64 != 0) rect(x, y + s - t / 2, s, t, 1f, 1f, 1f)
    }

    private fun number(n: Int, x: Float, y: Float, s: Float) {
        val text = n.toString()
        for (i in text.indices) digit(text[i] - '0', x + i * s * 1.5f, y, s)
    }

    private fun compile(type: Int, src: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, src)
        GLES20.glCompileShader(shader)
        val ok = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, ok, 0)
        if (ok[0] == 0) Log.e("kcatch", "shader: " + GLES20.glGetShaderInfoLog(shader))
        return shader
    }

    private fun link(vs: Int, fs: Int): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, vs)
        GLES20.glAttachShader(p, fs)
        GLES20.glLinkProgram(p)
        return p
    }

    companion object {
        private val SEGMENTS = intArrayOf(0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f)
        private const val VERTEX = """
            attribute vec2 p; uniform vec4 r; uniform vec2 res;
            void main() {
                vec2 px = r.xy + p * r.zw;
                gl_Position = vec4(px.x / res.x * 2.0 - 1.0, 1.0 - px.y / res.y * 2.0, 0.0, 1.0);
            }"""
        private const val FRAGMENT = "precision mediump float; uniform vec4 c; void main() { gl_FragColor = c; }"
    }
}
