# OpenEVSE ESP32 Firmware

OpenEVSE ESP32 Firmware is an ESP32-based WiFi gateway for OpenEVSE charging stations that provides web UI control, MQTT integration, solar divert functionality, and OCPP support.

Always reference these instructions first and fallback to search or bash commands only when you encounter unexpected information that does not match the info here.

## Working Effectively

### Bootstrap the repository:
- `git submodule update --init --recursive`
- `cd gui-v2`
- `npm install` -- takes ~20 seconds. 
- `npm run build` -- takes ~10 seconds.
- `cd ..`

### Build and test the repository:
- `pip install --upgrade platformio` -- install PlatformIO build system
- `pio run -e openevse_wifi_v1` -- build default environment. **NEVER CANCEL**. First build takes 15-45 minutes (downloads ESP32 toolchain ~500MB). Set timeout to 60+ minutes.
- **Common Build Failure**: "HTTPClientError" indicates network restrictions blocking PlatformIO registry (api.registry.platformio.org)
- Alternative boards: `pio run -e adafruit_huzzah32`, `pio run -e nodemcu-32s`, `pio run -e olimex_esp32-gateway-e`
- **Full CI Build**: Tests 15+ board configurations, takes ~60+ minutes total

### Test the repository:
- `cd divert_sim`
- `pip install -r requirements.txt`
- `pytest -v` -- runs Python-based divert simulation tests. Takes ~5 seconds.
- HTTP API tests available in `test/*.http` files (require VS Code REST Client extension)

### Development environment setup:
- Install Node.js (v20+) and npm for GUI development
- Install Python 3.x for build scripts and testing
- Install PlatformIO for ESP32 firmware compilation
- Git submodules are required for GUI source code

## Critical Build Dependencies

### Network Requirements:
- **CRITICAL**: PlatformIO requires internet access to download ESP32 platform packages and libraries
- First-time builds download ~500MB of ESP32 toolchain and libraries
- If you encounter "HTTPClientError" or connection timeouts, this indicates network restrictions blocking PlatformIO registry access
- **WORKAROUND**: If builds fail due to network restrictions, focus on GUI development and testing components that work offline

### GUI Build Process (REQUIRED before firmware):
- The web GUI is built separately as static assets then embedded into ESP32 firmware
- GUI source is in git submodule `gui-v2` (separate repository)
- **ALWAYS** run GUI build before firmware build:
  1. `git submodule update --init --recursive`
  2. `cd gui-v2 && npm install && npm run build`
  3. Return to project root for firmware build

### Build Timing Expectations:
- **GUI npm install**: ~20 seconds
- **GUI npm run build**: ~10 seconds  
- **ESP32 first build**: 15-45 minutes (downloads toolchain)
- **ESP32 incremental builds**: 2-5 minutes
- **NEVER CANCEL** long-running builds - ESP32 compilation is inherently slow

## Validation

### What You CAN Test Without Hardware:
- **GUI Development**: Full web interface build and preview
- **Configuration Logic**: Python-based config validation tests
- **Divert Algorithm**: Solar divert simulation testing
- **Build Process**: Firmware compilation (when network allows)

### What You CANNOT Test Without Hardware:
- **Runtime Functionality**: ESP32 firmware execution
- **API Endpoints**: HTTP/MQTT communication
- **OTA Updates**: Over-the-air firmware updates
- **Hardware Features**: LCD display, RFID, temperature sensors

### MANDATORY Validation Steps After Any Changes:
1. **Test GUI Build**: `cd gui-v2 && npm run build` -- must complete without errors
2. **Verify Submodules**: `git submodule status` -- must show clean state
3. **Run Python Tests**: `cd divert_sim && pytest -v` -- must pass when dependencies available
4. **Check Build Config**: Review `platformio.ini` for any new board configurations

### Complete Validation Workflow:
```bash
# 1. Verify git submodules are initialized
git submodule status

# 2. Build and verify GUI assets
cd gui-v2
npm install
npm run build
ls -la dist/  # Should contain assets
cd ..

# 3. Test Python components (if dependencies available)
cd divert_sim
pip install -r requirements.txt
python3 -c "import test_config; print('Config tests importable')"
cd ..

# 4. Attempt ESP32 build (will fail with network restrictions)
pio run -e openevse_wifi_v1  # Expected to fail with HTTPClientError in restricted environments
```

