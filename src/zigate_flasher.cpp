/**
 * @file zigate_flasher.cpp
 * @brief ZiGate+ JN5189 Flash Programmer for ESP32-S3
 *
 * Adapted from NXP JennicModuleProgrammer for embedded use.
 * Uses Serial1 (already configured for ZiGate) and GPIO pins from config.h
 *
 * Copyright NXP B.V. 2012-2018. All rights reserved (original code)
 * Adaptation for ESP32-S3: 2024
 */

#include "zigate_flasher.h"
#include <LittleFS.h>
#include <stdarg.h>

// CRC32 table (polynomial 0x04c11db7, reflected)
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// Flag to indicate ZiGate communication should be suspended
// This flag should be checked in the main loop to pause normal ZiGate protocol handling
volatile bool zigateFlashInProgress = false;

// Global instance pointer
ZigateFlasher* zigateFlasher = nullptr;

// Constructor - uses Serial1 and pins from config.h
ZigateFlasher::ZigateFlasher()
    : _baudRate(115200)
    , _originalBaudRate(115200)
    , _initialized(false)
    , _connected(false)
    , _isFlashing(false)
    , _cancelled(false)
    , _gpioControl(true)  // Default to GPIO mode (RESET=GPIO40, FLASH=GPIO39)
    , _useProtocolV1(false)
{
    memset(&_chipDetails, 0, sizeof(_chipDetails));
    memset(_lastError, 0, sizeof(_lastError));
}

// Initialize
ZigateFlashStatus ZigateFlasher::init() {
    if (_initialized) {
        return ZigateFlashStatus::OK;
    }

    // GPIO pins configuration only if GPIO control is enabled
    if (_gpioControl) {
        pinMode(FLASH_ZIGATE, OUTPUT);
        pinMode(RESET_ZIGATE, OUTPUT);

        // Set default states (not in bootloader mode)
        digitalWrite(FLASH_ZIGATE, HIGH);  // FLASH/PROGRAM inactive
        digitalWrite(RESET_ZIGATE, HIGH);  // Not in reset
        log_i("ZigateFlasher initialized with GPIO control (FLASH=%d, RESET=%d)", FLASH_ZIGATE, RESET_ZIGATE);
    } else {
        log_i("ZigateFlasher initialized in MANUAL mode (no GPIO control)");
    }

    _initialized = true;
    return ZigateFlashStatus::OK;
}

// Suspend normal ZiGate communication before flash operation
void ZigateFlasher::suspendZigateCommunication() {
    zigateFlashInProgress = true;

    // Wait a bit for any pending operations to complete
    delay(100);

    // Flush any pending data
    flushSerial();

    log_i("ZiGate communication suspended for flashing");
}

// Resume normal ZiGate communication after flash operation
void ZigateFlasher::resumeZigateCommunication() {
    // Restore original baud rate if needed
    Serial1.begin(_originalBaudRate, SERIAL_8N1, RXD2, TXD2);

    zigateFlashInProgress = false;

    log_i("ZiGate communication resumed");
}

// Enter bootloader mode via GPIO
ZigateFlashStatus ZigateFlasher::enterBootloader() {
    if (!_initialized) {
        setError("Not initialized");
        return ZigateFlashStatus::ERROR_NOT_INITIALIZED;
    }

    if (!_gpioControl) {
        // Manual mode - do nothing, user must put ZiGate in bootloader manually
        log_i("Manual mode - skipping GPIO bootloader entry");
        return ZigateFlashStatus::OK;
    }

    log_i("Entering bootloader mode via GPIO...");

    // Sequence based on NXP FTDI bitbang sequence:
    // The JN5189 enters bootloader mode when FLASH/PROGRAM is held LOW during reset release
    //
    // Step 1: Drive both RESET and PROGRAM/FLASH low
    // Step 2: Release RESET (high) while keeping PROGRAM low
    // Step 3: Release PROGRAM (high) - chip should now be in bootloader
    // Step 4: Wait for bootloader to initialize

    log_i("Step 1: RESET=LOW, FLASH=LOW");
    digitalWrite(FLASH_ZIGATE, LOW);   // FLASH/PROGRAM low (active)
    digitalWrite(RESET_ZIGATE, LOW);   // RESET low (active)
    delay(50);  // Wait 50ms with both low

    log_i("Step 2: RESET=HIGH, FLASH=LOW (bootloader entry)");
    digitalWrite(RESET_ZIGATE, HIGH);  // Release RESET while FLASH still low
    delay(100);  // Wait 100ms - critical timing for bootloader detection

    log_i("Step 3: RESET=HIGH, FLASH=HIGH");
    digitalWrite(FLASH_ZIGATE, HIGH);  // Release FLASH
    delay(250);  // Wait for bootloader to fully initialize

    // Flush any garbage on serial
    flushSerial();
    delay(50);  // Additional settling time

    log_i("Bootloader mode sequence complete");
    return ZigateFlashStatus::OK;
}

// Exit bootloader and run firmware
ZigateFlashStatus ZigateFlasher::exitBootloader() {
    if (!_initialized) {
        return ZigateFlashStatus::ERROR_NOT_INITIALIZED;
    }

    Serial.println("[ZF] === Exiting bootloader mode ===");

    // Send reset command if connected (this may or may not work depending on bootloader)
    if (_connected) {
        Serial.println("[ZF] Sending software reset command to bootloader...");
        blReset();
        _connected = false;
        delay(100);  // Give time for the reset command to be processed
    }

    // IMPORTANT: Ensure FLASH pin is HIGH (inactive) before hardware reset
    // Otherwise the chip will re-enter bootloader mode on reset
    Serial.printf("[ZF] GPIO control: %s\n", _gpioControl ? "ENABLED" : "DISABLED");

    if (_gpioControl) {
        Serial.println("[ZF] Setting FLASH=HIGH (GPIO39) to prevent re-entering bootloader");
        digitalWrite(FLASH_ZIGATE, HIGH);
        delay(50);

        // Hardware reset sequence - this is the reliable way to restart the chip
        Serial.println("[ZF] Performing HARDWARE RESET:");
        Serial.println("[ZF]   1. RESET (GPIO40) = LOW");
        digitalWrite(RESET_ZIGATE, LOW);
        delay(100);  // 100ms reset pulse

        Serial.println("[ZF]   2. RESET (GPIO40) = HIGH");
        digitalWrite(RESET_ZIGATE, HIGH);

        Serial.println("[ZF]   3. Waiting 1000ms for firmware boot...");
        delay(1000);  // Wait 1 second for firmware to boot

        Serial.println("[ZF] Hardware reset complete");
    } else {
        Serial.println("[ZF] WARNING: GPIO control disabled - cannot perform hardware reset!");
        Serial.println("[ZF] Please manually reset the ZiGate (power cycle or press RESET button)");
    }

    Serial.println("[ZF] === Exit bootloader sequence finished ===");
    return ZigateFlashStatus::OK;
}

