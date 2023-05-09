#ifdef M5UNIFIED
#include <M5GFX.h>
#include <M5Unified.h>
#include <SD.h>
#else
#include <SPIFFS.h>
#endif

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "webpages.h"

#define FIRMWARE_VERSION "v0.0.1"
#define MOUNT_POINT "/sd"

const String default_ssid = WIFI_SSID;
const String default_wifipassword = WIFI_PASSWORD;
const String default_httpuser = "admin";
const String default_httppassword = "admin";
const String slash = "/";
const int default_webserverporthttp = 80;

// configuration structure
struct Config {
  String ssid;           // wifi ssid
  String wifipassword;   // wifi password
  String httpuser;       // username to access web admin
  String httppassword;   // password to access web admin
  int webserverporthttp; // http port number for web admin
};

// variables
Config config;             // configuration
bool shouldReboot = false; // schedule a reboot
AsyncWebServer *server;    // initialise webserver

// function defaults
String listFiles(bool ishtml = false);
void rebootESP(String message);
void configureWebServer();
String humanReadableSize(const size_t bytes);
String processor(const String &var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index,
                  uint8_t *data, size_t len, bool final);
void notFound(AsyncWebServerRequest *request);
bool checkUserWebAuth(AsyncWebServerRequest *request);

void setup() {
#ifdef M5UNIFIED
  auto cfg = M5.config();
  cfg.serial_baudrate =
      BAUD; // default=115200. if "Serial" is not needed, set it to 0.
  cfg.led_brightness = 128; // default= 0. system LED brightness (0=off /
                            // 255=max) (※ not NeoPixel)
  M5.begin(cfg);
#else
  Serial.begin(BAUD);
#endif

  /* Wait for the Serial Monitor */
  while (!Serial) {
    yield();
  }
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);

  Serial.println("Booting ...");

#ifdef M5UNIFIED
  while (false == SD.begin(GPIO_NUM_4, SPI, 25000000, MOUNT_POINT, 5, true)) {
    delay(500);
    Serial.println("SD waiting...");
  }
  Serial.println("SD mounted.");
#else

  if (!SD.begin(true)) {
    // if you have not used SD before on a ESP32, it will show this error.
    // after a reboot SD will be configured and will happily work.
    Serial.println("ERROR: Cannot mount SD, Rebooting");
    rebootESP("ERROR: Cannot mount SD, Rebooting");
  }
