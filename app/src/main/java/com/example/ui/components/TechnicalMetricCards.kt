package com.example.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import com.example.ui.theme.Dimens
import com.example.ui.theme.JetBrainsMonoFamily
import com.example.ui.theme.SlateBorder
import com.example.ui.theme.SlateSurface
import com.example.ui.theme.SlateSurfaceContainer
import com.example.ui.theme.SlateSurfaceElevated
import com.example.ui.theme.StatusErrorContainer
import com.example.ui.theme.StatusErrorMuted
import com.example.ui.theme.StatusSuccessContainer
import com.example.ui.theme.StatusSuccessMuted
import com.example.ui.theme.StatusWarningContainer
import com.example.ui.theme.StatusWarningMuted
import com.example.ui.theme.TextMutedSlate
import com.example.ui.theme.TextPrimaryOffWhite
import com.example.ui.theme.TextSecondaryNeutral

enum class SemanticTone {
    NEUTRAL,
    SUCCESS,
    WARNING,
    ERROR
}

@Composable
fun SectionPanel(
    title: String,
    modifier: Modifier = Modifier,
    badgeText: String? = null,
    badgeTone: SemanticTone = SemanticTone.NEUTRAL,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = SlateSurface,
        shape = RoundedCornerShape(Dimens.RadiusMD),
        border = BorderStroke(Dimens.BorderThin, SlateBorder)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(Dimens.SpaceLG),
            verticalArrangement = Arrangement.spacedBy(Dimens.SpaceMD)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium,
                    color = TextPrimaryOffWhite
                )
                if (!badgeText.isNullOrEmpty()) {
                    StatusBadge(text = badgeText, tone = badgeTone)
                }
            }
            content()
        }
    }
}

@Composable
fun StatusBadge(
    text: String,
    tone: SemanticTone = SemanticTone.NEUTRAL,
    modifier: Modifier = Modifier
) {
    val (bgColor, borderColor, textColor) = when (tone) {
        SemanticTone.SUCCESS -> Triple(StatusSuccessContainer, StatusSuccessMuted, StatusSuccessMuted)
        SemanticTone.WARNING -> Triple(StatusWarningContainer, StatusWarningMuted, StatusWarningMuted)
        SemanticTone.ERROR -> Triple(StatusErrorContainer, StatusErrorMuted, StatusErrorMuted)
        SemanticTone.NEUTRAL -> Triple(SlateSurfaceContainer, SlateBorder, TextSecondaryNeutral)
    }

    Box(
        modifier = modifier
            .background(bgColor, RoundedCornerShape(Dimens.RadiusXS))
            .border(Dimens.BorderThin, borderColor, RoundedCornerShape(Dimens.RadiusXS))
            .padding(horizontal = Dimens.SpaceSM, vertical = Dimens.SpaceXXS)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall,
            color = textColor
        )
    }
}

@Composable
fun TelemetryKeyValuePair(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    isMonospace: Boolean = true
) {
    Column(
        modifier = modifier
            .background(SlateSurfaceElevated, RoundedCornerShape(Dimens.RadiusSM))
            .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusSM))
            .padding(horizontal = Dimens.SpaceMD, vertical = Dimens.SpaceSM),
        verticalArrangement = Arrangement.spacedBy(Dimens.SpaceXXS)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = TextMutedSlate
        )
        Text(
            text = value,
            style = if (isMonospace) {
                MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium)
            } else {
                MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium)
            },
            fontFamily = if (isMonospace) JetBrainsMonoFamily else MaterialTheme.typography.bodyMedium.fontFamily,
            color = TextPrimaryOffWhite
        )
    }
}

fun formatHex32(value: Long): String {
    return "0x" + (value and 0xFFFFFFFFL).toString(16).uppercase().padStart(8, '0')
}