// Connect to bootloader
ZigateFlashStatus ZigateFlasher::connect() {
    if (!_initialized) {
        setError("Not initialized");
        return ZigateFlashStatus::ERROR_NOT_INITIALIZED;
    }

    // Suspend normal ZiGate communication
    suspendZigateCommunication();

    // Configure serial for bootloader
    Serial1.end();
    Serial1.begin(_baudRate, SERIAL_8N1, RXD2, TXD2);
    flushSerial();

    // Enter bootloader mode (only if GPIO control is enabled)
    if (_gpioControl) {
        ZigateFlashStatus status = enterBootloader();
        if (status != ZigateFlashStatus::OK) {
            resumeZigateCommunication();
            return status;
        }
    } else {
        // Manual mode - just wait and flush
        log_i("Manual mode: assuming ZiGate is already in bootloader");
        delay(100);
        flushSerial();
    }

    // Try to unlock ISP with multiple attempts
    ZigateFlashStatus status = ZigateFlashStatus::ERROR_UNLOCK;
    const int MAX_UNLOCK_ATTEMPTS = 3;

    for (int attempt = 1; attempt <= MAX_UNLOCK_ATTEMPTS; attempt++) {
        log_i("Unlock attempt %d/%d...", attempt, MAX_UNLOCK_ATTEMPTS);

        status = blUnlock();
        if (status == ZigateFlashStatus::OK) {
            log_i("Bootloader unlocked on attempt %d", attempt);
            break;
        }

        // Wait before retry
        delay(200);
        flushSerial();
    }

    if (status != ZigateFlashStatus::OK) {
        if (_gpioControl) {
            setError("Failed to unlock bootloader - check GPIO connections");
        } else {
            setError("Failed to unlock bootloader - make sure ZiGate is in bootloader mode (hold FLASH button while pressing RESET)");
        }
        resumeZigateCommunication();
        return status;
    }

    // Read chip ID
    status = blChipIdRead();
    if (status != ZigateFlashStatus::OK) {
        setError("Failed to read chip ID");
        resumeZigateCommunication();
        return status;
    }

    log_i("Chip ID read OK: %s (0x%08X), BL version: 0x%08X",
          _chipDetails.chipName, _chipDetails.chipId, _chipDetails.bootloaderVersion);

    // Get memory info - iterate through memory indices to find flash
    // JN5189 may have memories at different indices
    Serial.println("[ZF] Scanning for memory regions...");
    bool foundFlash = false;

    for (uint8_t idx = 0; idx < 16 && !foundFlash; idx++) {
        MemoryInfo memInfo;
        memset(&memInfo, 0, sizeof(memInfo));

        status = blMemInfo(idx, &memInfo);
        if (status == ZigateFlashStatus::OK) {
            Serial.printf("[ZF] Found memory[%d]: '%s' @ 0x%08X, size=%d, block=%d\n",
                  memInfo.index, memInfo.name, memInfo.baseAddress, memInfo.size, memInfo.blockSize);

            // Look for flash memory (name contains "Flash" or "FLASH" or it's the first writable memory)
            if (strstr(memInfo.name, "Flash") || strstr(memInfo.name, "FLASH") ||
                strstr(memInfo.name, "flash") || strstr(memInfo.name, "pFlash")) {
                _chipDetails.flashInfo = memInfo;
                foundFlash = true;
                Serial.printf("[ZF] Using '%s' as flash memory\n", memInfo.name);
            }
        } else {
            // Stop on first error (end of memory list)
            Serial.printf("[ZF] No memory at index %d (end of list or error)\n", idx);
            break;
        }
    }

    // If MEM_INFO failed, try to probe memories by trying MEM_OPEN directly
    // JN5189 bootloader may not support MEM_INFO but may support MEM_OPEN/ERASE/WRITE
    if (!foundFlash && (_chipDetails.chipId >> 24) == 0x88) {
        Serial.println("[ZF] MEM_INFO not supported - probing memory indices with MEM_OPEN...");

        // Try opening memory indices 0-4 to see which ones work
        for (uint8_t idx = 0; idx < 5; idx++) {
            Serial.printf("[ZF] Trying MEM_OPEN for index %d (write mode)...\n", idx);
            status = blMemOpen(idx, 0x02);  // 0x02 = write access
            if (status == ZigateFlashStatus::OK) {
                Serial.printf("[ZF] Memory index %d is accessible for write!\n", idx);
                // Use this memory index
                _chipDetails.flashInfo.index = idx;
                _chipDetails.flashInfo.baseAddress = 0x00000000;
                _chipDetails.flashInfo.size = 632 * 1024;  // 632KB flash for JN5189
                _chipDetails.flashInfo.blockSize = 512;
                _chipDetails.flashInfo.access = 0x07;  // Read/Write/Erase
                snprintf(_chipDetails.flashInfo.name, sizeof(_chipDetails.flashInfo.name), "FLASH[%d]", idx);
                foundFlash = true;

                // Close it for now
                blMemClose(idx);
                break;
            } else {
                Serial.printf("[ZF] MEM_OPEN for index %d failed\n", idx);
            }
        }

        // If MEM_OPEN also doesn't work, use defaults
        if (!foundFlash) {
            Serial.println("[ZF] MEM_OPEN also failed - using default values");
            _chipDetails.flashInfo.index = 0;
            _chipDetails.flashInfo.baseAddress = 0x00000000;
            _chipDetails.flashInfo.size = 632 * 1024;
            _chipDetails.flashInfo.blockSize = 512;
            _chipDetails.flashInfo.access = 0x07;
            strlcpy(_chipDetails.flashInfo.name, "FLASH", sizeof(_chipDetails.flashInfo.name));
            foundFlash = true;
        }
    }

    if (!foundFlash) {
        setError("Failed to find flash memory");
        resumeZigateCommunication();
        return ZigateFlashStatus::ERROR_COMMS;
    }

    _connected = true;
    Serial.println("[ZF] ========== CONNECTION SUMMARY ==========");
    Serial.printf("[ZF] Chip ID: 0x%08X (%s)\n", _chipDetails.chipId, _chipDetails.chipName);
    Serial.printf("[ZF] Bootloader: 0x%08X\n", _chipDetails.bootloaderVersion);
    Serial.printf("[ZF] Flash memory index: %d\n", _chipDetails.flashInfo.index);
    Serial.printf("[ZF] Flash base address: 0x%08X\n", _chipDetails.flashInfo.baseAddress);
    Serial.printf("[ZF] Flash size: %d bytes (%dK)\n", _chipDetails.flashInfo.size, _chipDetails.flashInfo.size / 1024);
    Serial.printf("[ZF] Flash block size: %d bytes\n", _chipDetails.flashInfo.blockSize);
    Serial.printf("[ZF] Protocol: %s\n", _useProtocolV1 ? "v1" : "v2");
    Serial.println("[ZF] ==========================================");

    return ZigateFlashStatus::OK;
}

