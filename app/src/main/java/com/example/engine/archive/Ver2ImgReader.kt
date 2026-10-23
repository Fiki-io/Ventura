package com.example.engine.archive

import com.example.core.constants.AppConstants
import com.example.domain.model.ImgArchiveReport
import com.example.domain.model.ImgEntryItem
import com.example.domain.model.RenderWareTriangle
import com.example.domain.model.RenderWareVertex3D
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

object Ver2ImgReader {

    fun parseImgHeaderBytes(archivePath: String, headerAndDirBytes: ByteArray): ImgArchiveReport {
        if (headerAndDirBytes.size < 8) {
            return ImgArchiveReport(
                isLoaded = false,
                archivePath = archivePath,
                errorMessage = "Ukuran file .img terlalu kecil (< 8 byte)."
            )
        }
        val buf = ByteBuffer.wrap(headerAndDirBytes).order(ByteOrder.LITTLE_ENDIAN)
        val magicStr = String(headerAndDirBytes, 0, 4, StandardCharsets.US_ASCII)
        val totalEntries = buf.getInt(4)
        if (magicStr != AppConstants.IMG_MAGIC_VER2 || totalEntries < 0) {
            return ImgArchiveReport(
                isLoaded = false,
                archivePath = archivePath,
                magicSignature = magicStr,
                errorMessage = "Header arsip bukan VER2 standar GTA San Andreas (ditemukan: $magicStr)."
            )
        }
        var dffCount = 0
        var txdCount = 0
        val availableRecords = minOf(totalEntries, (headerAndDirBytes.size - 8) / AppConstants.IMG_DIRECTORY_ENTRY_BYTES)
        for (i in 0 until availableRecords) {
            val base = 8 + i * AppConstants.IMG_DIRECTORY_ENTRY_BYTES
            var nameEnd = base + 8
            while (nameEnd < base + 32 && headerAndDirBytes[nameEnd].toInt() != 0) {
                nameEnd++
            }
            val assetName = String(
                headerAndDirBytes,
                base + 8,
                nameEnd - (base + 8),
                StandardCharsets.US_ASCII
            ).trim().lowercase()
            if (assetName.endsWith(".dff")) dffCount++
            if (assetName.endsWith(".txd")) txdCount++
        }
        return ImgArchiveReport(
            isLoaded = true,
            archivePath = archivePath,
            magicSignature = magicStr,
            totalEntries = totalEntries,
            dffModelCount = dffCount,
            txdTextureCount = txdCount
        )
    }

