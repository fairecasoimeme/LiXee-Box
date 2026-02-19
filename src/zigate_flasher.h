/**
 * @file zigate_flasher.h
 * @brief ZiGate+ JN5189 Flash Programmer for ESP32-S3
 *
 * Adapted from NXP JennicModuleProgrammer for embedded use.
 * Allows flashing ZiGate+ firmware via web interface.
 *
 * Uses the existing Serial1 connection to ZiGate (RXD2/TXD2)
 * and GPIO pins RESET_ZIGATE/FLASH_ZIGATE defined in config.h
 *
 * Copyright NXP B.V. 2012-2018. All rights reserved (original code)
 * Adaptation for ESP32-S3: 2024
 */

#ifndef ZIGATE_FLASHER_H
#define ZIGATE_FLASHER_H

#include <Arduino.h>
#include <functional>
#include "config.h"  // Pour RESET_ZIGATE, FLASH_ZIGATE, RXD2, TXD2

/**
 * @brief Status codes for flash operations
 */
enum class ZigateFlashStatus {
    OK = 0,
    ERROR_TIMEOUT,
    ERROR_CRC,
    ERROR_COMMS,
    ERROR_UNLOCK,
    ERROR_ERASE,
    ERROR_WRITE,
    ERROR_VERIFY,
    ERROR_NO_RESPONSE,
    ERROR_INVALID_CHIP,
    ERROR_MEMORY,
    ERROR_FILE,
    ERROR_NOT_INITIALIZED,
    ERROR_ALREADY_RUNNING
};

/**
 * @brief Bootloader message types (protocol v1 and v2)
 */
enum class BLMessageType : uint8_t {
    // Protocol v1 commands (legacy - for older bootloaders)
    FLASH_ERASE_REQUEST_V1          = 0x07,
    FLASH_ERASE_RESPONSE_V1         = 0x08,
    FLASH_PROGRAM_REQUEST_V1        = 0x09,
    FLASH_PROGRAM_RESPONSE_V1       = 0x0A,
    FLASH_READ_REQUEST_V1           = 0x0B,
    FLASH_READ_RESPONSE_V1          = 0x0C,
    FLASH_SECTOR_ERASE_REQUEST_V1   = 0x0D,
    FLASH_SECTOR_ERASE_RESPONSE_V1  = 0x0E,

    // Common commands
    RESET_REQUEST           = 0x14,
    RESET_RESPONSE          = 0x15,
    RAM_RUN_REQUEST         = 0x21,
    RAM_RUN_RESPONSE        = 0x22,
    SET_BAUD_REQUEST        = 0x27,
    SET_BAUD_RESPONSE       = 0x28,
    GET_CHIPID_REQUEST      = 0x32,
    GET_CHIPID_RESPONSE     = 0x33,

    // Protocol v2 commands
    MEM_OPEN_REQUEST        = 0x40,
    MEM_OPEN_RESPONSE       = 0x41,
    MEM_ERASE_REQUEST       = 0x42,
    MEM_ERASE_RESPONSE      = 0x43,
    MEM_BLANK_CHECK_REQUEST = 0x44,
    MEM_BLANK_CHECK_RESPONSE= 0x45,
    MEM_READ_REQUEST        = 0x46,
    MEM_READ_RESPONSE       = 0x47,
    MEM_WRITE_REQUEST       = 0x48,
    MEM_WRITE_RESPONSE      = 0x49,
    MEM_CLOSE_REQUEST       = 0x4A,
    MEM_CLOSE_RESPONSE      = 0x4B,
    MEM_GET_INFO_REQUEST    = 0x4C,
    MEM_GET_INFO_RESPONSE   = 0x4D,
    UNLOCK_ISP_REQUEST      = 0x4E,
    UNLOCK_ISP_RESPONSE     = 0x4F
};

/**
 * @brief Bootloader response codes
 */
