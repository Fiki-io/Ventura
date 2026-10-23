package com.example.domain.model

enum class BinaryValidationState {
    MISSING,
    VALID_PE32,
    INVALID_FORMAT
}

data class PeSectionDescriptor(
    val name: String,
    val virtualAddressRva: Long,
    val virtualSize: Long,
    val rawDataOffset: Long,
    val rawDataSize: Long,
    val characteristicsFlags: Long
)

data class DllImportDescriptor(
    val dllName: String,
    val iatRva: Long,
    val isDynamicLoadLibrary: Boolean,
    val functions: List<String>,
    val nativeBridgeModule: String
)

data class PeBinaryAnalysis(
    val fileName: String,
    val filePath: String,
    val validationState: BinaryValidationState,
    val fileSizeBytes: Long = 0L,
    val machineArchitecture: Int = 0,
    val imageBaseVa: Long = 0L,
    val entryPointRva: Long = 0L,
    val entryPointVa: Long = 0L,
    val sizeOfImageBytes: Long = 0L,
    val crc32Checksum: Long = 0L,
    val sections: List<PeSectionDescriptor> = emptyList(),
    val staticImports: List<DllImportDescriptor> = emptyList(),
    val dynamicImports: List<DllImportDescriptor> = emptyList(),
    val errorDetail: String = ""
)

data class GameDirectoryVerification(
    val directoryPath: String = "",
    val stagedRuntimePath: String = "",
    val hasGtaSaExe: Boolean = false,
    val hasSampDll: Boolean = false,
    val hasGta3Img: Boolean = false,
    val gtaSaEntryPointVa: Long = 0L,
    val sampImageBaseVa: Long = 0L,
    val statusMessage: String = ""
) {
    val isReadyForSingleplayer: Boolean
        get() = hasGtaSaExe

    val isReadyForSamp: Boolean
        get() = hasGtaSaExe && hasSampDll
}

data class SampServerEndpoint(
    val address: String,
    val port: Int,
    val label: String
)

data class SampRuleEntry(
    val ruleKey: String,
    val ruleValue: String
)

data class SampPlayerEntry(
    val playerId: Int,
    val playerName: String,
    val score: Int,
    val pingMs: Int
)

data class SampQueryResponse(
    val isOnline: Boolean = false,
    val resolvedIp: String = "",
    val port: Int = 7777,
    val hasPassword: Boolean = false,
    val activePlayers: Int = 0,
    val maxPlayers: Int = 0,
    val rttPingMs: Long = 0L,
    val hostname: String = "",
    val gamemode: String = "",
    val language: String = "",
    val rawRequestHex: String = "",
    val rawResponseBytes: Int = 0,
    val rules: List<SampRuleEntry> = emptyList(),
    val onlinePlayers: List<SampPlayerEntry> = emptyList(),
    val errorDetail: String = ""
)

data class RenderWareVertex3D(
    val x: Float,
    val y: Float,
    val z: Float
)

data class RenderWareTriangle(
    val v0: Int,
    val v1: Int,
    val v2: Int
)

data class ImgEntryItem(
    val fileName: String,
    val sectorOffset: Long,
    val byteOffset: Long,
    val byteSize: Long,
    val isDffModel: Boolean,
    val isTxdTexture: Boolean,
    val rwSectionType: Int,
    val rwVersion: Int,
    val vertexCount: Int = 0,
    val triangleCount: Int = 0,
    val vertices: List<RenderWareVertex3D> = emptyList(),
    val triangles: List<RenderWareTriangle> = emptyList()
)

data class ImgArchiveReport(
    val isLoaded: Boolean = false,
    val archivePath: String = "",
    val magicSignature: String = "",
    val totalEntries: Int = 0,
    val dffModelCount: Int = 0,
    val txdTextureCount: Int = 0,
    val entries: List<ImgEntryItem> = emptyList(),
    val selectedAsset: ImgEntryItem? = null,
    val errorMessage: String = ""
)
