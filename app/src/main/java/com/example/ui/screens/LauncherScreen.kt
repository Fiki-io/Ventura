package com.example.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import com.example.R
import com.example.core.constants.AppConstants
import com.example.domain.model.GameDirectoryVerification
import com.example.domain.model.SampQueryResponse
import com.example.domain.model.SampServerEndpoint
import com.example.ui.components.SectionPanel
import com.example.ui.components.SemanticTone
import com.example.ui.components.StatusBadge
import com.example.ui.components.TelemetryKeyValuePair
import com.example.ui.theme.AccentDarkContainer
import com.example.ui.theme.AccentMonochromeLight
import com.example.ui.theme.Dimens
import com.example.ui.theme.SlateBackground
import com.example.ui.theme.SlateBorder
import com.example.ui.theme.SlateSurfaceElevated
import com.example.ui.theme.TextPrimaryOffWhite
import com.example.ui.theme.TextSecondaryNeutral

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LauncherScreen(
    directoryPath: String,
    verification: GameDirectoryVerification,
    playerNickname: String,
    serverHost: String,
    serverPort: String,
    serverPassword: String,
    isQueryingServer: Boolean,
    savedServers: List<SampServerEndpoint>,
    queryResponse: SampQueryResponse,
    onDirectoryPathChange: (String) -> Unit,
    onPickFolder: () -> Unit,
    onVerifyFolder: () -> Unit,
    onNicknameChange: (String) -> Unit,
    onHostChange: (String) -> Unit,
    onPortChange: (String) -> Unit,
    onPasswordChange: (String) -> Unit,
    onQueryServer: () -> Unit,
    onSaveServer: () -> Unit,
    onSelectSavedServer: (SampServerEndpoint) -> Unit,
    onRemoveSavedServer: (SampServerEndpoint) -> Unit,
    onStartSamp: () -> Unit,
    onStartSingleplayer: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(Dimens.SpaceLG)
    ) {
        // 1. Game Folder Selector
        val folderReady = verification.isReadyForSamp || verification.isReadyForSingleplayer
        SectionPanel(
            title = stringResource(R.string.section_game_folder),
            badgeText = if (folderReady) {
                stringResource(R.string.status_ready)
            } else {
                stringResource(R.string.status_incomplete)
            },
            badgeTone = if (folderReady) SemanticTone.SUCCESS else SemanticTone.WARNING
        ) {
            OutlinedTextField(
                value = directoryPath,
                onValueChange = onDirectoryPathChange,
                label = { Text(stringResource(R.string.label_directory_path)) },
                placeholder = { Text(stringResource(R.string.hint_directory_path)) },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(AppConstants.TAG_INPUT_DIR_PATH)
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM)
            ) {
                Button(
                    onClick = onPickFolder,
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_PICK_FOLDER),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = AccentMonochromeLight,
                        contentColor = SlateBackground
                    ),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_select_folder))
                }

                OutlinedButton(
                    onClick = onVerifyFolder,
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_VERIFY_FOLDER),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_verify_folder))
                }
            }

            FlowRow(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM),
                verticalArrangement = Arrangement.spacedBy(Dimens.SpaceSM),
                maxItemsInEachRow = 3
            ) {
                TelemetryKeyValuePair(
                    label = AppConstants.FILE_GTA_SA_EXE,
                    value = if (verification.hasGtaSaExe) {
                        stringResource(R.string.file_found)
                    } else {
                        stringResource(R.string.file_missing)
                    },
                    modifier = Modifier.weight(1f)
                )
                TelemetryKeyValuePair(
                    label = AppConstants.FILE_SAMP_DLL,
                    value = if (verification.hasSampDll) {
                        stringResource(R.string.file_found)
                    } else {
                        stringResource(R.string.file_missing)
                    },
                    modifier = Modifier.weight(1f)
                )
                TelemetryKeyValuePair(
                    label = AppConstants.FILE_GTA3_IMG_REL,
                    value = if (verification.hasGta3Img) {
                        stringResource(R.string.file_found)
                    } else {
                        stringResource(R.string.file_missing)
                    },
                    modifier = Modifier.weight(1f)
                )
            }

            if (verification.statusMessage.isNotEmpty()) {
                Text(
                    text = verification.statusMessage,
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondaryNeutral
                )
            }
        }

        // 2. SA-MP Connection & Launch Settings
        val serverTone = when {
            isQueryingServer -> SemanticTone.WARNING
            queryResponse.isOnline -> SemanticTone.SUCCESS
            else -> SemanticTone.NEUTRAL
        }
        val serverBadge = when {
            isQueryingServer -> "PING..."
            queryResponse.isOnline -> "${queryResponse.activePlayers}/${queryResponse.maxPlayers} (${queryResponse.rttPingMs} ms)"
            else -> "SA-MP 0.3.7"
        }

        SectionPanel(
            title = stringResource(R.string.section_samp_settings),
            badgeText = serverBadge,
            badgeTone = serverTone
        ) {
            OutlinedTextField(
                value = playerNickname,
                onValueChange = onNicknameChange,
                label = { Text(stringResource(R.string.label_player_nickname)) },
                placeholder = { Text(stringResource(R.string.hint_player_nickname)) },
                singleLine = true,
                modifier = Modifier
                    .fillMaxWidth()
                    .testTag(AppConstants.TAG_INPUT_NICKNAME)
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM)
            ) {
                OutlinedTextField(
                    value = serverHost,
                    onValueChange = onHostChange,
                    label = { Text(stringResource(R.string.label_server_address)) },
                    placeholder = { Text(stringResource(R.string.hint_server_address)) },
                    singleLine = true,
                    modifier = Modifier
                        .weight(1.8f)
                        .testTag(AppConstants.TAG_INPUT_SERVER_HOST)
                )
                OutlinedTextField(
                    value = serverPort,
                    onValueChange = onPortChange,
                    label = { Text(stringResource(R.string.label_server_port)) },
                    placeholder = { Text(stringResource(R.string.hint_server_port)) },
                    singleLine = true,
                    modifier = Modifier
                        .weight(1f)
                        .testTag(AppConstants.TAG_INPUT_SERVER_PORT)
                )
            }

            OutlinedTextField(
                value = serverPassword,
                onValueChange = onPasswordChange,
                label = { Text(stringResource(R.string.label_server_password)) },
                singleLine = true,
                modifier = Modifier.fillMaxWidth()
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM)
            ) {
                OutlinedButton(
                    onClick = onQueryServer,
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_QUERY_SERVER),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_check_server))
                }
                OutlinedButton(
                    onClick = onSaveServer,
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_SAVE_SERVER),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_save_server))
                }
            }

            if (queryResponse.isOnline) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(SlateSurfaceElevated, RoundedCornerShape(Dimens.RadiusSM))
                        .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusSM))
                        .padding(Dimens.SpaceMD),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = queryResponse.hostname,
                            style = MaterialTheme.typography.bodyMedium,
                            color = TextPrimaryOffWhite
                        )
                        Text(
                            text = "${queryResponse.gamemode} | ${queryResponse.activePlayers}/${queryResponse.maxPlayers} Pemain",
                            style = MaterialTheme.typography.bodySmall,
                            color = TextSecondaryNeutral
                        )
                    }
                    StatusBadge(
                        text = "${queryResponse.rttPingMs} ms",
                        tone = SemanticTone.SUCCESS
                    )
                }
            }

            if (savedServers.isNotEmpty()) {
                HorizontalDivider(color = SlateBorder)
                Text(
                    text = stringResource(R.string.label_saved_servers),
                    style = MaterialTheme.typography.labelLarge,
                    color = TextSecondaryNeutral
                )
                savedServers.forEach { ep ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(SlateSurfaceElevated, RoundedCornerShape(Dimens.RadiusSM))
                            .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusSM))
                            .clickable { onSelectSavedServer(ep) }
                            .padding(horizontal = Dimens.SpaceMD, vertical = Dimens.SpaceSM),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = ep.label,
                                style = MaterialTheme.typography.bodyMedium,
                                color = TextPrimaryOffWhite
                            )
                            Text(
                                text = "${ep.address}:${ep.port}",
                                style = MaterialTheme.typography.bodySmall,
                                color = TextSecondaryNeutral
                            )
                        }
                        if (savedServers.size > 1) {
                            OutlinedButton(
                                onClick = { onRemoveSavedServer(ep) },
                                modifier = Modifier.heightIn(min = Dimens.MinTouchTarget),
                                shape = RoundedCornerShape(Dimens.RadiusXS)
                            ) {
                                Text(stringResource(R.string.action_remove_server))
                            }
                        }
                    }
                }
            }

            HorizontalDivider(color = SlateBorder)

            // Primary Start Buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(Dimens.SpaceSM)
            ) {
                Button(
                    onClick = onStartSamp,
                    modifier = Modifier
                        .weight(1.2f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_START_SAMP),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = AccentMonochromeLight,
                        contentColor = SlateBackground
                    ),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_start_samp))
                }

                Button(
                    onClick = onStartSingleplayer,
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = Dimens.MinTouchTarget)
                        .testTag(AppConstants.TAG_BTN_START_SP),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = AccentDarkContainer,
                        contentColor = TextPrimaryOffWhite
                    ),
                    shape = RoundedCornerShape(Dimens.RadiusSM)
                ) {
                    Text(stringResource(R.string.action_start_singleplayer))
                }
            }
        }
    }
}
