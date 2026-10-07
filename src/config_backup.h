// Mirror of the user configuration on the microSD card, so a board whose flash
// has been wiped (new module, repartition, factory reset by mistake) can pick
// its settings back up from the card at boot.
//
// The mirror is the complete config, secrets included -- a copy without the
// WiFi and MQTT credentials would not bring a board back on the network, which
// is the whole point. It is plaintext on a card that lives inside the
// enclosure; treat the card accordingly.
#ifndef __CONFIG_BACKUP_H
#define __CONFIG_BACKUP_H

#ifndef CONFIG_BACKUP_DIR
#define CONFIG_BACKUP_DIR  "/sdcard/openevse"
#endif
#ifndef CONFIG_BACKUP_PATH
#define CONFIG_BACKUP_PATH CONFIG_BACKUP_DIR "/config.json"
#endif

#ifdef ENABLE_SD_CARD

// Until this is called, config_backup_to_card() does nothing. Boot calls it
// once the restore decision has been made, so the housekeeping commits that
// config_load_settings() makes on a default config cannot overwrite a good
// mirror before it has been read.
void config_backup_arm();

// Write the current user config to the card. No-op when the card is not
// mounted or the backup is not armed. Returns true on a successful write.
bool config_backup_to_card();

// Load the mirror into the live config and commit it to flash. Returns true
// if a config was restored; the caller should restart so every subsystem
// starts from the restored settings.
bool config_restore_from_card();

// Write the mirror, but only when the card does not already carry one.
//
// Safe to call whenever a card turns up. It never overwrites an existing
// mirror -- by the time this can do anything, boot has already decided not to
// restore from that mirror, so replacing it with the running config would
// throw away the better copy in the case where the running config is the
// defaults. It is also inert until config_backup_arm() has run, which is what
// makes it harmless on the boot mount.
bool config_backup_ensure_on_card();

#else

static inline void config_backup_arm() { }
static inline bool config_backup_to_card() { return false; }
static inline bool config_restore_from_card() { return false; }
static inline bool config_backup_ensure_on_card() { return false; }

#endif // ENABLE_SD_CARD

#endif // __CONFIG_BACKUP_H