// Flash firmware from buffer
ZigateFlashStatus ZigateFlasher::flash(const uint8_t* data, size_t length, FlashProgressCallback callback, bool skipErase) {
    if (!_connected) {
        setError("Not connected");
        return ZigateFlashStatus::ERROR_NOT_INITIALIZED;
    }

    if (_isFlashing) {
        setError("Flash operation already in progress");
        return ZigateFlashStatus::ERROR_ALREADY_RUNNING;
    }

    if (!data || length == 0) {
        setError("Invalid firmware data");
        return ZigateFlashStatus::ERROR_FILE;
    }

    _isFlashing = true;
    _cancelled = false;

    ZigateFlashStatus status;

    // Use Protocol v1 or v2 based on detection during connect
    if (_useProtocolV1) {
        // If skipErase is requested, use the no-erase version directly
        if (skipErase) {
            return flashV1NoErase(data, length, callback);
        }
        return flashV1(data, length, callback);
    }

    uint8_t memIndex = _chipDetails.flashInfo.index;

    if (callback) callback(0, "Opening flash memory...");

    // Open memory for write AND erase (0x02 = write, 0x04 = erase, 0x06 = both)
    Serial.printf("[ZF] Opening memory index %d with access mode 0x06 (write+erase)...\n", memIndex);
    status = blMemOpen(memIndex, 0x06);  // 0x06 = write + erase access
    if (status != ZigateFlashStatus::OK) {
        Serial.println("[ZF] Open with 0x06 failed, trying 0x02 (write only)...");
        status = blMemOpen(memIndex, 0x02);  // Fallback to write only
        if (status != ZigateFlashStatus::OK) {
            setError("Failed to open flash memory");
            _isFlashing = false;
            return status;
        }
    }

    // Erase flash (unless skipErase is requested)
    if (!skipErase) {
        if (callback) callback(5, "Erasing flash...");
        Serial.printf("[ZF] Erasing flash: %d bytes at 0x%08X\n", length, _chipDetails.flashInfo.baseAddress);

        // Try full erase first
        status = blMemErase(memIndex, _chipDetails.flashInfo.baseAddress, length);
        if (status != ZigateFlashStatus::OK) {
            Serial.println("[ZF] Full erase failed (0xF3) - trying sector-by-sector erase...");

            // Try erasing sector by sector (32KB sectors for JN5189)
            const uint32_t SECTOR_SIZE = 32 * 1024;  // 32KB
            uint32_t numSectors = (length + SECTOR_SIZE - 1) / SECTOR_SIZE;
            uint32_t eraseAddr = _chipDetails.flashInfo.baseAddress;
            bool eraseOk = true;

            for (uint32_t s = 0; s < numSectors && eraseOk; s++) {
                uint32_t eraseLen = SECTOR_SIZE;
                if (eraseAddr + eraseLen > _chipDetails.flashInfo.baseAddress + length) {
                    eraseLen = _chipDetails.flashInfo.baseAddress + length - eraseAddr;
                }

                Serial.printf("[ZF] Erasing sector %d/%d at 0x%08X (%d bytes)...\n",
                              s + 1, numSectors, eraseAddr, eraseLen);

                status = blMemErase(memIndex, eraseAddr, eraseLen);
                if (status != ZigateFlashStatus::OK) {
                    Serial.printf("[ZF] Sector %d erase failed\n", s + 1);
                    eraseOk = false;
                }
                eraseAddr += SECTOR_SIZE;

                if (callback) {
                    int progress = 5 + (s * 5 / numSectors);
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Erasing sector %d/%d...", s + 1, numSectors);
                    callback(progress, msg);
                }
                yield();
            }

            if (!eraseOk) {
                Serial.println("[ZF] Sector erase also failed - trying to write anyway");
                Serial.println("[ZF] WARNING: Flash may not be properly erased, firmware may be corrupted!");
            }
        } else {
            Serial.println("[ZF] Full erase OK");
        }
    } else {
        Serial.println("[ZF] Skipping erase as requested");
    }

    // Write data in chunks
    uint32_t address = _chipDetails.flashInfo.baseAddress;
    size_t remaining = length;
    size_t offset = 0;
    int lastProgress = 5;

    log_i("Writing %d bytes to flash...", length);

    while (remaining > 0 && !_cancelled) {
        size_t chunkSize = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

        status = blMemWrite(memIndex, address, data + offset, chunkSize);
        if (status != ZigateFlashStatus::OK) {
            setError("Write failed at 0x%08X", address);
            blMemClose(memIndex);
            _isFlashing = false;
            return status;
        }

        offset += chunkSize;
        address += chunkSize;
        remaining -= chunkSize;

        // Update progress (5-95%)
        int progress = 5 + (offset * 90 / length);
        if (progress != lastProgress) {
            lastProgress = progress;
            if (callback) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Writing... %d%%", progress);
                callback(progress, msg);
            }
        }

        // Yield to avoid watchdog
        yield();
    }

    if (_cancelled) {
        setError("Operation cancelled");
        blMemClose(memIndex);
        _isFlashing = false;
        return ZigateFlashStatus::ERROR_COMMS;
    }

    // Close memory
    if (callback) callback(95, "Closing flash memory...");
    blMemClose(memIndex);

    // Reset device
    if (callback) callback(98, "Resetting device...");
    exitBootloader();

    // Resume normal communication
    resumeZigateCommunication();

    _isFlashing = false;
    if (callback) callback(100, "Flash complete!");
    log_i("Flash complete: %d bytes written", length);

    return ZigateFlashStatus::OK;
}

