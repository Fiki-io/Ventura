package com.example.engine.storage

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import com.example.core.constants.AppConstants
import com.example.domain.model.BinaryValidationState
import com.example.domain.model.GameDirectoryVerification
import com.example.engine.archive.Ver2ImgReader
import com.example.engine.pe.Pe32BinaryInspector
import java.io.File
import java.io.FileOutputStream

object SafGameDirectoryResolver {

    private data class SafDocItem(
        val docId: String,
        val name: String,
        val relativePath: String,
        val mimeType: String,
        val sizeBytes: Long,
        val uri: Uri
    ) {
        val isDirectory: Boolean
            get() = mimeType == DocumentsContract.Document.MIME_TYPE_DIR
    }

    fun getStagedRuntimeDir(context: Context): File {
        val dir = File(context.filesDir, "ventura_runtime_dir")
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    fun resolveAndVerifyFromTreeUri(
        context: Context,
        treeUri: Uri,
        displayPath: String
    ): GameDirectoryVerification {
        val resolver = context.contentResolver
        try {
            resolver.takePersistableUriPermission(
                treeUri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
        } catch (_: Exception) {
        }

        val rootDocId = try {
            DocumentsContract.getTreeDocumentId(treeUri)
        } catch (e: Exception) {
            return GameDirectoryVerification(
                directoryPath = displayPath,
                statusMessage = "Gagal membaca izin folder SAF: ${e.message}"
            )
        }

        val allFiles = mutableListOf<SafDocItem>()
        collectSafDocumentsRecursive(
            context = context,
            treeUri = treeUri,
            parentDocId = rootDocId,
            parentRelPath = "",
            depth = 0,
            maxDepth = 3,
            outList = allFiles
        )

        if (allFiles.isEmpty()) {
            return verifyFromRawFilesystem(context, displayPath)
        }

        // 1. Find gta_sa.exe (exact name first, then any main game .exe)
        val gtaDoc = allFiles.firstOrNull {
            !it.isDirectory && (
                it.name.equals("gta_sa.exe", ignoreCase = true) ||
                it.name.equals("gta-sa.exe", ignoreCase = true) ||
                it.name.equals("gtasa.exe", ignoreCase = true)
            )
        } ?: allFiles.firstOrNull {
            !it.isDirectory &&
                it.name.endsWith(".exe", ignoreCase = true) &&
                !it.name.contains("samp", ignoreCase = true) &&
                !it.name.contains("uninst", ignoreCase = true) &&
                !it.name.contains("vorbis", ignoreCase = true) &&
                it.sizeBytes > 1_000_000L
        }

        // 2. Find samp.dll (root or inside SAMP/ subfolder)
        val sampDoc = allFiles.firstOrNull {
            !it.isDirectory && it.name.equals("samp.dll", ignoreCase = true)
        }

        // 3. Find models/gta3.img (inside models/ or anywhere in the folder)
        val gta3ImgDoc = allFiles.firstOrNull {
            !it.isDirectory && it.name.equals("gta3.img", ignoreCase = true)
        } ?: allFiles.firstOrNull {
            !it.isDirectory && it.name.endsWith(".img", ignoreCase = true)
        }

        val runtimeDir = getStagedRuntimeDir(context)
        var hasValidGta = false
        var gtaEntryVa = 0L
        var gtaImageBase = AppConstants.GTASA_IMAGE_BASE
        var hasValidSamp = false
        var sampBaseVa = 0L
        var hasValidImg = false

        // Stage & Inspect gta_sa.exe via ContentResolver stream
        if (gtaDoc != null) {
            val stagedExe = File(runtimeDir, AppConstants.FILE_GTA_SA_EXE)
            if (copySafUriToFile(context, gtaDoc.uri, stagedExe, maxBytes = 32L * 1024L * 1024L)) {
                val headerBytes = readFirstBytes(stagedExe, 262144)
                val analysis = Pe32BinaryInspector.inspectBytes(
                    fileName = gtaDoc.name,
                    filePath = stagedExe.absolutePath,
                    bytes = headerBytes
                )
                if (analysis.validationState == BinaryValidationState.VALID_PE32) {
                    hasValidGta = true
                    gtaEntryVa = analysis.entryPointVa
                    gtaImageBase = analysis.imageBaseVa
                }
            }
        }

        // Stage & Inspect samp.dll via ContentResolver stream
        if (sampDoc != null) {
            val stagedDll = File(runtimeDir, AppConstants.FILE_SAMP_DLL)
            if (copySafUriToFile(context, sampDoc.uri, stagedDll, maxBytes = 16L * 1024L * 1024L)) {
                val headerBytes = readFirstBytes(stagedDll, 262144)
                val analysis = Pe32BinaryInspector.inspectBytes(
                    fileName = sampDoc.name,
                    filePath = stagedDll.absolutePath,
                    bytes = headerBytes
                )
                if (analysis.validationState == BinaryValidationState.VALID_PE32) {
                    hasValidSamp = true
                    sampBaseVa = analysis.imageBaseVa
                }
            }
        }

        // Also stage auxiliary small DLLs & data config files so C++ CreateFileA can open them
        allFiles.filter {
            !it.isDirectory &&
                (it.name.endsWith(".dll", ignoreCase = true) ||
                 it.name.endsWith(".asi", ignoreCase = true) ||
                 it.name.endsWith(".dat", ignoreCase = true) ||
                 it.name.endsWith(".cfg", ignoreCase = true) ||
                 it.name.endsWith(".ide", ignoreCase = true) ||
                 it.name.endsWith(".ipl", ignoreCase = true)) &&
                it.sizeBytes in 1L..(4L * 1024L * 1024L)
        }.take(24).forEach { doc ->
            val targetFile = File(runtimeDir, doc.relativePath.ifEmpty { doc.name })
            targetFile.parentFile?.mkdirs()
            if (!targetFile.exists() || targetFile.length() != doc.sizeBytes) {
                copySafUriToFile(context, doc.uri, targetFile, maxBytes = 4L * 1024L * 1024L)
            }
        }

        // Inspect & stage header slice of models/gta3.img via ContentResolver stream
        if (gta3ImgDoc != null) {
            val imgHeaderBytes = readSafUriPrefix(context, gta3ImgDoc.uri, 65536)
            val imgReport = Ver2ImgReader.parseImgHeaderBytes(gta3ImgDoc.relativePath, imgHeaderBytes)
            hasValidImg = imgReport.isLoaded || imgHeaderBytes.size >= 8
            val stagedImg = File(runtimeDir, AppConstants.FILE_GTA3_IMG_REL)
            stagedImg.parentFile?.mkdirs()
            if (imgHeaderBytes.isNotEmpty()) {
                try {
                    stagedImg.writeBytes(imgHeaderBytes)
                } catch (_: Exception) {
                }
            }
        }

        val exeNamesFound = allFiles
            .filter { !it.isDirectory && (it.name.endsWith(".exe", true) || it.name.endsWith(".dll", true)) }
            .map { it.relativePath }
            .take(5)

        val msg = when {
            hasValidGta && hasValidSamp ->
                "Siap dijalankan: ${gtaDoc?.relativePath} (0x${gtaImageBase.toString(16).uppercase()}) & ${sampDoc?.relativePath} terverifikasi."
            hasValidGta && !hasValidSamp ->
                "Singleplayer siap (${gtaDoc?.relativePath} terdeteksi). File samp.dll belum ditemukan di folder ini."
            !hasValidGta && hasValidSamp ->
                "samp.dll ditemukan (${sampDoc?.relativePath}), tetapi file gta_sa.exe belum ada di folder ini (ditemukan: ${exeNamesFound.joinToString(", ")})."
            else ->
                if (exeNamesFound.isNotEmpty()) {
                    "gta_sa.exe belum ditemukan di folder ini. File biner yang terdeteksi: ${exeNamesFound.joinToString(", ")}"
                } else {
                    "Pilih folder instalasi GTA San Andreas PC yang berisi gta_sa.exe dan samp.dll."
                }
        }

        return GameDirectoryVerification(
            directoryPath = displayPath,
            stagedRuntimePath = runtimeDir.absolutePath,
            hasGtaSaExe = hasValidGta,
            hasSampDll = hasValidSamp,
            hasGta3Img = hasValidImg,
            gtaSaEntryPointVa = gtaEntryVa,
            sampImageBaseVa = sampBaseVa,
            statusMessage = msg
        )
    }

    fun verifyFromRawFilesystem(context: Context, rawPath: String): GameDirectoryVerification {
        val dir = File(rawPath)
        if (!dir.exists() || !dir.isDirectory) {
            return GameDirectoryVerification(
                directoryPath = rawPath,
                statusMessage = "Folder tidak ditemukan atau memerlukan pemilihan melalui tombol 'Pilih Folder'."
            )
        }

        val allFiles = mutableListOf<File>()
        collectRawFilesRecursive(dir, 0, 3, allFiles)
        if (allFiles.isEmpty()) {
            return GameDirectoryVerification(
                directoryPath = rawPath,
                statusMessage = "Akses langsung dibatasi Android Scoped Storage. Silakan tekan tombol 'Pilih Folder' lalu pilih 'USE THIS FOLDER'."
            )
        }

        val gtaFile = allFiles.firstOrNull {
            it.name.equals("gta_sa.exe", ignoreCase = true) ||
                it.name.equals("gta-sa.exe", ignoreCase = true)
        } ?: allFiles.firstOrNull {
            it.name.endsWith(".exe", ignoreCase = true) &&
                !it.name.contains("samp", ignoreCase = true) &&
                !it.name.contains("uninst", ignoreCase = true) &&
                it.length() > 1_000_000L
        }

        val sampFile = allFiles.firstOrNull {
            it.name.equals("samp.dll", ignoreCase = true)
        }

        val imgFile = allFiles.firstOrNull {
            it.name.equals("gta3.img", ignoreCase = true)
        }

        val runtimeDir = getStagedRuntimeDir(context)
        var hasValidGta = false
        var gtaEntryVa = 0L
        var gtaImageBase = AppConstants.GTASA_IMAGE_BASE
        var hasValidSamp = false
        var sampBaseVa = 0L

        if (gtaFile != null && gtaFile.canRead()) {
            val headerBytes = readFirstBytes(gtaFile, 262144)
            val analysis = Pe32BinaryInspector.inspectBytes(gtaFile.name, gtaFile.absolutePath, headerBytes)
            if (analysis.validationState == BinaryValidationState.VALID_PE32) {
                hasValidGta = true
                gtaEntryVa = analysis.entryPointVa
                gtaImageBase = analysis.imageBaseVa
                val stagedExe = File(runtimeDir, AppConstants.FILE_GTA_SA_EXE)
                if (gtaFile.absolutePath != stagedExe.absolutePath) {
                    try {
                        gtaFile.copyTo(stagedExe, overwrite = true)
                    } catch (_: Exception) {
                    }
                }
            }
        }

        if (sampFile != null && sampFile.canRead()) {
            val headerBytes = readFirstBytes(sampFile, 262144)
            val analysis = Pe32BinaryInspector.inspectBytes(sampFile.name, sampFile.absolutePath, headerBytes)
            if (analysis.validationState == BinaryValidationState.VALID_PE32) {
                hasValidSamp = true
                sampBaseVa = analysis.imageBaseVa
                val stagedDll = File(runtimeDir, AppConstants.FILE_SAMP_DLL)
                if (sampFile.absolutePath != stagedDll.absolutePath) {
                    try {
                        sampFile.copyTo(stagedDll, overwrite = true)
                    } catch (_: Exception) {
                    }
                }
            }
        }

        val hasImg = if (imgFile != null && imgFile.canRead()) {
            val headerBytes = readFirstBytes(imgFile, 65536)
            val report = Ver2ImgReader.parseImgHeaderBytes(imgFile.absolutePath, headerBytes)
            report.isLoaded || headerBytes.size >= 8
        } else {
            false
        }

        val msg = when {
            hasValidGta && hasValidSamp ->
                "gta_sa.exe (0x${gtaImageBase.toString(16).uppercase()}) & samp.dll siap dijalankan."
            hasValidGta ->
                "gta_sa.exe terdeteksi (samp.dll belum ditemukan di folder ini)."
            else ->
                "Tekan tombol 'Pilih Folder' dan pilih folder instalasi GTA San Andreas PC Anda."
        }

        return GameDirectoryVerification(
            directoryPath = rawPath,
            stagedRuntimePath = runtimeDir.absolutePath,
            hasGtaSaExe = hasValidGta,
            hasSampDll = hasValidSamp,
            hasGta3Img = hasImg,
            gtaSaEntryPointVa = gtaEntryVa,
            sampImageBaseVa = sampBaseVa,
            statusMessage = msg
        )
    }

    private fun collectSafDocumentsRecursive(
        context: Context,
        treeUri: Uri,
        parentDocId: String,
        parentRelPath: String,
        depth: Int,
        maxDepth: Int,
        outList: MutableList<SafDocItem>
    ) {
        if (depth > maxDepth) return
        val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId)
        val projection = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE
        )

        try {
            context.contentResolver.query(childrenUri, projection, null, null, null)?.use { cursor ->
                val idCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID)
                val nameCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME)
                val mimeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
                val sizeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_SIZE)

