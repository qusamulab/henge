package dev.henge.kcatch

import android.content.Context
import android.os.SystemClock
import android.util.Log
import android.view.MotionEvent
import java.util.Random

class Item(var x: Float, var y: Float, val speed: Float, val r: Float, val g: Float, val b: Float) {
    var alive = true
}

class Game {
    var width = 1f
    var height = 1f
    var score = 0
    var lives = LIVES
    var flash = 0f
    var bgR = 0.05f
    var bgG = 0.08f
    var bgB = 0.18f
    var baseSpeed = 380f

    var paddleX = 0f
    private var targetX = 0f
    private var lastTouch = SystemClock.uptimeMillis()
    private var lastFrame = SystemClock.uptimeMillis()
    private var spawnTimer = 0f
    private val rng = Random(1234)
    val items = ArrayList<Item>()

    /** assets/game.cfg: "bg=r g b" and "speed=n" lines. */
    fun loadConfig(context: Context) {
        try {
            val text = String(context.getAssets().open("game.cfg").readBytes())
            for (line in text.split("\n")) {
                if (line.startsWith("bg=")) {
                    val p = line.substring(3).trim().split(" ")
                    bgR = p[0].toFloat()
                    bgG = p[1].toFloat()
                    bgB = p[2].toFloat()
                } else if (line.startsWith("speed=")) {
                    baseSpeed = line.substring(6).trim().toFloat()
                }
            }
            Log.i("kcatch", "game.cfg: speed=$baseSpeed bg=$bgR $bgG $bgB")
        } catch (e: Exception) {
            Log.w("kcatch", "no game.cfg: $e")
        }
    }

    fun resize(w: Int, h: Int) {
        width = w.toFloat()
        height = h.toFloat()
        paddleX = width / 2
        targetX = paddleX
    }

    fun onTouch(e: MotionEvent) {
        targetX = e.getX()
        lastTouch = SystemClock.uptimeMillis()
        if (e.getActionMasked() == MotionEvent.ACTION_DOWN && lives <= 0) reset()
    }

    fun reset() {
        items.clear()
        score = 0
        lives = LIVES
        flash = 0f
    }

    val itemSize: Float get() = width * 0.07f
    val paddleW: Float get() = width * 0.24f
    val paddleH: Float get() = width * 0.035f
    val paddleY: Float get() = height * 0.88f

    fun update() {
        val now = SystemClock.uptimeMillis()
        val dt = minOf(0.05f, (now - lastFrame) / 1000f)
        lastFrame = now
        val attract = now - lastTouch > 5000
        if (lives <= 0) {
            if (attract && now - lastTouch > 8000) {
                reset()
                lastTouch = now - 6000
            }
            return
        }
        if (attract) {
            var best: Item? = null
            for (it in items) if (it.alive && (best == null || it.y > best.y)) best = it
            if (best != null) targetX = best.x
        }
        paddleX += (targetX - paddleX) * minOf(1f, dt * 18f)

        spawnTimer -= dt
        if (spawnTimer <= 0f) {
            spawnTimer = maxOf(0.35f, 0.9f - score * 0.02f)
            val s = itemSize
            items.add(Item(s / 2 + rng.nextFloat() * (width - s), -s,
                baseSpeed * (0.8f + rng.nextFloat() * 0.5f + score * 0.01f),
                0.5f + rng.nextFloat() * 0.5f, 0.4f + rng.nextFloat() * 0.6f, 0.3f + rng.nextFloat() * 0.7f))
        }
        val s = itemSize
        for (item in items) {
            if (!item.alive) continue
            item.y += item.speed * dt
            if (item.y + s >= paddleY && item.y <= paddleY + paddleH && Math.abs(item.x - paddleX) < (paddleW + s) / 2) {
                item.alive = false
                score++
            } else if (item.y > height) {
                item.alive = false
                lives--
                flash = 0.25f
                if (lives <= 0) Log.i("kcatch", "game over, score $score")
            }
        }
        items.removeIf { !it.alive }
        if (flash > 0f) flash -= dt
    }

    companion object {
        const val LIVES = 3
    }
}
