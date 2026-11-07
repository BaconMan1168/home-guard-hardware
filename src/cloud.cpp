#include "cloud.h"

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <FirebaseClient.h>
#include <NetworkClientSecure.h>

#include <optional>

#include "secrets.h"
#include "stream_events.h"

static NetworkClientSecure writeSsl;
static NetworkClientSecure streamSsl;
static AsyncClientClass writeClient(writeSsl);
static AsyncClientClass streamClient(streamSsl);
static FirebaseApp app;
static RealtimeDatabase database;

static std::string basePath;
static CloudCallbacks callbacks;
static bool streamStarted = false;
static std::optional<Command> pendingCommand;
static std::optional<int64_t> pendingLearnRequestAt;

static void logIfError(AsyncResult& result) {
  if (result.isError()) {
    Serial.printf("[cloud] %s error %d: %s\n", result.uid().c_str(), result.error().code(),
                  result.error().message().c_str());
  }
}

static void onAuthResult(AsyncResult& result) {
  if (result.isEvent()) {
    Serial.printf("[cloud] auth: %s (%d)\n", result.eventLog().message().c_str(), result.eventLog().code());
  }
  logIfError(result);
}

static void onStreamResult(AsyncResult& result) {
  logIfError(result);
  if (!result.available()) return;
  RealtimeDatabaseResult& stream = result.to<RealtimeDatabaseResult>();
  if (!stream.isStream()) return;
  String event = stream.event();
  if (event != "put" && event != "patch") return;

  StreamUpdate update = parseCommandsEvent(stream.dataPath().c_str(), stream.to<String>().c_str());
  if (update.latest) pendingCommand = update.latest;
  if (update.learnRequestAt) pendingLearnRequestAt = update.learnRequestAt;
}

void cloudBegin(const std::string& deviceId, CloudCallbacks cb) {
  basePath = "/devices/" + deviceId;
  callbacks = std::move(cb);
  writeSsl.setInsecure();
  streamSsl.setInsecure();

  UserAccount user(FIREBASE_API_KEY);
  signup(writeClient, app, getAuth(user), onAuthResult, "auth");
  app.getApp<RealtimeDatabase>(database);
  database.url(FIREBASE_DATABASE_URL);
}

void cloudLoop() {
  app.loop();

  if (pendingCommand) {
    Command cmd = *pendingCommand;
    pendingCommand.reset();
    if (callbacks.onCommand) callbacks.onCommand(cmd);
  }
  if (pendingLearnRequestAt) {
    int64_t requestedAt = *pendingLearnRequestAt;
    pendingLearnRequestAt.reset();
    if (callbacks.onLearnRequest) callbacks.onLearnRequest(requestedAt);
  }
}

bool cloudReady() { return app.ready(); }

void cloudStartStream() {
  if (streamStarted) return;
  streamStarted = true;
  database.get(streamClient, String((basePath + "/commands").c_str()), onStreamResult, true, "commands");
}

void cloudSet(const std::string& relPath, const std::string& json) {
  database.set<object_t>(writeClient, String((basePath + "/" + relPath).c_str()), object_t(String(json.c_str())),
                         logIfError, "set");
}

void cloudPush(const std::string& relPath, const std::string& json) {
  database.push<object_t>(writeClient, String((basePath + "/" + relPath).c_str()), object_t(String(json.c_str())),
                          logIfError, "push");
}