enum class BLResponse : uint8_t {
    OK              = 0x00,
    NOT_SUPPORTED   = 0xFF,
    WRITE_FAIL      = 0xFE,
    INVALID_RESPONSE= 0xFD,
    CRC_ERROR       = 0xFC,
    ASSERT_FAIL     = 0xFB,
    USER_INTERRUPT  = 0xFA,
    READ_FAIL       = 0xF9,
    TST_ERROR       = 0xF8,
    AUTH_ERROR      = 0xF7,
    NO_RESPONSE     = 0xF6,
    ERROR           = 0xF0
};

/**
 * @brief Memory region information
 */
struct MemoryInfo {
    uint8_t index;
    uint32_t baseAddress;
    uint32_t size;
    uint32_t blockSize;
    uint8_t access;
    char name[32];
};

/**
 * @brief Chip details
 */
struct ChipDetails {
    uint32_t chipId;
    uint32_t bootloaderVersion;
    char chipName[32];
    MemoryInfo flashInfo;
};

/**
 * @brief Progress callback type
 * @param progress Progress percentage (0-100)
 * @param message Status message
 */
typedef std::function<void(int progress, const char* message)> FlashProgressCallback;

/**
 * @brief ZiGate Flash Programmer class
 *
 * Uses Serial1 (already configured for ZiGate) and GPIO pins from config.h
 */
class ZigateFlasher {
public:
    /**
     * @brief Constructor
     * Uses Serial1 with RESET_ZIGATE and FLASH_ZIGATE pins from config.h
     */
    ZigateFlasher();

    /**
     * @brief Initialize the flasher
     * @return Status
     */
    ZigateFlashStatus init();

    /**
     * @brief Enable/disable GPIO control for bootloader entry
     * @param enable true to use GPIO pins for bootloader entry (default)
     *               false for manual mode (user must put ZiGate in bootloader manually)
     */
    void setGpioControl(bool enable) { _gpioControl = enable; }

    /**
     * @brief Check if GPIO control is enabled
     * @return true if GPIO control is enabled
     */
    bool isGpioControlEnabled() const { return _gpioControl; }

    /**
     * @brief Enter bootloader mode via GPIO (only if GPIO control enabled)
     * @return Status
     */
    ZigateFlashStatus enterBootloader();

    /**
     * @brief Exit bootloader and run firmware
     * @return Status
     */
    ZigateFlashStatus exitBootloader();

    /**
     * @brief Connect to bootloader and read chip info
     * If GPIO control is enabled, will enter bootloader mode first
     * If GPIO control is disabled (manual mode), assumes ZiGate is already in bootloader
     * @return Status
     */
    ZigateFlashStatus connect();

    /**
     * @brief Flash firmware from buffer
     * @param data Firmware data
     * @param length Data length
     * @param callback Progress callback
     * @param skipErase If true, skip flash erase and write directly (faster, works if flash is already erased)
     * @return Status
     */
    ZigateFlashStatus flash(const uint8_t* data, size_t length, FlashProgressCallback callback = nullptr, bool skipErase = false);

    /**
     * @brief Flash firmware from SPIFFS file
     * @param filename Filename in SPIFFS
     * @param callback Progress callback
     * @param skipErase If true, skip flash erase and write directly
     * @return Status
     */
    ZigateFlashStatus flashFromFile(const char* filename, FlashProgressCallback callback = nullptr, bool skipErase = false);

    /**
     * @brief Get chip details (after connect)
     * @return Chip details
     */
    const ChipDetails& getChipDetails() const { return _chipDetails; }

    /**
     * @brief Check if flash operation is in progress
     * @return true if flashing
     */
    bool isFlashing() const { return _isFlashing; }

    /**
     * @brief Get last error message
     * @return Error message
     */
    const char* getLastError() const { return _lastError; }

    /**
     * @brief Cancel ongoing flash operation
     */
    void cancel() { _cancelled = true; }

