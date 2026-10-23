package com.example.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val VenturaColorScheme = darkColorScheme(
    primary = AccentMonochromeLight,
    onPrimary = SlateBackground,
    primaryContainer = AccentDarkContainer,
    onPrimaryContainer = TextPrimaryOffWhite,
    secondary = AccentSteelGray,
    onSecondary = SlateBackground,
    secondaryContainer = SlateSurfaceContainer,
    onSecondaryContainer = TextPrimaryOffWhite,
    tertiary = StatusSuccessMuted,
    onTertiary = TextPrimaryOffWhite,
    background = SlateBackground,
    onBackground = TextPrimaryOffWhite,
    surface = SlateSurface,
    onSurface = TextPrimaryOffWhite,
    surfaceVariant = SlateSurfaceElevated,
    onSurfaceVariant = TextSecondaryNeutral,
    outline = SlateBorder,
    outlineVariant = SlateBorderSubtle,
    error = StatusErrorMuted,
    onError = TextPrimaryOffWhite,
    errorContainer = StatusErrorContainer,
    onErrorContainer = TextPrimaryOffWhite
)

@Composable
fun MyApplicationTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = VenturaColorScheme,
        typography = Typography,
        content = content
    )
}