// Flash firmware from LittleFS file
ZigateFlashStatus ZigateFlasher::flashFromFile(const char* filename, FlashProgressCallback callback, bool skipErase) {
    if (!LittleFS.exists(filename)) {
        setError("File not found: %s", filename);
        return ZigateFlashStatus::ERROR_FILE;
    }

    File file = LittleFS.open(filename, "r");
    if (!file) {
        setError("Failed to open file: %s", filename);
        return ZigateFlashStatus::ERROR_FILE;
    }

    size_t fileSize = file.size();
    if (fileSize == 0 || fileSize > 640 * 1024) {  // Max 640KB
        file.close();
        setError("Invalid file size: %d", fileSize);
        return ZigateFlashStatus::ERROR_FILE;
    }

    // Allocate buffer in PSRAM if available
    uint8_t* buffer = (uint8_t*)ps_malloc(fileSize);
    if (!buffer) {
        // Fallback to regular heap
        buffer = (uint8_t*)malloc(fileSize);
    }
    if (!buffer) {
        file.close();
        setError("Out of memory");
        return ZigateFlashStatus::ERROR_MEMORY;
    }

    // Read file
    size_t bytesRead = file.read(buffer, fileSize);
    file.close();

    if (bytesRead != fileSize) {
        free(buffer);
        setError("Failed to read file");
        return ZigateFlashStatus::ERROR_FILE;
    }

    // Flash
    ZigateFlashStatus status = flash(buffer, fileSize, callback, skipErase);
    free(buffer);

    return status;
}

// ============================================================================
// Low-level protocol implementation
// ============================================================================

// Calculate CRC32
uint32_t ZigateFlasher::calcCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        uint8_t idx = (crc ^ data[i]) & 0xFF;
        crc = (crc32_table[idx] ^ (crc >> 8)) & 0xFFFFFFFF;
    }
    return crc ^ 0xFFFFFFFF;
}

// Send message to bootloader
ZigateFlashStatus ZigateFlasher::sendMessage(BLMessageType type, const uint8_t* header, uint8_t headerLen,
                                              const uint8_t* data, uint16_t dataLen) {
    uint8_t buffer[MAX_MSG_LENGTH];
    uint8_t* ptr = buffer;

    // Calculate total length: header + data + CRC(4)
    uint16_t msgLen = headerLen + dataLen + 8;  // +8 for flags, length, type, crc

    // Flags (no signing, no hash)
    *ptr++ = 0x00;

    // Length (big endian)
    *ptr++ = (msgLen >> 8) & 0xFF;
    *ptr++ = msgLen & 0xFF;

    // Message type
    *ptr++ = static_cast<uint8_t>(type);

    // Header
    if (header && headerLen > 0) {
        memcpy(ptr, header, headerLen);
        ptr += headerLen;
    }

    // Data
    if (data && dataLen > 0) {
        memcpy(ptr, data, dataLen);
        ptr += dataLen;
    }

    // Calculate CRC over flags + length + type + header + data
    size_t crcLen = ptr - buffer;
    uint32_t crc = calcCRC32(buffer, crcLen);

    // Append CRC (big endian)
    *ptr++ = (crc >> 24) & 0xFF;
    *ptr++ = (crc >> 16) & 0xFF;
    *ptr++ = (crc >> 8) & 0xFF;
    *ptr++ = crc & 0xFF;

    // Debug: dump message being sent
    size_t totalLen = ptr - buffer;
    Serial.printf("[ZF] Tx msg (%d bytes): ", totalLen);
    for (size_t i = 0; i < (totalLen < 20 ? totalLen : 20); i++) {
        Serial.printf("%02X ", buffer[i]);
    }
    Serial.println();

    // Send via Serial1
    size_t written = Serial1.write(buffer, totalLen);

    if (written != totalLen) {
        setError("Failed to send message");
        return ZigateFlashStatus::ERROR_COMMS;
    }

    return ZigateFlashStatus::OK;
}

