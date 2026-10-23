package com.example.ui.screens

import android.app.Activity
import android.content.Context
import android.content.pm.ActivityInfo
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.inputmethod.InputMethodManager
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.example.R
import com.example.core.constants.AppConstants
import com.example.engine.jni.VenturaNativeBridge
import com.example.ui.theme.Dimens
import com.example.ui.theme.SlateBackground
import com.example.ui.theme.SlateBorder
import com.example.ui.theme.SlateSurface
import com.example.ui.theme.TextPrimaryOffWhite

@Composable
fun GameRunnerScreen(
    onSendVirtualKey: (Int, Boolean) -> Unit,
    onSendKeyTap: (Int) -> Unit,
    onSendTypedChar: (Int) -> Unit,
    onExitRunner: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var imeInputVisible by remember { mutableStateOf(false) }
    var imeBuffer by remember { mutableStateOf("") }
    val focusRequester = remember { FocusRequester() }

    // Lock to Landscape Fullscreen while Engine Runner is active
    DisposableEffect(context) {
        val activity = context as? Activity
        val prevOrientation = activity?.requestedOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE

        val window = activity?.window
        val insetsController = if (window != null) {
            WindowCompat.getInsetsController(window, window.decorView)
        } else {
            null
        }
        insetsController?.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        insetsController?.hide(WindowInsetsCompat.Type.systemBars())

        onDispose {
            VenturaNativeBridge.bindAndroidSurface(null)
            insetsController?.show(WindowInsetsCompat.Type.systemBars())
            activity?.requestedOrientation = prevOrientation
        }
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(SlateBackground)
    ) {
        // 1. 100% Native C++ ANativeWindow / Direct3D 9 SurfaceView
        AndroidView(
            modifier = Modifier.fillMaxSize(),
            factory = { ctx ->
                SurfaceView(ctx).apply {
                    isFocusable = true
                    isFocusableInTouchMode = true
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            if (holder.surface != null && holder.surface.isValid) {
                                VenturaNativeBridge.bindAndroidSurface(holder.surface)
                            }
                        }

                        override fun surfaceChanged(
                            holder: SurfaceHolder,
                            format: Int,
                            width: Int,
                            height: Int
                        ) {
                            // Viewport dimensions are queried via ANativeWindow_getWidth/Height on the render worker thread
                        }

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            VenturaNativeBridge.bindAndroidSurface(null)
                        }
                    })

                    setOnTouchListener { v, event ->
                        val x = event.x.toInt()
                        val y = event.y.toInt()
                        when (event.actionMasked) {
                            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                                VenturaNativeBridge.sendWindowsPointerEvent(x, y, true)
                            }
                            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                                VenturaNativeBridge.sendWindowsPointerEvent(x, y, false)
                                v.performClick()
                            }
                        }
                        true
                    }
                }
            }
        )

        // 2. Top Shortcut Bar (Sends real Windows VK_ESCAPE, VK_TAB, VK_F6 directly to WndProc)
        Row(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .fillMaxWidth()
                .padding(horizontal = Dimens.SpaceLG, vertical = Dimens.SpaceSM),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM)) {
                OverlayShortcutKey(
                    label = stringResource(R.string.runner_key_esc),
                    onClick = { onSendKeyTap(AppConstants.VK_ESCAPE) }
                )
                OverlayShortcutKey(
                    label = stringResource(R.string.runner_key_tab),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_TAB, held) }
                )
                OverlayShortcutKey(
                    label = stringResource(R.string.runner_key_f6),
                    onClick = {
                        // Send VK_F6 to samp.dll WndProc to open SA-MP's native in-game chatbox
                        onSendKeyTap(AppConstants.VK_F6)
                        imeInputVisible = !imeInputVisible
                    }
                )
                OverlayShortcutKey(
                    label = stringResource(R.string.runner_key_kb),
                    onClick = {
                        imeInputVisible = !imeInputVisible
                        if (imeInputVisible) {
                            val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager
                            imm?.toggleSoftInput(InputMethodManager.SHOW_FORCED, 0)
                        }
                    }
                )
            }

            OverlayShortcutKey(
                label = stringResource(R.string.runner_action_exit),
                onClick = onExitRunner,
                modifier = Modifier.testTag(AppConstants.TAG_BTN_EXIT_RUNNER)
            )
        }

        // Optional Direct WM_CHAR Keyboard Bridge (forwards characters directly to samp.dll WndProc)
        if (imeInputVisible) {
            Box(
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .padding(top = 64.dp)
                    .widthIn(max = 420.dp)
                    .background(SlateSurface.copy(alpha = 0.85f), RoundedCornerShape(Dimens.RadiusSM))
                    .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusSM))
                    .padding(Dimens.SpaceXS)
            ) {
                OutlinedTextField(
                    value = imeBuffer,
                    onValueChange = { newText ->
                        if (newText.length > imeBuffer.length) {
                            val added = newText.substring(imeBuffer.length)
                            added.forEach { ch -> onSendTypedChar(ch.code) }
                        } else if (newText.length < imeBuffer.length) {
                            onSendKeyTap(AppConstants.VK_BACK)
                        }
                        imeBuffer = newText
                    },
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                    keyboardActions = KeyboardActions(
                        onSend = {
                            onSendKeyTap(AppConstants.VK_RETURN)
                            imeBuffer = ""
                            imeInputVisible = false
                        }
                    ),
                    singleLine = true,
                    modifier = Modifier
                        .fillMaxWidth()
                        .focusRequester(focusRequester)
                )
            }
        }

        // 3. Bottom-Left DirectInput8 Movement Pad (W, A, S, D)
        Column(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(Dimens.SpaceXL),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(Dimens.SpaceXS)
        ) {
            TouchKeyPadButton(
                label = stringResource(R.string.ctrl_forward),
                onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_W, held) }
            )
            Row(horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceXS)) {
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_left),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_A, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_back),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_S, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_right),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_D, held) }
                )
            }
        }

        // 4. Bottom-Right Action & Vehicle Pad (F, G, SPACE, SHIFT, ATK, AIM)
        Column(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(Dimens.SpaceXL),
            horizontalAlignment = Alignment.End,
            verticalArrangement = Arrangement.spacedBy(Dimens.SpaceXS)
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceXS)) {
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_enter_veh),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_F, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_passenger),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_G, held) }
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceXS)) {
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_aim),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_RBUTTON, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_jump),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_SHIFT, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_sprint),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_SPACE, held) }
                )
                TouchKeyPadButton(
                    label = stringResource(R.string.ctrl_fire),
                    onHoldChange = { held -> onSendVirtualKey(AppConstants.VK_LBUTTON, held) }
                )
            }
        }
    }
}

