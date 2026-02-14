# OpenEVSE ESP32 Firmware - Naming Conventions

This document defines the naming conventions used in the OpenEVSE ESP32 Firmware codebase. Following these conventions ensures consistency and maintainability across the project.

## Table of Contents
- [C++ Naming Conventions](#c-naming-conventions)
- [JavaScript/TypeScript Conventions](#javascripttypescript-conventions)
- [Rationale](#rationale)
- [Enforcement](#enforcement)

## C++ Naming Conventions

### 1. Classes and Structs
**Convention:** `PascalCase`

Classes and structs use PascalCase (upper camel case), where each word starts with a capital letter.

**Examples:**
```cpp
class EvseManager { };
class DivertTask { };
struct EnergyMeterData { };
struct WiFiEventStationModeConnected { };
```

**Why:** PascalCase clearly distinguishes type names from variable and function names, making code more readable.

---

### 2. Functions and Methods
**Convention:** `camelCase`

All functions and methods use camelCase (lower camel case), starting with a lowercase letter.

**Examples:**
```cpp
void getState();
void setChargeCurrent(int amps);
bool attemptConnection();
void handleMqttMessage(const String& topic);
```

**Why:** camelCase is the standard for C++ member functions and provides clear distinction from class names.

---

### 3. Member Variables
**Convention:** `_snake_case` (underscore prefix + snake_case)

Private and protected member variables use snake_case with a leading underscore.

**Examples:**
```cpp
class EvseManager {
private:
  int _charge_current;
  int _max_current;
  EvseState _state;
  bool _auto_release;
};
```

**Why:** The underscore prefix clearly identifies member variables vs local variables. Snake_case improves readability for multi-word variable names.

---

### 4. Local Variables
**Convention:** `camelCase` (preferred) or `snake_case` (acceptable)

Local variables should preferably use camelCase, though snake_case is acceptable for consistency with legacy code.

**Examples:**
```cpp
void processData() {
  int chargeCurrent = 0;      // Preferred
  String userName = "admin";   // Preferred
  
  // Legacy style (acceptable)
  int charge_rate = 0;
  bool auto_release = false;
}
```

**Why:** camelCase aligns with function naming and distinguishes locals from members. Legacy snake_case is tolerated to avoid mass refactoring.

---

### 5. Global Variables
**Convention:** `snake_case`

Global variables use lowercase snake_case.

**Examples:**
```cpp
extern String esid;
extern String epass;
extern int solar;
extern int grid_ie;
extern uint32_t flags;
```

**Why:** Lowercase distinguishes globals from constants. Snake_case improves readability.

---

### 6. Constants and Macros
**Convention:** `UPPER_SNAKE_CASE`

All constants, macros, and preprocessor definitions use uppercase with underscores.

**Examples:**
```cpp
#define CONFIG_SERVICE_MQTT       (1 << 1)
#define EVSE_MONITOR_POLL_TIME    5000
#define MAX_INTERVAL              60000

const int MQTT_LOOP_INTERVAL = 100;
const int MQTT_CONNECT_TIMEOUT = 5000;
```

**Why:** Uppercase makes constants immediately recognizable and follows C/C++ convention.

---

### 7. Enumerations

#### 7.1 Plain Enum Types
**Convention:** `PascalCase`

Plain enum type names use PascalCase, consistent with class naming.

**Examples:**
```cpp
enum LedState { ... };
enum WiFiDisconnectReason { ... };
enum ScreenType { ... };
```

#### 7.2 Scoped Enum Types (enum class)
**Convention:** `PascalCase`

Scoped enumerations use PascalCase.

**Examples:**
```cpp
class LimitType {
public:
  enum Value : uint8_t { ... };
};

class DivertMode {
public:
  enum Value : uint8_t { ... };
};
```

#### 7.3 Enum Values - Simple Enums
**Convention:** `UPPER_SNAKE_CASE` with type prefix

For enums representing states, flags, or system values, use UPPER_SNAKE_CASE with the enum type name as a prefix.

**Examples:**
```cpp
enum WiFiDisconnectReason {
  WIFI_DISCONNECT_REASON_UNSPECIFIED = 1,
  WIFI_DISCONNECT_REASON_AUTH_EXPIRE = 2,
  WIFI_DISCONNECT_REASON_AUTH_FAIL = 202
};

enum ScreenType {
  SCREEN_BOOT,
  SCREEN_CHARGE,
  SCREEN_LOCK,
  SCREEN_COUNT
};
```

#### 7.4 Enum Values - Scoped/Nested Enums
**Convention:** `PascalCase` (no prefix needed due to scoping)

For scoped or nested enumerations, use PascalCase without prefix since the scope provides context.

**Examples:**
```cpp
class LimitType {
public:
  enum Value : uint8_t {
    None,      // Used as LimitType::None
    Time,      // Used as LimitType::Time
    Energy,    // Used as LimitType::Energy
    Soc        // Used as LimitType::Soc
  };
};

class DivertMode {
public:
  enum Value : uint8_t {
    Normal = 1,  // Used as DivertMode::Normal
    Eco = 2      // Used as DivertMode::Eco
  };
};
```

**Why:** PascalCase for scoped enums is cleaner since the type name provides context (e.g., `LimitType::Energy` vs `LIMIT_TYPE_ENERGY`). UPPER_SNAKE_CASE for plain enums prevents naming conflicts.

---

### 8. Namespaces
**Convention:** `lowercase` or `snake_case`

Namespaces use lowercase, with underscores for multi-word names if needed.

**Examples:**
```cpp
namespace openevse { ... }
namespace openevse_util { ... }
```

**Why:** Follows C++ standard library convention (std, std::chrono, etc.).

---

### 9. Template Parameters
**Convention:** `PascalCase` or single uppercase letter

Template type parameters use PascalCase or single uppercase letters (T, U, V).

**Examples:**
```cpp
template<typename T>
class Container { ... };

template<typename ValueType>
class GenericHandler { ... };
```

**Why:** Distinguishes template parameters from regular types.

---

## JavaScript/TypeScript Conventions

For the GUI codebase (gui-v2 directory), follow standard JavaScript/TypeScript conventions:

- **Variables and Functions:** `camelCase`
- **Classes and Types:** `PascalCase`
- **Constants:** `UPPER_SNAKE_CASE`
- **Private Fields:** `#fieldName` (private class fields)
- **Files:** `kebab-case.ts` or `kebab-case.svelte`

Refer to the ESLint configuration in `gui-v2/.eslintrc.json` for details.

---

## Rationale

### Why These Conventions?

1. **Consistency:** Uniform naming reduces cognitive load when reading code
2. **Clarity:** Different conventions for different identifiers make code structure obvious
3. **Safety:** Distinguishing members (`_var`) from locals (`var`) prevents shadowing bugs
4. **Standards:** Aligns with common C++ and embedded systems practices
5. **Tooling:** Enables automated checking via clang-tidy and linters

### When to Deviate

- **Legacy Code:** When modifying existing code, maintain local consistency
- **External APIs:** Match naming of external libraries (e.g., Arduino WiFi.status())
- **Generated Code:** Auto-generated code may use different conventions
- **Third-party Libraries:** Library code maintains its original conventions

---

## Enforcement

### Automated Checking

The project uses **clang-tidy** for automated naming convention enforcement. The configuration is in `.clang-tidy`.

#### Running Clang-Tidy Locally

```bash
# Check a single file
clang-tidy src/evse_man.cpp -- -Isrc -Ilib

# Check all source files
find src -name '*.cpp' -exec clang-tidy {} -- -Isrc -Ilib \;
```

#### GitHub Actions CI

A GitHub Actions workflow automatically runs clang-tidy on all pull requests. The workflow fails if naming violations are detected.

**Workflow:** `.github/workflows/lint.yaml`

### Manual Review

In addition to automated checks, code reviewers should verify:
1. New classes use PascalCase
2. New functions use camelCase
3. New member variables use `_snake_case`
4. New constants use UPPER_SNAKE_CASE
5. Enum types and values follow the appropriate convention

---

## Examples of Correct Usage

### Complete Class Example

```cpp
class EvseMonitor {
public:
  // Public methods use camelCase
  void beginMonitor();
  int getChargeCurrent() const;
  void setChargeCurrent(int amps);
  
  // Public constants use UPPER_SNAKE_CASE
  static const int DEFAULT_POLL_TIME = 5000;
  
private:
  // Private members use _snake_case
  int _charge_current;
  int _max_current;
  EvseState _state;
  bool _auto_release;
  
  // Private methods also use camelCase
  void updateState();
  bool checkLimits();
};
```

### Enum Example

```cpp
// Plain enum with prefixed values
enum ScreenType {
  SCREEN_BOOT,
  SCREEN_CHARGE,
  SCREEN_LOCK
};

// Scoped enum with PascalCase values
class DivertMode {
public:
  enum Value : uint8_t {
    Normal = 1,
    Eco = 2
  };
};
```

### Function Example

```cpp
void handleMqttMessage(const String& topic, const String& payload) {
  // Local variables use camelCase (preferred)
  int messageLength = payload.length();
  bool isValid = validatePayload(payload);
  
  // Constants use UPPER_SNAKE_CASE
  const int MAX_PAYLOAD_SIZE = 1024;
  
  if (messageLength > MAX_PAYLOAD_SIZE) {
    return;
  }
  
  // Update member variable
  _last_message_time = millis();
}
```

---

## Migration Strategy

For existing code that doesn't follow these conventions:

1. **No mass refactoring:** Don't rename everything at once
2. **New code compliance:** All new code must follow conventions
3. **Local cleanup:** When modifying a file, fix naming in that file
4. **Gradual improvement:** Naming improves over time through incremental changes

---

## Questions?

If you're unsure about a naming convention:

1. Check this document first
2. Look for similar examples in the codebase
3. Consult `.clang-tidy` configuration
4. Ask in PR reviews or discussions

When in doubt, consistency with surrounding code takes precedence.

---

**Last Updated:** 2026-02-14  
**Version:** 1.0