// Read message from bootloader
// JN5189 Protocol v2 format:
// Full message: [flags:1][length:2][type:1][response:1][data...][crc:4]
// 'length' is the TOTAL message size including the 3-byte header
// CRC is calculated over all bytes EXCEPT the last 4 (CRC itself)
BLResponse ZigateFlasher::readMessage(BLMessageType* rxType, uint8_t* rxData, uint16_t* rxLen, uint32_t timeoutMs) {
    uint8_t buffer[MAX_MSG_LENGTH];
    unsigned long startTime = millis();

    // Wait for first 3 bytes (flags + length header)
    while (Serial1.available() < 3) {
        if (millis() - startTime > timeoutMs) {
            Serial.println("[ZF] Timeout waiting for response header");
            return BLResponse::NO_RESPONSE;
        }
        delay(1);
    }

    // Read flags + length header into buffer (needed for CRC calculation)
    buffer[0] = Serial1.read();  // flags
    buffer[1] = Serial1.read();  // length MSB
    buffer[2] = Serial1.read();  // length LSB

    uint16_t totalLen = (buffer[1] << 8) | buffer[2];  // Total message length

    Serial.printf("[ZF] Rx header: flags=0x%02X, totalLen=%d\n", buffer[0], totalLen);

    // Minimum message is 9 bytes: header(3) + type(1) + response(1) + crc(4)
    if (totalLen < 9 || totalLen >= MAX_MSG_LENGTH) {
        Serial.printf("[ZF] ERROR: Invalid message length: %d\n", totalLen);
        return BLResponse::INVALID_RESPONSE;
    }

    // Read the rest of the message (totalLen - 3 bytes already read)
    int remaining = totalLen - 3;
    int idx = 3;  // Start writing after header

    while (remaining > 0) {
        if (millis() - startTime > timeoutMs) {
            Serial.printf("[ZF] ERROR: Timeout waiting for body, got %d/%d\n", idx - 3, totalLen - 3);
            return BLResponse::NO_RESPONSE;
        }

        if (Serial1.available()) {
            buffer[idx] = Serial1.read();
            idx++;
            remaining--;
        } else {
            delay(1);
        }
    }

    // Debug: dump received message
    Serial.printf("[ZF] Rx msg (%d bytes): ", totalLen);
    for (int i = 0; i < (totalLen < 20 ? totalLen : 20); i++) {
        Serial.printf("%02X ", buffer[i]);
    }
    Serial.println();

    // Verify CRC - CRC is calculated over all bytes except the last 4 (CRC itself)
    uint32_t rxCrc = ((uint32_t)buffer[totalLen - 4] << 24) |
                     ((uint32_t)buffer[totalLen - 3] << 16) |
                     ((uint32_t)buffer[totalLen - 2] << 8) |
                     ((uint32_t)buffer[totalLen - 1]);

    uint32_t calcCrc = calcCRC32(buffer, totalLen - 4);

    if (rxCrc != calcCrc) {
        Serial.printf("[ZF] ERROR: CRC mismatch: rx=0x%08X, calc=0x%08X\n", rxCrc, calcCrc);
        return BLResponse::CRC_ERROR;
    }

    // Extract type and response from message
    // Layout: [0]=flags, [1-2]=length, [3]=type, [4]=response, [5..totalLen-5]=data, [totalLen-4..totalLen-1]=CRC
    *rxType = static_cast<BLMessageType>(buffer[3]);
    BLResponse response = static_cast<BLResponse>(buffer[4]);

    Serial.printf("[ZF] Rx type=0x%02X, response=0x%02X\n", (int)*rxType, (int)response);

    // Copy data (excluding header, type, response, crc)
    // Data starts at offset 5 and ends 4 bytes before the end
    if (rxLen && rxData) {
        *rxLen = totalLen - 9;  // Total - header(3) - type(1) - response(1) - crc(4)
        if (*rxLen > 0) {
            memcpy(rxData, &buffer[5], *rxLen);
        }
    }

    return response;
}

// Request/Response wrapper
BLResponse ZigateFlasher::request(BLMessageType txType, const uint8_t* header, uint8_t headerLen,
                                   const uint8_t* txData, uint16_t txLen,
                                   BLMessageType* rxType, uint8_t* rxData, uint16_t* rxLen,
                                   uint32_t timeoutMs) {
    // Small delay between commands to let bootloader settle
    delay(10);

    // Flush any residual data from previous commands
    while (Serial1.available()) {
        uint8_t garbage = Serial1.read();
        Serial.printf("[ZF] Flushing residual byte: 0x%02X\n", garbage);
    }

    ZigateFlashStatus status = sendMessage(txType, header, headerLen, txData, txLen);
    if (status != ZigateFlashStatus::OK) {
        return BLResponse::ERROR;
    }

    return readMessage(rxType, rxData, rxLen, timeoutMs);
}

// ============================================================================
// Bootloader commands
// ============================================================================

