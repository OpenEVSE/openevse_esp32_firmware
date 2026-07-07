# HTTPS and SSH CLI — Implementation and Limitations

## HTTPS

### Overview

The ESP32 gateway runs a Mongoose IoT HTTP server (`MongooseHttpServer`) with optional TLS via **mbedTLS**. HTTP and HTTPS are independently switchable and can run concurrently on separate ports.

### TLS Stack

| Layer | Implementation |
|---|---|
| TLS library | mbedTLS (bundled with Arduino ESP32 / ESP-IDF) |
| RNG | `mg_ssl_if_mbed_random()` → `esp_fill_random()` (hardware HWRNG) |
| ESP32 / ESP32-C3 entropy | Wi-Fi RF noise (always active while the gateway runs) |
| ESP32-P4 entropy | SAR-ADC noise source enabled at boot via `bootloader_random_enable` (no on-die radio) |

The production firmware replaces Mongoose's default `rand()`-based RNG stub (`MG_SSL_MBED_DUMMY_RANDOM`) with a real hardware-backed implementation in `src/mongoose_rng.cpp`.

### Certificate Store

Certificates are managed by `CertificateStore` (`src/certificates.cpp`) and persisted as JSON files in LittleFS under `/certificates/<serial_hex>.json`.

Two certificate types are supported:

| Type | Fields | Use |
|---|---|---|
| `root` | `certificate` (PEM) | Adds a trusted CA to outbound TLS validation (MQTT, OTA, EmonCMS, etc.) |
| `client` | `certificate` + `key` (PEM) | Used as the HTTPS server certificate, or for MQTT mTLS client auth |

On load, each certificate is validated with `CertificateValidator` (mbedTLS on device, OpenSSL in native/test builds). The root CA bundle starts from the compiled-in `root_ca.h` bundle; any user-added root CAs are appended to it at runtime.

**X.509 serial numbers are truncated to the least-significant 8 bytes (64-bit).** Certificates with serials longer than 64 bits are accepted but indexed by the truncated value.

### Configuration Keys

| Key | Default | Description |
|---|---|---|
| `www_https_enabled` | `false` | Enable the HTTPS listener (flag bit `CONFIG_HTTPS_ENABLED`) |
| `www_http_enabled` | `true` | Enable the HTTP listener |
| `www_certificate_id` | `""` | Hex serial of the client cert to use as the server certificate |
| `www_http_port` | `80` | HTTP listen port |
| `www_https_port` | `443` | HTTPS listen port |
| `mqtt_certificate_id` | `""` | Hex serial of the client cert for MQTT mTLS |

`CONFIG_HTTPS_ENABLED` is intentionally excluded from the factory-default flags. HTTPS stays disabled until the user explicitly selects a certificate, preventing a misconfigured HTTPS listener with no working HTTP fallback from locking the device's web UI out entirely.

### Server Startup (`web_server_setup`)

```
if HTTPS enabled AND www_certificate_id set AND cert+key loads OK:
    server.begin(www_https_port, cert, key)   ← TLS listener
    registerWebServerRoutes()
    use_ssl = true

if HTTP enabled OR HTTPS failed to start:
    server.begin(www_http_port)               ← plaintext listener
    registerWebServerRoutes()
```

Routes must be registered once per listener because Mongoose attaches endpoints to the active `mg_connection*` at the time of registration, not to the server object globally. If HTTPS fails to start for any reason (disabled, no cert chosen, cert load failed), HTTP is force-enabled as a fallback so the device is never unreachable.

### HTTP → HTTPS Redirect

`handleHttpsRedirect()` (`src/web_server.cpp`) issues an HTTP 301 with a `Location: https://...` header and a meta-refresh HTML body. The handler exists and can be registered on the plain HTTP listener.

The `redirect` MongooseHttpServer object (line 53) is declared but **`redirect.begin()` is never called** — the dedicated redirect-only listener is not yet implemented. Automatic HTTP→HTTPS redirection is a stub for a future iteration.

### MQTT TLS

When `mqtt_certificate_id` is set, `Mqtt::connect()` calls `_mqttclient.setCertificate(cert, key)` before connecting, enabling mTLS client authentication to the broker. The outbound connection validates the broker's certificate against the runtime root CA bundle (compiled-in CAs plus any user-added root CAs).

### HTTPS Limitations

| Limitation | Detail |
|---|---|
| No HSTS | `Strict-Transport-Security` header is never sent |
| No HTTP→HTTPS redirect | Handler exists; dedicated redirect listener not started |
| No hot TLS reload | `web_server_setup()` runs once at boot; changing the cert or enabling HTTPS requires a reboot |
| No certificate revocation | No OCSP or CRL checking — any valid cert chains to a trusted CA |
| No SNI / virtual hosting | Single server cert for the whole listener |
| Serial number truncation | X.509 serials > 64 bits are accepted but keyed by the truncated LSB value |
| Certificate management CLI gap | `show certificates` / cert import are web-UI-only (binary upload not feasible over a line CLI) |

---

## SSH CLI

### Overview

The SSH CLI provides a Cisco IOS-style interactive management interface over SSH, running alongside the existing HTTP web server. It is enabled by the build flag `ENABLE_SSH_CLI` and active in the `openevse_wifi_v1` and `openevse_wifi_v1_16mb` firmware builds.

### Architecture

The fundamental challenge is that libssh's API is **synchronous and blocking** (`ssh_bind_accept`, `ssh_message_get`, `ssh_channel_read`), while the firmware's main scheduler (MicroTasks) is cooperative and must return promptly. The solution splits responsibilities across two contexts:

