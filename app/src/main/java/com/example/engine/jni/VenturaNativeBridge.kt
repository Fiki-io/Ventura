package com.example.engine.jni

import android.view.Surface
import com.example.core.constants.AppConstants

object VenturaNativeBridge {

    val isJniLibraryLoaded: Boolean

    init {
        isJniLibraryLoaded = try {
            System.loadLibrary(AppConstants.NATIVE_LIBRARY_NAME)
            true
        } catch (_: UnsatisfiedLinkError) {
            false
        } catch (_: SecurityException) {
            false
        }
    }

    external fun nativeInitArena(baseVa: Int, arenaSize: Int): Boolean
    external fun nativeLaunchGameProcess(
        gameDir: String,
        multiplayerSamp: Boolean,
        serverIp: String,
        serverPort: Int,
        nickname: String,
        password: String
    ): Boolean
    external fun nativeStopGameProcess()
    external fun nativeSendKeyEvent(virtualKey: Int, isDown: Boolean)
    external fun nativeSendCharEvent(charCode: Int)
    external fun nativeSendPointerEvent(x: Int, y: Int, pressed: Boolean)
    external fun nativeInspectPe32(filePath: String): String
    external fun nativeQuerySampServer(host: String, port: Int): String
    external fun nativeAttachSurface(surface: Surface?)

    fun startNativeGameSession(
        gameDir: String,
        multiplayerSamp: Boolean,
        serverIp: String,
        serverPort: Int,
        nickname: String,
        password: String
    ): Boolean {
        return if (isJniLibraryLoaded) {
            try {
                nativeLaunchGameProcess(
                    gameDir = gameDir,
                    multiplayerSamp = multiplayerSamp,
                    serverIp = serverIp,
                    serverPort = serverPort,
                    nickname = nickname,
                    password = password
                )
            } catch (_: Throwable) {
                false
            }
        } else {
            false
        }
    }

    fun stopNativeGameSession() {
        if (isJniLibraryLoaded) {
            try {
                nativeStopGameProcess()
            } catch (_: Throwable) {
            }
        }
    }

    fun sendWindowsKeyEvent(virtualKey: Int, isDown: Boolean) {
        if (isJniLibraryLoaded) {
            try {
                nativeSendKeyEvent(virtualKey, isDown)
            } catch (_: Throwable) {
            }
        }
    }

    fun sendWindowsCharEvent(charCode: Int) {
        if (isJniLibraryLoaded) {
            try {
                nativeSendCharEvent(charCode)
            } catch (_: Throwable) {
            }
        }
    }

    fun sendWindowsPointerEvent(x: Int, y: Int, pressed: Boolean) {
        if (isJniLibraryLoaded) {
            try {
                nativeSendPointerEvent(x, y, pressed)
            } catch (_: Throwable) {
            }
        }
    }

    fun bindAndroidSurface(surface: Surface?) {
        if (isJniLibraryLoaded) {
            try {
                nativeAttachSurface(surface)
            } catch (_: Throwable) {
            }
        }
    }
}