                while (cursor.moveToNext()) {
                    val docId = if (idCol >= 0) cursor.getString(idCol) ?: continue else continue
                    val name = if (nameCol >= 0) cursor.getString(nameCol) ?: "" else ""
                    val mime = if (mimeCol >= 0) cursor.getString(mimeCol) ?: "" else ""
                    val size = if (sizeCol >= 0 && !cursor.isNull(sizeCol)) cursor.getLong(sizeCol) else 0L
                    if (name.isEmpty()) continue

                    val relPath = if (parentRelPath.isEmpty()) name else "$parentRelPath/$name"
                    val docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId)
                    val item = SafDocItem(
                        docId = docId,
                        name = name,
                        relativePath = relPath,
                        mimeType = mime,
                        sizeBytes = size,
                        uri = docUri
                    )
                    outList.add(item)

                    if (item.isDirectory && depth < maxDepth) {
                        val lowerDir = name.lowercase()
                        if (lowerDir == "models" || lowerDir == "samp" || lowerDir == "data" ||
                            lowerDir.contains("gta") || lowerDir.startsWith("$") || depth == 0
                        ) {
                            collectSafDocumentsRecursive(
                                context = context,
                                treeUri = treeUri,
                                parentDocId = docId,
                                parentRelPath = relPath,
                                depth = depth + 1,
                                maxDepth = maxDepth,
                                outList = outList
                            )
                        }
                    }
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun collectRawFilesRecursive(dir: File, depth: Int, maxDepth: Int, out: MutableList<File>) {
        if (depth > maxDepth) return
        val children = try {
            dir.listFiles()
        } catch (_: Exception) {
            null
        } ?: return

        for (f in children) {
            if (f.isFile) {
                out.add(f)
            } else if (f.isDirectory && depth < maxDepth) {
                collectRawFilesRecursive(f, depth + 1, maxDepth, out)
            }
        }
    }

