package com.example.ui.viewmodel

import android.app.Application
import android.net.Uri
import android.provider.DocumentsContract
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.core.constants.AppConstants
import com.example.domain.model.BinaryValidationState
import com.example.domain.model.GameDirectoryVerification
import com.example.domain.model.SampQueryResponse
import com.example.domain.model.SampServerEndpoint
import com.example.engine.jni.VenturaNativeBridge
import com.example.engine.network.SampUdpClient
import com.example.engine.storage.SafGameDirectoryResolver
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class VenturaUiState(
    val isRunnerActive: Boolean = false,
    val isMultiplayerSession: Boolean = true,
    val directoryPath: String = "/storage/emulated/0/GTA San Andreas",
    val directoryVerification: GameDirectoryVerification = GameDirectoryVerification(
        directoryPath = "/storage/emulated/0/GTA San Andreas"
    ),
    val playerNickname: String = "Carl_Johnson",
    val serverHostInput: String = AppConstants.SAMP_DEFAULT_HOST,
    val serverPortInput: String = AppConstants.SAMP_DEFAULT_PORT.toString(),
    val serverPasswordInput: String = "",
    val isQueryingServer: Boolean = false,
    val savedServers: List<SampServerEndpoint> = listOf(
        SampServerEndpoint("127.0.0.1", 7777, "Local SA-MP Server (127.0.0.1:7777)")
    ),
    val sampQueryResult: SampQueryResponse = SampQueryResponse()
)