### Expected Validation Results:
- **GUI build**: Always succeeds (~10 seconds)
- **Python imports**: Always succeed when pytest installed
- **ESP32 build**: Fails in network-restricted environments, succeeds with internet access

## Common Tasks

### Adding new web UI features:
1. Navigate to `gui-v2/src/` for Svelte components
2. Make changes to UI components
3. Test with `npm run dev` (starts development server)
4. Build with `npm run build`
5. Rebuild ESP32 firmware to embed new assets

### Modifying firmware behavior:
1. Edit source code in `src/` directory
2. Key files: `src/app_config.cpp` (configuration), `src/web_server.cpp` (HTTP endpoints)
3. Build specific board: `pio run -e your_board_name`
4. Upload to hardware: `pio run -e your_board_name -t upload`

### Working with configuration:
- Board configurations in `platformio.ini` with 15+ supported ESP32 variants
- **Default environment**: `openevse_wifi_v1` (ESP32 with NeoPixel LEDs, MCP9808 temperature sensor)
- **Common development boards**: 
  - `adafruit_huzzah32` - Adafruit Feather ESP32
  - `nodemcu-32s` - NodeMCU-32S development board  
  - `espressif_esp-wrover-kit` - Espressif development kit
  - `olimex_esp32-gateway-e` - Olimex Gateway with Ethernet
- **Debug environments**: Add `_dev` suffix for debug builds (e.g., `openevse_wifi_v1_dev`)
- **TFT Display**: `openevse_wifi_tft_v1` for boards with LCD touchscreen
- **Ethernet Support**: `olimex_esp32-gateway-e`, `wt32-eth01`, `olimex_esp32-poe-iso`

## Repository Structure

### Key directories:
- `src/` - ESP32 firmware source code (C++)
- `gui-v2/` - Web UI source (Svelte/JavaScript, git submodule)
- `docs/` - User and developer documentation
- `test/` - HTTP API test files
- `divert_sim/` - Python-based simulation and testing
- `scripts/` - Build automation scripts
- `.github/workflows/` - CI/CD pipeline definitions

### Important files:
- `platformio.ini` - Build configuration for all board variants
- `src/app_config.cpp` - Device configuration management
- `gui-v2/package.json` - Web UI dependencies and build scripts
- `readme.md` - Project overview and requirements

### Build outputs:
- ESP32 firmware: `.pio/build/{board_name}/firmware.bin`
- GUI assets: `gui-v2/dist/` (embedded into firmware)
- Python test results: `divert_sim/output/`

## Troubleshooting Common Issues

### "HTTPClientError" or "Could not resolve host: api.registry.platformio.org":
- **Cause**: Network firewall blocking PlatformIO package registry
- **Impact**: Cannot download ESP32 platform packages (~500MB) or library dependencies
- **Workaround**: Focus on GUI development and Python testing components
- **Alternative**: Download pre-built firmware binaries from GitHub releases

### "Warning: GUI files not found" during build:
- **Cause**: Git submodules not initialized OR GUI not built
- **Solution**: `git submodule update --init --recursive && cd gui-v2 && npm install && npm run build`

### "Warning: Node.JS and NPM required to update the UI":
- **Cause**: Node.js not installed or not in PATH
- **Solution**: Install Node.js v20+ and ensure `node` and `npm` commands are available

### ESP32 upload failures ("Failed to connect to ESP32"):
- **Cause**: Hardware not connected, wrong USB port, or ESP32 not in bootloader mode
- **Solution**: Check connections, try different USB port, manually enter bootloader mode
- **Bootloader Mode**: Hold BOOT button, press RESET button, release RESET, release BOOT

### Long build times or apparent hangs:
- **ESP32 builds take 15-45 minutes** on first build (downloads toolchain)
- **NEVER CANCEL** builds that appear stuck - ESP32 compilation is inherently slow  
- Monitor `.platformio` directory size growth to confirm download progress
- Subsequent builds are faster (2-5 minutes) using cached dependencies

## API and Development References

- **API Documentation**: https://openevse.stoplight.io/docs/openevse-wifi-v4/
- **User Guide**: `docs/user-guide.md`  
- **Developer Guide**: `docs/developer-guide.md`
- **MQTT API**: `docs/mqtt.md`
- **CI/CD Pipeline**: `.github/workflows/build.yaml`