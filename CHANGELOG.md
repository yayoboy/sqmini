# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2024-11-17

### Added
- Initial release of SQLite Micro
- Support for ESP32 platform
- Support for RP2040 (Raspberry Pi Pico) platform
- SPIFFS VFS implementation for ESP32
- LittleFS VFS implementation for ESP32 and RP2040
- SD Card VFS implementation for ESP32 and RP2040
- Memory-optimized SQLite configuration for embedded systems
- Comprehensive examples for all platforms and storage types
- PlatformIO configuration files
- Arduino library support via library.json
- Complete API documentation in README
- Memory statistics tracking

### Configuration
- Default page size: 512 bytes (optimized for flash)
- Default cache size: 64KB (ESP32), 32KB (RP2040)
- Disabled unused SQLite features to reduce code size
- Thread-safe mode (serialized) enabled

### Examples
- ESP32 SPIFFS example with basic CRUD operations
- ESP32 LittleFS example with transactions and aggregations
- ESP32 SD Card example with data logging
- RP2040 LittleFS example with IoT sensor data
- RP2040 SD Card example with environmental monitoring

### Documentation
- Complete README with Italian documentation
- API reference
- Usage examples
- Best practices
- Troubleshooting guide
- Performance benchmarks

## Future Enhancements

### Planned for v1.1.0
- [ ] Support for raw flash storage
- [ ] Wear leveling for flash-based storage
- [ ] Query optimizer hints for embedded systems
- [ ] Background database compaction
- [ ] Encryption support (optional)
- [ ] Multi-database support
- [ ] Improved error handling and logging

### Planned for v1.2.0
- [ ] Support for STM32 platform
- [ ] Support for nRF52 platform
- [ ] FreeRTOS integration examples
- [ ] Over-the-air database sync
- [ ] Database backup/restore utilities

### Long-term Ideas
- [ ] Web interface for database browsing (via WiFi)
- [ ] MQTT integration for remote queries
- [ ] Time-series optimizations
- [ ] Compression for archived data
- [ ] Database migration tools

---

**Note**: Version numbers follow [Semantic Versioning](https://semver.org/)
