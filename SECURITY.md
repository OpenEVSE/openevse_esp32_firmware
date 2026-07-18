# Security Policy

OpenEVSE takes the security of its hardware, firmware, and software seriously. Because OpenEVSE products control high-power EV charging equipment, security issues can have both cybersecurity and physical safety implications. We appreciate responsible disclosure from the security community.

## Scope

This policy covers the OpenEVSE open-source projects, including:

- [open_evse](https://github.com/OpenEVSE/open_evse) — EVSE controller firmware (ATmega)
- [openevse_esp32_firmware](https://github.com/OpenEVSE/openevse_esp32_firmware) — ESP32 WiFi gateway firmware and web UI
- [OpenEVSE_Lib](https://github.com/OpenEVSE/OpenEVSE_Lib) and other libraries/tools under the [OpenEVSE organization](https://github.com/OpenEVSE)

Examples of issues in scope:

- Authentication or authorization bypass in the web interface or HTTP/MQTT/RAPI APIs
- Remote code execution, memory corruption, or buffer overflows in firmware
- Firmware update (OTA) integrity issues
- Vulnerabilities that could cause unsafe charging behavior (e.g., bypassing pilot/GFCI safety logic remotely)
- Credential leakage (WiFi, MQTT, OCPP, or cloud service credentials)
- Cross-site scripting (XSS), CSRF, or injection in the web UI

Out of scope:

- Issues requiring physical access to the device's internal serial/JTAG headers (physical access is assumed to grant control)
- Attacks requiring the attacker to already be an authenticated administrator
- Denial of service via the local network against a device with authentication disabled by the owner
- Vulnerabilities in third-party dependencies with no demonstrated impact on OpenEVSE (report those upstream)
- Social engineering, phishing, or attacks on OpenEVSE staff or infrastructure

## Supported Versions

Only the latest stable releases receive security fixes:

| Project | Version | Supported |
| ------- | ------- | --------- |
| openevse_esp32_firmware (WiFi) | latest stable (5.x) | ✅ |
| openevse_esp32_firmware (WiFi) | 4.x and earlier | ❌ |
| open_evse (controller) | latest stable (9.x / 8.2.x) | ✅ |
| open_evse (controller) | 8.1.x and earlier | ❌ |
| ESP8266 WiFi firmware (legacy) | any | ❌ (end of life — upgrade to ESP32 hardware) |

Development (`dev`/pre-release) builds are not supported for production use, but security reports against them are still welcome.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues, discussions, or the community forum.**

Preferred: use GitHub's private vulnerability reporting — open the **Security** tab of the affected repository and click **"Report a vulnerability"**. This creates a private advisory visible only to maintainers.

Alternatively, email **support@openevse.com** with "SECURITY" in the subject line.

Please include as much of the following as you can:

- The project and version/commit affected (e.g., WiFi firmware v5.1.5)
- Hardware involved, if relevant (OpenEVSE controller revision, ESP32 gateway model)
- A description of the vulnerability and its impact
- Steps to reproduce, proof-of-concept code, or captures
- Any suggested fix or mitigation

## What to Expect

- **Acknowledgement** of your report within **5 business days**
- An initial **assessment and severity triage** within **14 days**
- Ongoing status updates as we work on a fix
- A **coordinated disclosure**: we ask that you keep the issue private until a fixed release is available. We aim to ship fixes for confirmed high-severity issues within **90 days** of the report
- **Credit** in the release notes and/or security advisory, if you would like it (we're happy to keep you anonymous instead)

OpenEVSE is a small open-source team; we don't operate a paid bug bounty program, but we genuinely value and publicly credit good-faith research.

## Safe Harbor

We will not pursue legal action against researchers who:

- Make a good-faith effort to follow this policy
- Avoid privacy violations, data destruction, and disruption of other users' chargers or the OpenEVSE cloud/demo services
- Test only against devices they own or have explicit permission to test
- Give us reasonable time to remediate before public disclosure

## Keeping Your Device Secure

For users, the best protections are:

- Keep both the controller and WiFi firmware updated to the latest stable release
- Set a strong web interface password and do not disable authentication
- Do not expose the charger's web interface directly to the internet — use a VPN or a trusted remote-access service instead
- Use TLS-enabled MQTT/OCPP endpoints where available