ZigateFlashStatus ZigateFlasher::blUnlock() {
    Serial.println("[ZF] Sending UNLOCK_ISP_REQUEST...");

    // Try unlock with mode=1 and default NXP key first
    // Default unlock key: 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 (repeated)
    static const uint8_t unlockKey[16] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };

    BLMessageType rxType;
    uint16_t rxLen = 0;
    BLResponse response;

    // First try mode 1 with key (secure mode)
    Serial.println("[ZF] Trying unlock mode=1 with default key...");
    uint8_t header[1] = { 0x01 };  // Mode 1 = with key
    response = request(BLMessageType::UNLOCK_ISP_REQUEST, header, 1,
                       unlockKey, sizeof(unlockKey), &rxType, nullptr, &rxLen);

    if (response == BLResponse::OK && rxType == BLMessageType::UNLOCK_ISP_RESPONSE) {
        Serial.println("[ZF] Bootloader unlocked OK (mode 1 with key)");
        return ZigateFlashStatus::OK;
    }

    Serial.printf("[ZF] Mode 1 unlock failed (response=0x%02X), trying mode 0...\n", (int)response);

    // Fallback to mode 0 (no key)
    header[0] = 0x00;
    response = request(BLMessageType::UNLOCK_ISP_REQUEST, header, 1,
                       nullptr, 0, &rxType, nullptr, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::UNLOCK_ISP_RESPONSE) {
        Serial.printf("[ZF] Unlock failed: response=0x%02X, rxType=0x%02X\n", (int)response, (int)rxType);
        return ZigateFlashStatus::ERROR_UNLOCK;
    }

    Serial.println("[ZF] Bootloader unlocked OK (mode 0)");
    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blChipIdRead() {
    Serial.println("[ZF] Sending GET_CHIPID_REQUEST...");
    BLMessageType rxType;
    uint8_t rxData[16];
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::GET_CHIPID_REQUEST, nullptr, 0,
                                   nullptr, 0, &rxType, rxData, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::GET_CHIPID_RESPONSE) {
        Serial.printf("[ZF] ChipId read failed: response=0x%02X\n", (int)response);
        return ZigateFlashStatus::ERROR_COMMS;
    }

    Serial.printf("[ZF] ChipId rxLen=%d\n", rxLen);

    if (rxLen >= 4) {
        _chipDetails.chipId = ((uint32_t)rxData[0] << 24) |
                              ((uint32_t)rxData[1] << 16) |
                              ((uint32_t)rxData[2] << 8) |
                              ((uint32_t)rxData[3]);
    }

    if (rxLen >= 8) {
        _chipDetails.bootloaderVersion = ((uint32_t)rxData[4] << 24) |
                                         ((uint32_t)rxData[5] << 16) |
                                         ((uint32_t)rxData[6] << 8) |
                                         ((uint32_t)rxData[7]);
    }

    // Set chip name based on ID
    switch (_chipDetails.chipId >> 24) {
        case 0x88:
            strlcpy(_chipDetails.chipName, "JN5189", sizeof(_chipDetails.chipName));
            break;
        case 0x89:
            strlcpy(_chipDetails.chipName, "JN5188", sizeof(_chipDetails.chipName));
            break;
        default:
            snprintf(_chipDetails.chipName, sizeof(_chipDetails.chipName),
                     "Unknown (0x%08X)", _chipDetails.chipId);
    }

    Serial.printf("[ZF] Chip: %s (0x%08X), BL: 0x%08X\n", _chipDetails.chipName,
          _chipDetails.chipId, _chipDetails.bootloaderVersion);

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blMemInfo(uint8_t index, MemoryInfo* info) {
    uint8_t header[1] = { index };
    BLMessageType rxType;
    uint8_t rxData[64];
    uint16_t rxLen = 0;

    Serial.printf("[ZF] Sending MEM_GET_INFO_REQUEST for index %d...\n", index);

    BLResponse response = request(BLMessageType::MEM_GET_INFO_REQUEST, header, 1,
                                   nullptr, 0, &rxType, rxData, &rxLen, 2000);  // 2s timeout

    Serial.printf("[ZF] MemInfo response=0x%02X, rxType=0x%02X, rxLen=%d\n",
          (int)response, (int)rxType, rxLen);

    // E_BL_RESPONSE_NOT_SUPPORTED (0xFF) means end of memory list, not an error for index > 0
    if (response == BLResponse::NOT_SUPPORTED && index > 0) {
        Serial.printf("[ZF] No more memories at index %d\n", index);
        return ZigateFlashStatus::ERROR_COMMS;  // Signal end of list
    }

    if (response != BLResponse::OK || rxType != BLMessageType::MEM_GET_INFO_RESPONSE) {
        Serial.printf("[ZF] MemInfo failed: response=0x%02X, rxType=0x%02X\n", (int)response, (int)rxType);
        return ZigateFlashStatus::ERROR_COMMS;
    }

    if (rxLen >= 15) {
        info->index = rxData[0];
        // Little-endian byte order
        info->baseAddress = (rxData[1]) | (rxData[2] << 8) | (rxData[3] << 16) | (rxData[4] << 24);
        info->size = (rxData[5]) | (rxData[6] << 8) | (rxData[7] << 16) | (rxData[8] << 24);
        info->blockSize = (rxData[9]) | (rxData[10] << 8) | (rxData[11] << 16) | (rxData[12] << 24);
        info->access = rxData[14];

        // Copy name if present
        if (rxLen > 15) {
            size_t nameLen = rxLen - 15;
            if (nameLen > sizeof(info->name) - 1) nameLen = sizeof(info->name) - 1;
            memcpy(info->name, &rxData[15], nameLen);
            info->name[nameLen] = '\0';
        } else {
            info->name[0] = '\0';
        }

        log_i("Memory[%d]: '%s' Base=0x%08X Size=%d Block=%d Access=0x%02X",
              info->index, info->name, info->baseAddress, info->size, info->blockSize, info->access);
    } else {
        log_e("MemInfo response too short: %d bytes", rxLen);
        return ZigateFlashStatus::ERROR_COMMS;
    }

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blMemOpen(uint8_t index, uint8_t accessMode) {
    uint8_t header[2] = { index, accessMode };
    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::MEM_OPEN_REQUEST, header, 2,
                                   nullptr, 0, &rxType, nullptr, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::MEM_OPEN_RESPONSE) {
        log_e("MemOpen failed: response=0x%02X", (int)response);
        return ZigateFlashStatus::ERROR_COMMS;
    }

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blMemErase(uint8_t index, uint32_t address, uint32_t length) {
    uint8_t header[10];
    header[0] = index;
    header[1] = 0;  // Mode
    header[2] = (address >> 0) & 0xFF;
    header[3] = (address >> 8) & 0xFF;
    header[4] = (address >> 16) & 0xFF;
    header[5] = (address >> 24) & 0xFF;
    header[6] = (length >> 0) & 0xFF;
    header[7] = (length >> 8) & 0xFF;
    header[8] = (length >> 16) & 0xFF;
    header[9] = (length >> 24) & 0xFF;

    BLMessageType rxType;
    uint16_t rxLen = 0;

    // Erase can take a while
    BLResponse response = request(BLMessageType::MEM_ERASE_REQUEST, header, 10,
                                   nullptr, 0, &rxType, nullptr, &rxLen, 10000);

    if (response != BLResponse::OK || rxType != BLMessageType::MEM_ERASE_RESPONSE) {
        log_e("MemErase failed: response=0x%02X", (int)response);
        return ZigateFlashStatus::ERROR_ERASE;
    }

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blMemWrite(uint8_t index, uint32_t address, const uint8_t* data, uint32_t length) {
    uint8_t header[10];
    header[0] = index;
    header[1] = 0;  // Mode
    header[2] = (address >> 0) & 0xFF;
    header[3] = (address >> 8) & 0xFF;
    header[4] = (address >> 16) & 0xFF;
    header[5] = (address >> 24) & 0xFF;
    header[6] = (length >> 0) & 0xFF;
    header[7] = (length >> 8) & 0xFF;
    header[8] = (length >> 16) & 0xFF;
    header[9] = (length >> 24) & 0xFF;

    // Log first write to verify start address
    static bool firstWrite = true;
    if (firstWrite) {
        Serial.printf("[ZF] FIRST WRITE: index=%d, addr=0x%08X, len=%d\n", index, address, length);
        Serial.printf("[ZF] First 16 bytes of data: ");
        for (int i = 0; i < 16 && i < (int)length; i++) {
            Serial.printf("%02X ", data[i]);
        }
        Serial.println();
        firstWrite = false;
    }

    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::MEM_WRITE_REQUEST, header, 10,
                                   data, length, &rxType, nullptr, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::MEM_WRITE_RESPONSE) {
        log_e("MemWrite failed at 0x%08X: response=0x%02X", address, (int)response);
        return ZigateFlashStatus::ERROR_WRITE;
    }

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blMemClose(uint8_t index) {
    uint8_t header[1] = { index };
    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::MEM_CLOSE_REQUEST, header, 1,
                                   nullptr, 0, &rxType, nullptr, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::MEM_CLOSE_RESPONSE) {
        log_e("MemClose failed: response=0x%02X", (int)response);
        return ZigateFlashStatus::ERROR_COMMS;
    }

    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blReset() {
    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::RESET_REQUEST, nullptr, 0,
                                   nullptr, 0, &rxType, nullptr, &rxLen);

    // Reset may not get a response if device resets immediately
    if (response == BLResponse::NO_RESPONSE) {
        return ZigateFlashStatus::OK;
    }

    return (response == BLResponse::OK) ? ZigateFlashStatus::OK : ZigateFlashStatus::ERROR_COMMS;
}

// ============================================================================
// Protocol v1 commands (legacy fallback)
// ============================================================================

ZigateFlashStatus ZigateFlasher::blFlashEraseV1() {
    Serial.println("[ZF] Sending FLASH_ERASE_REQUEST (v1)...");
    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::FLASH_ERASE_REQUEST_V1, nullptr, 0,
                                   nullptr, 0, &rxType, nullptr, &rxLen, 10000);  // 10s timeout for erase

    if (response != BLResponse::OK || rxType != BLMessageType::FLASH_ERASE_RESPONSE_V1) {
        Serial.printf("[ZF] FlashErase v1 failed: response=0x%02X, rxType=0x%02X\n", (int)response, (int)rxType);
        return ZigateFlashStatus::ERROR_ERASE;
    }

    Serial.println("[ZF] Flash erased OK (v1)");
    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blFlashSectorEraseV1(uint8_t sectorNum) {
    Serial.printf("[ZF] Sending FLASH_SECTOR_ERASE_REQUEST (v1) for sector %d...\n", sectorNum);
    uint8_t header[1] = { sectorNum };
    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::FLASH_SECTOR_ERASE_REQUEST_V1, header, 1,
                                   nullptr, 0, &rxType, nullptr, &rxLen, 2000);  // 2s timeout per sector

    if (response != BLResponse::OK || rxType != BLMessageType::FLASH_SECTOR_ERASE_RESPONSE_V1) {
        Serial.printf("[ZF] FlashSectorErase v1 failed: sector=%d, response=0x%02X, rxType=0x%02X\n",
                     sectorNum, (int)response, (int)rxType);
        return ZigateFlashStatus::ERROR_ERASE;
    }

    Serial.printf("[ZF] Sector %d erased OK (v1)\n", sectorNum);
    return ZigateFlashStatus::OK;
}