    /**
     * @brief Set baud rate for flashing
     * @param baudRate Baud rate (38400, 115200, 230400, 460800, 921600, 1000000)
     */
    void setBaudRate(uint32_t baudRate) { _baudRate = baudRate; }

    /**
     * @brief Get current baud rate
     * @return Baud rate
     */
    uint32_t getBaudRate() const { return _baudRate; }

    /**
     * @brief Get status string
     * @param status Status code
     * @return String representation
     */
    static const char* statusToString(ZigateFlashStatus status);

    /**
     * @brief Suspend normal ZiGate communication
     * Call before starting flash operation
     */
    void suspendZigateCommunication();

    /**
     * @brief Resume normal ZiGate communication
     * Call after flash operation completes
     */
    void resumeZigateCommunication();

private:
    // Serial communication - uses Serial1 directly
    uint32_t _baudRate;
    uint32_t _originalBaudRate;

    // State
    bool _initialized;
    bool _connected;
    bool _isFlashing;
    volatile bool _cancelled;
    bool _gpioControl;  // true = use GPIO for bootloader entry, false = manual mode
    char _lastError[128];

    // Chip info
    ChipDetails _chipDetails;

    // Protocol constants
    static constexpr uint32_t BL_TIMEOUT_MS = 1000;
    static constexpr uint32_t CHUNK_SIZE = 512;
    static constexpr size_t MAX_MSG_LENGTH = 2048;

    // Low-level protocol methods
    ZigateFlashStatus sendMessage(BLMessageType type, const uint8_t* header, uint8_t headerLen,
                                   const uint8_t* data = nullptr, uint16_t dataLen = 0);
    BLResponse readMessage(BLMessageType* rxType, uint8_t* rxData, uint16_t* rxLen,
                           uint32_t timeoutMs = BL_TIMEOUT_MS);
    BLResponse request(BLMessageType txType, const uint8_t* header, uint8_t headerLen,
                       const uint8_t* txData, uint16_t txLen,
                       BLMessageType* rxType, uint8_t* rxData, uint16_t* rxLen,
                       uint32_t timeoutMs = BL_TIMEOUT_MS);

    // Bootloader commands - Protocol v2
    ZigateFlashStatus blUnlock();
    ZigateFlashStatus blChipIdRead();
    ZigateFlashStatus blMemInfo(uint8_t index, MemoryInfo* info);
    ZigateFlashStatus blMemOpen(uint8_t index, uint8_t accessMode);
    ZigateFlashStatus blMemErase(uint8_t index, uint32_t address, uint32_t length);
    ZigateFlashStatus blMemWrite(uint8_t index, uint32_t address, const uint8_t* data, uint32_t length);
    ZigateFlashStatus blMemClose(uint8_t index);
    ZigateFlashStatus blReset();

    // Bootloader commands - Protocol v1 (legacy fallback)
    ZigateFlashStatus blFlashEraseV1();
    ZigateFlashStatus blFlashSectorEraseV1(uint8_t sectorNum);
    ZigateFlashStatus blFlashWriteV1(uint32_t address, const uint8_t* data, uint8_t length);
    ZigateFlashStatus flashV1(const uint8_t* data, size_t length, FlashProgressCallback callback);
    ZigateFlashStatus flashV1NoErase(const uint8_t* data, size_t length, FlashProgressCallback callback);

    // Protocol detection
    bool _useProtocolV1;  // true if v2 commands fail, fallback to v1

    // CRC
    uint32_t calcCRC32(const uint8_t* data, size_t length);

    // Helpers
    void setError(const char* format, ...);
    void flushSerial();
};

// Global instance declaration (created in main setup)
extern ZigateFlasher* zigateFlasher;

// Flag to pause normal ZiGate communication during flash
// Check this in your main loop before processing ZiGate protocol
extern volatile bool zigateFlashInProgress;

// Web interface functions
void setupZigateFlasherRoutes();
String handleZigateFlashStatus();
String handleZigateFlashStart();

#endif // ZIGATE_FLASHER_H
