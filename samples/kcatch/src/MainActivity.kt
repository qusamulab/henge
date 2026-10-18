package dev.henge.kcatch

import android.app.Activity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log

/** KCatch: drag to move the paddle, catch the falling squares. Written in Kotlin; runs on henge-vm. */
class MainActivity : Activity() {
    private var glView: GLSurfaceView? = null
    private val game = Game()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i("kcatch", "onCreate")
        game.loadConfig(this)
        val view = GLSurfaceView(this)
        view.setEGLContextClientVersion(2)
        view.setRenderer(GameRenderer(game))
        view.setOnTouchListener { _, e ->
            game.onTouch(e)
            true
        }
        glView = view
        setContentView(view)
    }

    override fun onResume() {
        super.onResume()
        glView?.onResume()
    }

    override fun onPause() {
        glView?.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        Log.i("kcatch", "onDestroy, final score ${game.score}")
        super.onDestroy()
    }
}