ZigateFlashStatus ZigateFlasher::blFlashWriteV1(uint32_t address, const uint8_t* data, uint8_t length) {
    // Protocol v1 FLASH_PROGRAM: [addr:4][data:length]
    uint8_t header[4];
    header[0] = (address >> 0) & 0xFF;
    header[1] = (address >> 8) & 0xFF;
    header[2] = (address >> 16) & 0xFF;
    header[3] = (address >> 24) & 0xFF;

    BLMessageType rxType;
    uint16_t rxLen = 0;

    BLResponse response = request(BLMessageType::FLASH_PROGRAM_REQUEST_V1, header, 4,
                                   data, length, &rxType, nullptr, &rxLen);

    if (response != BLResponse::OK || rxType != BLMessageType::FLASH_PROGRAM_RESPONSE_V1) {
        Serial.printf("[ZF] FlashWrite v1 failed at 0x%08X: response=0x%02X\n", address, (int)response);
        return ZigateFlashStatus::ERROR_WRITE;
    }

    return ZigateFlashStatus::OK;
}

// Flash using Protocol v1 (legacy) - without erase (write only)
ZigateFlashStatus ZigateFlasher::flashV1NoErase(const uint8_t* data, size_t length, FlashProgressCallback callback) {
    Serial.println("[ZF] Flashing using Protocol v1 - NO ERASE (direct write)");

    // Write data in small chunks (v1 max is ~252 bytes)
    const uint8_t V1_CHUNK_SIZE = 128;  // Safe chunk size for protocol v1
    uint32_t address = _chipDetails.flashInfo.baseAddress;
    size_t remaining = length;
    size_t offset = 0;
    int lastProgress = 0;

    Serial.printf("[ZF] Writing %d bytes to flash (v1, no erase)...\n", length);

    while (remaining > 0 && !_cancelled) {
        uint8_t chunkSize = (remaining > V1_CHUNK_SIZE) ? V1_CHUNK_SIZE : remaining;

        ZigateFlashStatus status = blFlashWriteV1(address, data + offset, chunkSize);
        if (status != ZigateFlashStatus::OK) {
            setError("Write failed at 0x%08X (v1)", address);
            _isFlashing = false;
            return status;
        }

        offset += chunkSize;
        address += chunkSize;
        remaining -= chunkSize;

        // Update progress (0-95%)
        int progress = (offset * 95 / length);
        if (progress != lastProgress) {
            lastProgress = progress;
            if (callback) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Writing (no erase)... %d%%", progress);
                callback(progress, msg);
            }
        }

        // Yield to avoid watchdog
        yield();
    }

    if (_cancelled) {
        setError("Operation cancelled");
        _isFlashing = false;
        return ZigateFlashStatus::ERROR_COMMS;
    }

    // Reset device
    if (callback) callback(98, "Resetting device...");
    exitBootloader();

    // Resume normal communication
    resumeZigateCommunication();

    _isFlashing = false;
    if (callback) callback(100, "Flash complete!");
    Serial.printf("[ZF] Flash complete (v1, no erase): %d bytes written\n", length);

    return ZigateFlashStatus::OK;
}