    fun parseImgArchive(imgFile: File, maxEntries: Int = 250): ImgArchiveReport {
        if (!imgFile.exists() || !imgFile.isFile) {
            return ImgArchiveReport(
                isLoaded = false,
                archivePath = imgFile.absolutePath,
                errorMessage = "File arsip tidak ditemukan: ${imgFile.absolutePath}"
            )
        }

        return try {
            RandomAccessFile(imgFile, "r").use { raf ->
                if (raf.length() < 8L) {
                    return ImgArchiveReport(
                        isLoaded = false,
                        archivePath = imgFile.absolutePath,
                        errorMessage = "Ukuran file .img terlalu kecil (< 8 byte)."
                    )
                }

                val headerBytes = ByteArray(8)
                raf.readFully(headerBytes)
                val headerBuf = ByteBuffer.wrap(headerBytes).order(ByteOrder.LITTLE_ENDIAN)
                val magicStr = String(headerBytes, 0, 4, StandardCharsets.US_ASCII)
                val totalEntries = headerBuf.getInt(4)

                if (magicStr != AppConstants.IMG_MAGIC_VER2 || totalEntries < 0) {
                    return ImgArchiveReport(
                        isLoaded = false,
                        archivePath = imgFile.absolutePath,
                        magicSignature = magicStr,
                        errorMessage = "Header arsip bukan VER2 standar GTA San Andreas (ditemukan: $magicStr)."
                    )
                }

                val countToRead = minOf(totalEntries, 65536)
                val dirBytes = ByteArray(countToRead * AppConstants.IMG_DIRECTORY_ENTRY_BYTES)
                val actualRead = raf.read(dirBytes)
                val dirBuf = ByteBuffer.wrap(dirBytes).order(ByteOrder.LITTLE_ENDIAN)

                var dffCount = 0
                var txdCount = 0
                val parsedEntries = mutableListOf<ImgEntryItem>()
                val availableRecords = actualRead / AppConstants.IMG_DIRECTORY_ENTRY_BYTES

                for (i in 0 until availableRecords) {
                    val base = i * AppConstants.IMG_DIRECTORY_ENTRY_BYTES
                    val sectorOffset = dirBuf.getInt(base).toLong() and 0xFFFFFFFFL
                    val streamingSectors = dirBuf.getShort(base + 4).toInt() and 0xFFFF
                    val archiveSectors = dirBuf.getShort(base + 6).toInt() and 0xFFFF
                    val sectors = if (streamingSectors > 0) streamingSectors else archiveSectors

                    var nameEnd = base + 8
                    while (nameEnd < base + 32 && dirBytes[nameEnd].toInt() != 0) {
                        nameEnd++
                    }
                    val assetName = String(
                        dirBytes,
                        base + 8,
                        nameEnd - (base + 8),
                        StandardCharsets.US_ASCII
                    ).trim()

                    val lower = assetName.lowercase()
                    val isDff = lower.endsWith(".dff")
                    val isTxd = lower.endsWith(".txd")
                    if (isDff) dffCount++
                    if (isTxd) txdCount++

                    if (parsedEntries.size < maxEntries) {
                        val byteOffset = sectorOffset * AppConstants.IMG_SECTOR_SIZE_BYTES
                        val byteSize = sectors.toLong() * AppConstants.IMG_SECTOR_SIZE_BYTES

                        var rwType = 0
                        var rwVer = 0
                        val vertices = mutableListOf<RenderWareVertex3D>()
                        val triangles = mutableListOf<RenderWareTriangle>()

                        if (byteOffset + 20L <= raf.length()) {
                            raf.seek(byteOffset)
                            val chunkHeader = ByteArray(minOf(512, (raf.length() - byteOffset).toInt()))
                            raf.readFully(chunkHeader)
                            val chunkBuf = ByteBuffer.wrap(chunkHeader).order(ByteOrder.LITTLE_ENDIAN)
                            rwType = chunkBuf.getInt(0)
                            rwVer = chunkBuf.getInt(8)

                            if (isDff && chunkHeader.size >= 20) {
                                val vCount = chunkBuf.getInt(12).coerceIn(0, 32)
                                val tCount = chunkBuf.getInt(16).coerceIn(0, 32)
                                var cursor = 20
                                for (v in 0 until vCount) {
                                    if (cursor + 12 <= chunkHeader.size) {
                                        vertices.add(
                                            RenderWareVertex3D(
                                                x = chunkBuf.getFloat(cursor),
                                                y = chunkBuf.getFloat(cursor + 4),
                                                z = chunkBuf.getFloat(cursor + 8)
                                            )
                                        )
                                        cursor += 12
                                    }
                                }
                                for (t in 0 until tCount) {
                                    if (cursor + 6 <= chunkHeader.size) {
                                        triangles.add(
                                            RenderWareTriangle(
                                                v0 = chunkBuf.getShort(cursor).toInt() and 0xFFFF,
                                                v1 = chunkBuf.getShort(cursor + 2).toInt() and 0xFFFF,
                                                v2 = chunkBuf.getShort(cursor + 4).toInt() and 0xFFFF
                                            )
                                        )
                                        cursor += 6
                                    }
                                }
                            }
                        }

                        parsedEntries.add(
                            ImgEntryItem(
                                fileName = assetName,
                                sectorOffset = sectorOffset,
                                byteOffset = byteOffset,
                                byteSize = byteSize,
                                isDffModel = isDff,
                                isTxdTexture = isTxd,
                                rwSectionType = rwType,
                                rwVersion = rwVer,
                                vertexCount = vertices.size,
                                triangleCount = triangles.size,
                                vertices = vertices,
                                triangles = triangles
                            )
                        )
                    }
                }

                ImgArchiveReport(
                    isLoaded = true,
                    archivePath = imgFile.absolutePath,
                    magicSignature = magicStr,
                    totalEntries = totalEntries,
                    dffModelCount = dffCount,
                    txdTextureCount = txdCount,
                    entries = parsedEntries,
                    selectedAsset = parsedEntries.firstOrNull { it.isDffModel } ?: parsedEntries.firstOrNull()
                )
            }
        } catch (e: Exception) {
            ImgArchiveReport(
                isLoaded = false,
                archivePath = imgFile.absolutePath,
                errorMessage = "Gagal memproses arsip VER2: ${e.message ?: "Kesalahan I/O"}"
            )
        }
    }
}