    private fun copySafUriToFile(context: Context, sourceUri: Uri, destFile: File, maxBytes: Long): Boolean {
        return try {
            destFile.parentFile?.mkdirs()
            context.contentResolver.openInputStream(sourceUri)?.use { input ->
                FileOutputStream(destFile).use { output ->
                    val buf = ByteArray(65536)
                    var total = 0L
                    while (total < maxBytes) {
                        val read = input.read(buf)
                        if (read <= 0) break
                        output.write(buf, 0, read)
                        total += read
                    }
                    output.flush()
                }
            }
            destFile.exists() && destFile.length() > 0L
        } catch (_: Exception) {
            false
        }
    }

    private fun readSafUriPrefix(context: Context, sourceUri: Uri, maxBytes: Int): ByteArray {
        return try {
            context.contentResolver.openInputStream(sourceUri)?.use { input ->
                val buf = ByteArray(maxBytes)
                var offset = 0
                while (offset < maxBytes) {
                    val read = input.read(buf, offset, maxBytes - offset)
                    if (read <= 0) break
                    offset += read
                }
                if (offset == maxBytes) buf else buf.copyOf(offset)
            } ?: ByteArray(0)
        } catch (_: Exception) {
            ByteArray(0)
        }
    }

    private fun readFirstBytes(file: File, maxBytes: Int): ByteArray {
        return try {
            file.inputStream().use { input ->
                val len = minOf(file.length(), maxBytes.toLong()).toInt()
                val buf = ByteArray(len)
                var offset = 0
                while (offset < len) {
                    val read = input.read(buf, offset, len - offset)
                    if (read <= 0) break
                    offset += read
                }
                if (offset == len) buf else buf.copyOf(offset)
            }
        } catch (_: Exception) {
            ByteArray(0)
        }
    }
}