```
Main thread (MicroTasks loop)          SSH FreeRTOS task (core 1)
──────────────────────────────         ──────────────────────────
SshServerTask::loop()                  SshServerTask::runServer()
  dequeues CliCmdRequest                 ssh_bind_accept()  ← blocks until client connects
  calls handler(out, argc, argv)         handleSession()
  xSemaphoreGive(req.done)               authenticate()
                                         ssh_channel_read()  ← blocks on client input
                                         CliEngine::feedByte() per byte
                                           ↳ tab/help: runs inline (read-only tree)
                                           ↳ command: xQueueSend(req) + xSemaphoreTake(done)  ← blocks
```

Command **execution** (anything that calls `config_set()`, `EvseManager`, `limit`, etc.) always runs on the main MicroTasks thread via queue + semaphore handoff. Tree-walking, tab-completion, and `?` help run directly on the SSH task since they only read the `const CliNode[]` tree and per-session `CliEngine` state.

### Host Key

- Algorithm: **Ed25519** (generated by libssh `ssh_pki_generate`)
- Storage: LittleFS at `/ssh_host_key.pem` (PEM-armored private key, ~400+ bytes)
- Key generation and loading runs on the **main thread** in `setup()` because it calls `config_set()`/`config_commit()` — the SSH FreeRTOS task only reads the pointer afterwards
- The key persists across reboots. If the stored key fails to import it is silently regenerated

The host key is stored separately from the `user_config` EEPROM-emulated region (which is bounded at 3072 bytes and shared by ~90 other config fields) to prevent silent truncation of the blob.

### Authentication

- Method: **password only** (`SSH_AUTH_METHOD_PASSWORD`)
- Credentials: `ssh_username` and `ssh_password` config keys (set via `set ssh username` / `set ssh password` in CONFIG mode, or the web UI)
- No public-key authentication; no keyboard-interactive

### Session Handling

- **Single-session by construction**: `runServer()` calls `ssh_bind_accept()` again only after the prior `handleSession()` returns. There is never more than one session in flight.
- **Heap gate**: New connections are refused if free heap falls below `SSH_MIN_FREE_HEAP_TO_ACCEPT` (40 KB). libssh session setup costs approximately 20–40 KB.
- **PTY requests**: Accepted with success reply so the client's terminal operates in raw mode — Tab and arrow keys are forwarded as bytes instead of being processed locally by the SSH client.

### CLI Engine

The `CliEngine` (`src/cli/cli_engine.cpp`) is instantiated per-session and holds all per-connection state:

| Feature | Detail |
|---|---|
| Line buffer | `CLI_LINE_MAX` = 128 bytes |
| Token limit | `CLI_MAX_TOKENS` = 12 tokens per line |
| History | `CLI_HISTORY_SIZE` entries (ring buffer), ↑/↓ arrow navigation |
| Tab completion | Unambiguous prefix expansion, hidden nodes excluded (e.g. `do`) |
| `?` help | Lists matching children and argument type hints |
| Abbreviation | Unambiguous prefix matching (e.g. `sh ver` = `show version`) |
| Ctrl-C | Clears the current line and re-prints the prompt |

### CLI Mode Hierarchy

```
host>          Unprivileged EXEC — show version/status/faults, enable
  ↓ enable
host#          Privileged EXEC — all show commands, charge, clear, rfid, reset, reload, install, debug, write, configure terminal
  ↓ configure terminal
host(config)#  Configuration — set, feature, no, service, do
  ↓ exit
host#
  ↓ exit
host>
```

Mode transitions are intercepted by the engine by function-pointer identity before reaching the `CliExecutor` — they are per-session local state and never marshalled to the main thread.

### Threading and Blocking Behaviour

While a CLI handler runs on the main thread, **all MicroTasks-scheduled subsystems stall**: MQTT keepalives, OCPP heartbeats, energy metering, the web server event loop, etc. Handlers must complete quickly. The `show tech-support` command (which calls ~15 sub-handlers in sequence) is the longest expected single invocation.

The SSH I/O task runs on **core 1** (`portNUM_PROCESSORS - 1`). The main MicroTasks loop runs on core 0.

### Build Configuration

```ini
-D ENABLE_SSH_CLI                    ; enable the SSH server
```

Present in: `openevse_wifi_v1`, `openevse_wifi_v1_16mb`.

SSH credentials are stored as plain config strings (`ssh_username`, `ssh_password`) and survive reboot. The host key lives at `/ssh_host_key.pem` in LittleFS and is separate from the config blob.

### SSH Limitations

| Limitation | Detail |
|---|---|
| Password auth only | No public-key or keyboard-interactive authentication |
| Single concurrent session | New connections wait until the active session closes; no session queue |
| No hot credential reload | Username/password changes take effect after reboot (`notifyConfigChanged()` logs a note and returns) |
| No session idle timeout | An idle SSH session holds the listener indefinitely (the heap gate still protects from OOM) |
| No in-line cursor movement | Left/right arrow keys are silently ignored; only home-row editing via backspace |
| Synchronous handler execution | Every command stalls the entire MicroTasks cooperative loop for its duration |
| No SFTP / SCP | Shell channel only; no file transfer |
| No OTA via CLI (binary) | `install <url>` covers URL-based OTA; raw binary upload is web-UI-only |
| No certificate management via CLI | Binary cert/key import is web-UI-only |
| `enable` has no secret | No separate enable password; access to privileged EXEC is controlled by the SSH login credentials only |
| ESP32-P4 RNG caveat | libssh uses the ESP32 hardware HWRNG for Ed25519 key generation; on ESP32-P4 (no on-die radio) this relies on the SAR-ADC noise source being active — confirmed enabled by `hardware_setup()`, but entropy quality is lower than the radio-fed HWRNG on classic ESP32 |