#endif

  Serial.print("SD Free: ");
  Serial.println(humanReadableSize((SD.totalBytes() - SD.usedBytes())));
  Serial.print("SD Used: ");
  Serial.println(humanReadableSize(SD.usedBytes()));
  Serial.print("SD Total: ");
  Serial.println(humanReadableSize(SD.totalBytes()));

  Serial.println(listFiles());

  Serial.println("Loading Configuration ...");

  config.ssid = default_ssid;
  config.wifipassword = default_wifipassword;
  config.httpuser = default_httpuser;
  config.httppassword = default_httppassword;
  config.webserverporthttp = default_webserverporthttp;

  Serial.print("\nConnecting to Wifi: ");
  WiFi.begin(config.ssid.c_str(), config.wifipassword.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n\nNetwork Configuration:");
  Serial.println("----------------------");
  Serial.print("         SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("  Wifi Status: ");
  Serial.println(WiFi.status());
  Serial.print("Wifi Strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("          MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("           IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("       Subnet: ");
  Serial.println(WiFi.subnetMask());
  Serial.print("      Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("        DNS 1: ");
  Serial.println(WiFi.dnsIP(0));
  Serial.print("        DNS 2: ");
  Serial.println(WiFi.dnsIP(1));
  Serial.print("        DNS 3: ");
  Serial.println(WiFi.dnsIP(2));
  Serial.println();

  // configure web server
  Serial.println("Configuring Webserver ...");
  server = new AsyncWebServer(config.webserverporthttp);
  configureWebServer();

  // startup web server
  Serial.println("Starting Webserver ...");
  server->begin();
}

void loop() {
  // reboot if we've told it to reboot
  if (shouldReboot) {
    rebootESP("Web Admin Initiated Reboot");
  }
}

void rebootESP(String message) {
  Serial.print("Rebooting ESP32: ");
  Serial.println(message);
  ESP.restart();
}

// list all of the files, if ishtml=true, return html rather than simple text
String listFiles(bool ishtml) {
  String returnText = "";
  Serial.println("Listing files stored on SD");
  File root = SD.open("/");
  File foundfile = root.openNextFile();
  if (ishtml) {
    returnText += "<table><tr><th align='left'>Name</th><th "
                  "align='left'>Size</th><th></th><th></th></tr>";
  }
  while (foundfile) {
    if (ishtml) {
      returnText += "<tr align='left'><td>" + String(foundfile.name()) +
                    "</td><td>" + humanReadableSize(foundfile.size()) + "</td>";
      returnText += "<td><button onclick=\"downloadDeleteButton(\'" +
                    String(foundfile.name()) +
                    "\', \'download\')\">Download</button>";
      returnText += "<td><button onclick=\"downloadDeleteButton(\'" +
                    String(foundfile.name()) +
                    "\', \'delete\')\">Delete</button></tr>";
    } else {
      returnText += "File: " + String(foundfile.name()) +
                    " Size: " + humanReadableSize(foundfile.size()) + "\n";
    }
    foundfile = root.openNextFile();
  }
  if (ishtml) {
    returnText += "</table>";
  }
  root.close();
  foundfile.close();
  return returnText;
}

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(const size_t bytes) {
  if (bytes < 1024)
    return String(bytes) + " B";
  else if (bytes < (1024 * 1024))
    return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024))
    return String(bytes / 1024.0 / 1024.0) + " MB";
  else
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

// parses and processes webpages
// if the webpage has %SOMETHING% or %SOMETHINGELSE% it will replace those
// strings with the ones defined
String processor(const String &var) {
  if (var == "FIRMWARE") {
    return FIRMWARE_VERSION;
  }

  if (var == "FREESPIFFS") {
    return humanReadableSize((SD.totalBytes() - SD.usedBytes()));
  }

  if (var == "USEDSPIFFS") {
    return humanReadableSize(SD.usedBytes());
  }

  if (var == "TOTALSPIFFS") {
    return humanReadableSize(SD.totalBytes());
  }
  return "";
}

void configureWebServer() {
  // configure web server

  // if url isn't found
  server->onNotFound(notFound);

  // run handleUpload function when any file is uploaded
  server->onFileUpload(handleUpload);

  // visiting this page will cause you to be logged out
  server->on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->requestAuthentication();
    request->send(401);
  });

  // presents a "you are now logged out webpage
  server->on("/logged-out", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        " " + request->url();
    Serial.println(logmessage);
    request->send_P(401, "text/html", logout_html, processor);
  });

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        +" " + request->url();

    if (checkUserWebAuth(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);
      request->send_P(200, "text/html", index_html, processor);
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
  });

  server->on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        " " + request->url();

    if (checkUserWebAuth(request)) {
      request->send(200, "text/html", reboot_html);
      logmessage += " Auth: Success";
      Serial.println(logmessage);
      shouldReboot = true;
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
  });

  server->on("/listfiles", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        " " + request->url();
    if (checkUserWebAuth(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);
      request->send(200, "text/plain", listFiles(true));
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
  });

  server->on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        " " + request->url();
    if (checkUserWebAuth(request)) {
      logmessage += " Auth: Success";
      Serial.println(logmessage);

      if (request->hasParam("name") && request->hasParam("action")) {
        const String fnStr = slash + request->getParam("name")->value();
        const char *fileName = fnStr.c_str();
        const char *fileAction = request->getParam("action")->value().c_str();

        logmessage = "Client:" + request->client()->remoteIP().toString() +
                     " " + request->url() + "?name=" + String(fileName) +
                     "&action=" + String(fileAction);

        if (!SD.exists(fileName)) {
          Serial.printf(" ERROR: file does not exist, '%s' '%s'\n",
                        logmessage.c_str(), fileName);
          request->send(400, "text/plain", "ERROR: file does not exist");
        } else {
          Serial.println(logmessage + " file exists");
          if (strcmp(fileAction, "download") == 0) {
            logmessage += " downloaded";
            Serial.printf("downloading: '%s'\n", fileName);

            request->send(SD, fileName, "application/octet-stream");
          } else if (strcmp(fileAction, "delete") == 0) {
            logmessage += " deleted";
            Serial.printf("removing: '%s'\n", fileName);
            SD.remove(fileName);
            request->send(200, "text/plain",
                          "Deleted File: " + String(fileName));
          } else {
            logmessage += " ERROR: invalid action param supplied";
            request->send(400, "text/plain",
                          "ERROR: invalid action param supplied");
          }
          Serial.println(logmessage);
        }
      } else {
        request->send(400, "text/plain",
                      "ERROR: name and action params required");
      }
    } else {
      logmessage += " Auth: Failed";
      Serial.println(logmessage);
      return request->requestAuthentication();
    }
  });
}

void notFound(AsyncWebServerRequest *request) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() +
                      " " + request->url();
  Serial.println(logmessage);
  request->send(404, "text/plain", "Not found");
}

// used by server.on functions to discern whether a user has the correct
// httpapitoken OR is authenticated by username and password
bool checkUserWebAuth(AsyncWebServerRequest *request) {
  bool isAuthenticated = false;

  if (request->authenticate(config.httpuser.c_str(),
                            config.httppassword.c_str())) {
    Serial.println("is authenticated via username and password");
    isAuthenticated = true;
  }
  return isAuthenticated;
}

// handles uploads to the filserver
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index,
                  uint8_t *data, size_t len, bool final) {
  // make sure authenticated before allowing upload
  if (checkUserWebAuth(request)) {
    String logmessage = "Client:" + request->client()->remoteIP().toString() +
                        " " + request->url();
    Serial.println(logmessage);

    if (!index) {
      logmessage = "Upload Start: " + String(filename);
      // open the file on first call and store the file handle in the request
      // object
      request->_tempFile = SD.open("/" + filename, "w");
      Serial.println(logmessage);
    }

    if (len) {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data, len);
      logmessage = "Writing file: " + String(filename) +
                   " index=" + String(index) + " len=" + String(len);
      Serial.println(logmessage);
    }

    if (final) {
      logmessage = "Upload Complete: " + String(filename) +
                   ",size: " + String(index + len);
      // close the file handle as the upload is now done
      request->_tempFile.close();
      Serial.println(logmessage);
      request->redirect("/");
    }
  } else {
    Serial.println("Auth: Failed");
    return request->requestAuthentication();
  }
}
