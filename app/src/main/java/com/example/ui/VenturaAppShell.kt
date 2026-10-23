package com.example.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.example.R
import com.example.ui.components.SemanticTone
import com.example.ui.components.StatusBadge
import com.example.ui.screens.GameRunnerScreen
import com.example.ui.screens.LauncherScreen
import com.example.ui.theme.Dimens
import com.example.ui.theme.SlateBackground
import com.example.ui.theme.SlateBorder
import com.example.ui.theme.SlateSurface
import com.example.ui.theme.TextPrimaryOffWhite
import com.example.ui.theme.TextSecondaryNeutral
import com.example.ui.viewmodel.VenturaViewModel

@Composable
fun VenturaAppShell(
    viewModel: VenturaViewModel,
    modifier: Modifier = Modifier
) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()

    val folderPickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { treeUri ->
        if (treeUri != null) {
            viewModel.onFolderTreePicked(treeUri)
        }
    }

    if (state.isRunnerActive) {
        BackHandler {
            viewModel.exitGameRunner()
        }
        GameRunnerScreen(
            onSendVirtualKey = viewModel::sendVirtualKey,
            onSendKeyTap = viewModel::sendKeyTap,
            onSendTypedChar = viewModel::sendTypedCharacter,
            onExitRunner = viewModel::exitGameRunner,
            modifier = modifier.fillMaxSize()
        )
    } else {
        Scaffold(
            modifier = modifier.fillMaxSize(),
            containerColor = SlateBackground,
            contentWindowInsets = WindowInsets.safeDrawing
        ) { innerPadding ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding),
                contentAlignment = Alignment.TopCenter
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .widthIn(max = Dimens.MaxContentWidth)
                        .verticalScroll(rememberScrollState())
                        .padding(Dimens.SpaceLG),
                    verticalArrangement = Arrangement.spacedBy(Dimens.SpaceLG)
                ) {
                    VenturaHeaderBanner()

                    LauncherScreen(
                        directoryPath = state.directoryPath,
                        verification = state.directoryVerification,
                        playerNickname = state.playerNickname,
                        serverHost = state.serverHostInput,
                        serverPort = state.serverPortInput,
                        serverPassword = state.serverPasswordInput,
                        isQueryingServer = state.isQueryingServer,
                        savedServers = state.savedServers,
                        queryResponse = state.sampQueryResult,
                        onDirectoryPathChange = viewModel::updateDirectoryPath,
                        onPickFolder = { folderPickerLauncher.launch(null) },
                        onVerifyFolder = viewModel::verifyGameDirectory,
                        onNicknameChange = viewModel::updatePlayerNickname,
                        onHostChange = viewModel::updateServerHost,
                        onPortChange = viewModel::updateServerPort,
                        onPasswordChange = viewModel::updateServerPassword,
                        onQueryServer = viewModel::querySampServerUdp,
                        onSaveServer = viewModel::saveCurrentServerEndpoint,
                        onSelectSavedServer = viewModel::selectSavedServer,
                        onRemoveSavedServer = viewModel::removeSavedServer,
                        onStartSamp = { viewModel.startGameRunner(multiplayerSamp = true) },
                        onStartSingleplayer = { viewModel.startGameRunner(multiplayerSamp = false) }
                    )
                }
            }
        }
    }
}

@Composable
private fun VenturaHeaderBanner(
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(SlateSurface, RoundedCornerShape(Dimens.RadiusMD))
            .border(Dimens.BorderThin, SlateBorder, RoundedCornerShape(Dimens.RadiusMD))
            .padding(Dimens.SpaceLG),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = stringResource(R.string.app_name),
                style = MaterialTheme.typography.headlineMedium,
                color = TextPrimaryOffWhite
            )
            Text(
                text = stringResource(R.string.app_subtitle),
                style = MaterialTheme.typography.bodyMedium,
                color = TextSecondaryNeutral
            )
        }
        StatusBadge(
            text = stringResource(R.string.arch_badge_arm64),
            tone = SemanticTone.NEUTRAL
        )
    }
}