@Composable
private fun OverlayShortcutKey(
    label: String,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    onHoldChange: ((Boolean) -> Unit)? = null
) {
    val gestureMod = if (onHoldChange != null) {
        Modifier.pointerInput(label) {
            detectTapGestures(
                onPress = {
                    onHoldChange(true)
                    tryAwaitRelease()
                    onHoldChange(false)
                }
            )
        }
    } else if (onClick != null) {
        Modifier.clickable(onClick = onClick)
    } else {
        Modifier
    }

    Box(
        modifier = modifier
            .heightIn(min = Dimens.MinTouchTarget)
            .background(SlateSurface.copy(alpha = 0.65f), RoundedCornerShape(Dimens.RadiusSM))
            .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusSM))
            .then(gestureMod)
            .padding(horizontal = Dimens.SpaceMD, vertical = Dimens.SpaceXS),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = TextPrimaryOffWhite
        )
    }
}

@Composable
private fun TouchKeyPadButton(
    label: String,
    onHoldChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .size(54.dp)
            .background(SlateSurface.copy(alpha = 0.55f), RoundedCornerShape(Dimens.RadiusMD))
            .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusMD))
            .pointerInput(label) {
                detectTapGestures(
                    onPress = {
                        onHoldChange(true)
                        tryAwaitRelease()
                        onHoldChange(false)
                    }
                )
            },
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = TextPrimaryOffWhite
        )
    }
}
