#ifdef ENABLE_SD_CARD

#include <Arduino.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config_backup.h"
#include "app_config.h"
#include "sd_card.h"
#include "debug.h"

static bool _armed = false;

void config_backup_arm()
{
  _armed = true;

  // A card that has never seen a config change carries no mirror, and swapping
  // in a fresh card is the ordinary way to end up with one -- which left the
  // board silently unprotected until the user next happened to edit a setting.
  // Write it now instead, so the backup exists from the first boot with a card
  // in the slot.
  config_backup_ensure_on_card();
}

bool config_backup_ensure_on_card()
{
  if(!_armed || !sd_card_mounted()) {
    return false;
  }

  struct stat st;
  if(stat(CONFIG_BACKUP_PATH, &st) == 0) {
    return false;
  }

  DBUGLN("[config-backup] card has no mirror, writing one");
  return config_backup_to_card();
}

bool config_backup_to_card()
{
  if(!_armed || !sd_card_mounted()) {
    return false;
  }

  String json;
  if(!config_serialize(json, true, true, false)) {
    DBUGLN("[config-backup] serialize failed");
    return false;
  }

  mkdir(CONFIG_BACKUP_DIR, 0777);   // EEXIST is fine

  // Write to a sibling and rename over the mirror so a reset mid-write leaves
  // the previous good copy in place rather than a truncated one.
  const char *tmp = CONFIG_BACKUP_PATH ".tmp";
  FILE *fp = fopen(tmp, "wb");
  if(fp == nullptr) {
    DBUGLN("[config-backup] open failed");
    return false;
  }
  bool ok = fwrite(json.c_str(), 1, json.length(), fp) == json.length()
         && fflush(fp) == 0
         && fsync(fileno(fp)) == 0;
  fclose(fp);
  if(ok) {
    remove(CONFIG_BACKUP_PATH);     // FATFS rename will not overwrite
    ok = rename(tmp, CONFIG_BACKUP_PATH) == 0;
  }
  if(!ok) {
    DBUGLN("[config-backup] write failed");
    remove(tmp);
    return false;
  }
  DBUGF("[config-backup] mirrored %u bytes to %s", json.length(), CONFIG_BACKUP_PATH);
  return true;
}

bool config_restore_from_card()
{
  if(!sd_card_mounted()) {
    return false;
  }

  FILE *fp = fopen(CONFIG_BACKUP_PATH, "rb");
  if(fp == nullptr) {
    DBUGLN("[config-backup] no mirror on the card");
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // Config JSON is a few KB; anything huge is not ours.
  if(size <= 2 || size > 16384) {
    DBUGF("[config-backup] mirror is %ld bytes, ignoring it", size);
    fclose(fp);
    return false;
  }

  String json;
  json.reserve(size + 1);
  char buf[256];
  size_t n;
  while((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
    json.concat(buf, n);
  }
  fclose(fp);

  if(!config_deserialize(json)) {
    DBUGLN("[config-backup] mirror did not parse, ignoring it");
    return false;
  }
  config_user_commit();
  DBUGF("[config-backup] restored %ld bytes from %s", size, CONFIG_BACKUP_PATH);
  return true;
}

#endif // ENABLE_SD_CARD
