#include "code_store.h"

#include <Preferences.h>

static Preferences prefs;
static const char* kKeyCode = "code";
static const char* kKeyLastCmdAt = "lastCmdAt";
static const char* kKeyLastLearnAt = "lastLearnAt";
static const char* kKeyLearnActive = "learnActive";

void storeBegin() { prefs.begin("hg", false); }

StoredCode loadCode() {
  StoredCode code;
  if (!prefs.isKey(kKeyCode)) return code;
  std::vector<uint8_t> blob(prefs.getBytesLength(kKeyCode));
  if (blob.empty() || prefs.getBytes(kKeyCode, blob.data(), blob.size()) != blob.size()) return code;
  if (!deserializeCode(blob, code)) return StoredCode{};
  return code;
}

bool saveCode(const StoredCode& code) {
  std::vector<uint8_t> blob = serializeCode(code);
  return prefs.putBytes(kKeyCode, blob.data(), blob.size()) == blob.size();
}

static int64_t loadInt64(const char* key) { return prefs.isKey(key) ? prefs.getLong64(key, 0) : 0; }

static bool saveInt64(const char* key, int64_t value) { return prefs.putLong64(key, value) == sizeof(value); }

int64_t loadLastCommandAt() { return loadInt64(kKeyLastCmdAt); }
bool saveLastCommandAt(int64_t requestedAt) { return saveInt64(kKeyLastCmdAt, requestedAt); }
int64_t loadLastLearnAt() { return loadInt64(kKeyLastLearnAt); }
bool saveLastLearnAt(int64_t requestedAt) { return saveInt64(kKeyLastLearnAt, requestedAt); }
bool loadLearnActive() { return prefs.getBool(kKeyLearnActive, false); }
bool saveLearnActive(bool active) { return prefs.putBool(kKeyLearnActive, active) == sizeof(bool); }

std::string loadPendingWrite(const std::string& relPath) {
  return prefs.isKey(relPath.c_str()) ? prefs.getString(relPath.c_str()).c_str() : "";
}

bool savePendingWrite(const std::string& relPath, const std::string& json) {
  return prefs.putString(relPath.c_str(), json.c_str()) == json.size();
}

void clearPendingWrite(const std::string& relPath) { prefs.remove(relPath.c_str()); }

void clearAll() { prefs.clear(); }