class VenturaViewModel(application: Application) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences("ventura_launcher_prefs", 0)
    private val _uiState = MutableStateFlow(VenturaUiState())
    val uiState: StateFlow<VenturaUiState> = _uiState.asStateFlow()
    private var activeTreeUri: Uri? = null

    init {
        SampUdpClient.ensureLocalLoopbackResponder()
        val savedUriStr = prefs.getString("saved_tree_uri", null)
        val savedPath = prefs.getString("saved_dir_path", null)
        if (!savedUriStr.isNullOrEmpty()) {
            activeTreeUri = try { Uri.parse(savedUriStr) } catch (_: Exception) { null }
        }
        if (!savedPath.isNullOrEmpty()) {
            _uiState.update { it.copy(directoryPath = savedPath) }
        }
        verifyGameDirectory()
    }

    fun updateDirectoryPath(newPath: String) {
        activeTreeUri = null
        prefs.edit().remove("saved_tree_uri").putString("saved_dir_path", newPath).apply()
        _uiState.update { it.copy(directoryPath = newPath) }
    }

    fun onFolderTreePicked(treeUri: Uri) {
        activeTreeUri = treeUri
        val resolvedPath = resolveTreeUriToFilePath(treeUri).ifEmpty { treeUri.toString() }
        prefs.edit()
            .putString("saved_tree_uri", treeUri.toString())
            .putString("saved_dir_path", resolvedPath)
            .apply()
        _uiState.update {
            it.copy(
                directoryPath = resolvedPath,
                directoryVerification = it.directoryVerification.copy(
                    statusMessage = "Memeriksa dan menyinkronkan file biner dari folder SAF..."
                )
            )
        }
        viewModelScope.launch(Dispatchers.IO) {
            val verification = SafGameDirectoryResolver.resolveAndVerifyFromTreeUri(
                context = getApplication(),
                treeUri = treeUri,
                displayPath = resolvedPath
            )
            _uiState.update {
                it.copy(directoryVerification = verification)
            }
        }
    }

    private fun resolveTreeUriToFilePath(treeUri: Uri): String {
        return try {
            val docId = DocumentsContract.getTreeDocumentId(treeUri)
            val split = docId.split(":")
            if (split.size >= 2) {
                val type = split[0]
                val relPath = split[1]
                if ("primary".equals(type, ignoreCase = true)) {
                    "/storage/emulated/0/$relPath"
                } else {
                    "/storage/$type/$relPath"
                }
            } else {
                treeUri.path ?: ""
            }
        } catch (_: Exception) {
            treeUri.path ?: ""
        }
    }

    fun verifyGameDirectory() {
        val path = _uiState.value.directoryPath.trim()
        val currentUri = activeTreeUri
        viewModelScope.launch(Dispatchers.IO) {
            val verification = if (currentUri != null) {
                SafGameDirectoryResolver.resolveAndVerifyFromTreeUri(
                    context = getApplication(),
                    treeUri = currentUri,
                    displayPath = path
                )
            } else {
                SafGameDirectoryResolver.verifyFromRawFilesystem(
                    context = getApplication(),
                    rawPath = path
                )
            }
            _uiState.update {
                it.copy(directoryVerification = verification)
            }
        }
    }

    fun updatePlayerNickname(name: String) {
        _uiState.update { it.copy(playerNickname = name) }
    }

    fun updateServerHost(host: String) {
        _uiState.update { it.copy(serverHostInput = host) }
    }

    fun updateServerPort(portStr: String) {
        val filtered = portStr.filter { it.isDigit() }.take(5)
        _uiState.update { it.copy(serverPortInput = filtered) }
    }

    fun updateServerPassword(password: String) {
        _uiState.update { it.copy(serverPasswordInput = password) }
    }

    fun saveCurrentServerEndpoint() {
        val state = _uiState.value
        val host = state.serverHostInput.trim().ifEmpty { AppConstants.SAMP_DEFAULT_HOST }
        val port = state.serverPortInput.toIntOrNull() ?: AppConstants.SAMP_DEFAULT_PORT
        val label = state.sampQueryResult.hostname.ifEmpty { "$host:$port" }

        val updated = state.savedServers.toMutableList()
        if (updated.none { it.address == host && it.port == port }) {
            updated.add(SampServerEndpoint(host, port, label))
            _uiState.update { it.copy(savedServers = updated) }
        }
    }

    fun selectSavedServer(endpoint: SampServerEndpoint) {
        _uiState.update {
            it.copy(
                serverHostInput = endpoint.address,
                serverPortInput = endpoint.port.toString()
            )
        }
        querySampServerUdp()
    }

    fun removeSavedServer(endpoint: SampServerEndpoint) {
        _uiState.update { state ->
            state.copy(savedServers = state.savedServers.filterNot { it == endpoint })
        }
    }

    fun querySampServerUdp() {
        val host = _uiState.value.serverHostInput.trim().ifEmpty { AppConstants.SAMP_DEFAULT_HOST }
        val port = _uiState.value.serverPortInput.toIntOrNull() ?: AppConstants.SAMP_DEFAULT_PORT

        _uiState.update { it.copy(isQueryingServer = true) }
        viewModelScope.launch(Dispatchers.IO) {
            val response = SampUdpClient.queryServer(host, port)
            _uiState.update {
                it.copy(
                    isQueryingServer = false,
                    sampQueryResult = response
                )
            }
        }
    }

    fun startGameRunner(multiplayerSamp: Boolean) {
        val state = _uiState.value
        val host = state.serverHostInput.trim().ifEmpty { AppConstants.SAMP_DEFAULT_HOST }
        val port = state.serverPortInput.toIntOrNull() ?: AppConstants.SAMP_DEFAULT_PORT
        val nickname = state.playerNickname.trim().ifEmpty { "Carl_Johnson" }
        val effectiveGameDir = state.directoryVerification.stagedRuntimePath.ifEmpty {
            state.directoryPath.trim()
        }

        viewModelScope.launch(Dispatchers.IO) {
            VenturaNativeBridge.startNativeGameSession(
                gameDir = effectiveGameDir,
                multiplayerSamp = multiplayerSamp,
                serverIp = host,
                serverPort = port,
                nickname = nickname,
                password = state.serverPasswordInput
            )
        }

        _uiState.update {
            it.copy(
                isRunnerActive = true,
                isMultiplayerSession = multiplayerSamp
            )
        }
    }

    fun exitGameRunner() {
        viewModelScope.launch(Dispatchers.IO) {
            VenturaNativeBridge.stopNativeGameSession()
        }
        _uiState.update {
            it.copy(isRunnerActive = false)
        }
    }

    fun sendVirtualKey(virtualKey: Int, isDown: Boolean) {
        VenturaNativeBridge.sendWindowsKeyEvent(virtualKey, isDown)
    }

    fun sendKeyTap(virtualKey: Int) {
        VenturaNativeBridge.sendWindowsKeyEvent(virtualKey, true)
        VenturaNativeBridge.sendWindowsKeyEvent(virtualKey, false)
    }

    fun sendTypedCharacter(charCode: Int) {
        VenturaNativeBridge.sendWindowsCharEvent(charCode)
    }

    override fun onCleared() {
        super.onCleared()
        VenturaNativeBridge.stopNativeGameSession()
        SampUdpClient.shutdownLocalResponder()
    }
}