// Flash using Protocol v1 (legacy) - with sector erase
ZigateFlashStatus ZigateFlasher::flashV1(const uint8_t* data, size_t length, FlashProgressCallback callback) {
    Serial.println("[ZF] Flashing using Protocol v1 (legacy mode)");
    ZigateFlashStatus status;

    // Try sector-by-sector erase instead of full erase
    // JN5189 has 512-byte pages, 32KB sectors
    const uint32_t SECTOR_SIZE = 32 * 1024;  // 32KB per sector
    uint32_t numSectors = (length + SECTOR_SIZE - 1) / SECTOR_SIZE;

    if (callback) callback(2, "Erasing sectors...");
    Serial.printf("[ZF] Erasing %d sectors for %d bytes firmware...\n", numSectors, length);

    for (uint32_t sector = 0; sector < numSectors && !_cancelled; sector++) {
        status = blFlashSectorEraseV1(sector);
        if (status != ZigateFlashStatus::OK) {
            Serial.printf("[ZF] Sector erase failed at sector %d, trying direct write...\n", sector);
            // If sector erase fails, try writing directly (no erase)
            return flashV1NoErase(data, length, callback);
        }
        int progress = 2 + (sector * 3 / numSectors);
        if (callback) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Erasing sector %d/%d...", sector + 1, numSectors);
            callback(progress, msg);
        }
        yield();
    }

    if (_cancelled) {
        setError("Operation cancelled");
        _isFlashing = false;
        return ZigateFlashStatus::ERROR_COMMS;
    }

    // Write data in small chunks (v1 max is ~252 bytes)
    const uint8_t V1_CHUNK_SIZE = 128;  // Safe chunk size for protocol v1
    uint32_t address = _chipDetails.flashInfo.baseAddress;
    size_t remaining = length;
    size_t offset = 0;
    int lastProgress = 5;

    Serial.printf("[ZF] Writing %d bytes to flash (v1)...\n", length);

    while (remaining > 0 && !_cancelled) {
        uint8_t chunkSize = (remaining > V1_CHUNK_SIZE) ? V1_CHUNK_SIZE : remaining;

        status = blFlashWriteV1(address, data + offset, chunkSize);
        if (status != ZigateFlashStatus::OK) {
            setError("Write failed at 0x%08X (v1)", address);
            _isFlashing = false;
            return status;
        }

        offset += chunkSize;
        address += chunkSize;
        remaining -= chunkSize;

        // Update progress (5-95%)
        int progress = 5 + (offset * 90 / length);
        if (progress != lastProgress) {
            lastProgress = progress;
            if (callback) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Writing (v1)... %d%%", progress);
                callback(progress, msg);
            }
        }

        // Yield to avoid watchdog
        yield();
    }

    if (_cancelled) {
        setError("Operation cancelled");
        _isFlashing = false;
        return ZigateFlashStatus::ERROR_COMMS;
    }

    // Reset device
    if (callback) callback(98, "Resetting device...");
    exitBootloader();

    // Resume normal communication
    resumeZigateCommunication();

    _isFlashing = false;
    if (callback) callback(100, "Flash complete!");
    Serial.printf("[ZF] Flash complete (v1): %d bytes written\n", length);

    return ZigateFlashStatus::OK;
}

// ============================================================================
// Helper functions
// ============================================================================

void ZigateFlasher::setError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(_lastError, sizeof(_lastError), format, args);
    va_end(args);
    log_e("%s", _lastError);
}

void ZigateFlasher::flushSerial() {
    while (Serial1.available()) {
        Serial1.read();
    }
}

const char* ZigateFlasher::statusToString(ZigateFlashStatus status) {
    switch (status) {
        case ZigateFlashStatus::OK: return "OK";
        case ZigateFlashStatus::ERROR_TIMEOUT: return "Timeout";
        case ZigateFlashStatus::ERROR_CRC: return "CRC Error";
        case ZigateFlashStatus::ERROR_COMMS: return "Communication Error";
        case ZigateFlashStatus::ERROR_UNLOCK: return "Unlock Failed";
        case ZigateFlashStatus::ERROR_ERASE: return "Erase Failed";
        case ZigateFlashStatus::ERROR_WRITE: return "Write Failed";
        case ZigateFlashStatus::ERROR_VERIFY: return "Verify Failed";
        case ZigateFlashStatus::ERROR_NO_RESPONSE: return "No Response";
        case ZigateFlashStatus::ERROR_INVALID_CHIP: return "Invalid Chip";
        case ZigateFlashStatus::ERROR_MEMORY: return "Memory Error";
        case ZigateFlashStatus::ERROR_FILE: return "File Error";
        case ZigateFlashStatus::ERROR_NOT_INITIALIZED: return "Not Initialized";
        case ZigateFlashStatus::ERROR_ALREADY_RUNNING: return "Already Running";
        default: return "Unknown Error";
    }
}
