
#include "settings.h"
#include "display.h"
#include "esp_mac.h"
#include "mykeyboard.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "sd_functions.h"
#include <cstdio>
#include <cstdlib>
#include <globals.h>
#include <memory>

namespace {
uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    while (length--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

String makeWifiKey(char prefix, uint32_t crc) {
    char key[11] = {0};
    std::snprintf(key, sizeof(key), "%c_%08X", prefix, crc);
    return String(key);
}

std::unique_ptr<nvs::NVSHandle> openNamespace(const char *ns, nvs_open_mode_t mode, esp_err_t &err) {
    auto handle = nvs::open_nvs_handle(ns, mode, &err);
    if (err != ESP_OK) {
        log_i("openNamespace(%s) failed: %d", ns, err);
        return nullptr;
    }
    return handle;
}

JsonArray ensureWifiListInternal() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return JsonArray();

    JsonArray wifiList = setting["wifi"].as<JsonArray>();
    if (wifiList.isNull()) { wifiList = setting.createNestedArray("wifi"); }
    if (wifiList.isNull()) { log_e("ensureWifiList: failed to create wifi list"); }
    return wifiList;
}
} // namespace

JsonObject ensureSettingsRoot() {
    JsonArray settingsArray = settings.as<JsonArray>();
    if (settingsArray.isNull()) {
        settings.clear();
        settingsArray = settings.to<JsonArray>();
    }
    if (settingsArray.isNull()) {
        log_e("ensureSettingsRoot: unable to prepare settings array");
        return JsonObject();
    }

    JsonObject setting;
    if (settingsArray.size() > 0 && settingsArray[0].is<JsonObject>()) {
        setting = settingsArray[0].as<JsonObject>();
    } else {
        settingsArray.clear();
        setting = settingsArray.add<JsonObject>();
    }

    if (setting.isNull()) { log_e("ensureSettingsRoot: failed to create root object"); }
    return setting;
}

bool getWifiCredential(const String &searchSsid, String &outPwd) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == searchSsid) {
            outPwd = wifiEntry["pwd"].as<String>();
            return true;
        }
    }

    return false;
}

bool setWifiCredential(const String &ssidValue, const String &passwordValue, bool persist) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    JsonObject target;
    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == ssidValue) {
            target = wifiEntry;
            break;
        }
    }
    if (target.isNull()) { target = wifiList.createNestedObject(); }
    if (target.isNull()) {
        log_e("setWifiCredential: failed to allocate entry");
        return false;
    }

    target["ssid"] = ssidValue;
    target["pwd"] = passwordValue;

    if (persist) { saveConfigs(); }
    return true;
}

void settings_menu() {
    options = {
#ifndef E_PAPER_DISPLAY
        {"Charge Mode", [=]() { chargeMode(); }},
#endif
        {"Brightness",
                               [=]() {
             setBrightnessMenu();
             saveConfigs();
         }                                                           },
        {"Dim time",
                               [=]() {
             setdimmerSet();
             saveConfigs();
         }                                                           },
#if !defined(E_PAPER_DISPLAY)
        {"UI Color",    [=]() {
             setUiColor();
             saveConfigs();
         }                 },
#endif
    };
    if (sdcardMounted) {
        if (onlyBins)
            options.push_back({"All Files", [=]() {
                                   gsetOnlyBins(true, false);
                                   saveConfigs();
                               }});
        else
            options.push_back({"Only Bins", [=]() {
                                   gsetOnlyBins(true, true);
                                   saveConfigs();
                               }});
    }

    if (askSpiffs)
        options.push_back({"Avoid Spiffs", [=]() {
                               gsetAskSpiffs(true, false);
                               saveConfigs();
                           }});
    else
        options.push_back({"Ask Spiffs", [=]() {
                               gsetAskSpiffs(true, true);
                               saveConfigs();
                           }});
#if !defined(E_PAPER_DISPLAY) || defined(USE_M5GFX)
    options.push_back({"Orientation", [=]() {
                           gsetRotation(true);
                           saveConfigs();
                       }});
#endif
#if defined(PART_08MB) && defined(M5STACK)
    options.push_back({"Partition Change", [=]() { partitioner(); }});
    options.push_back({"List of Partitions", [=]() { partList(); }});
#endif

#ifndef PART_04MB
    options.push_back({"Clear FAT", [=]() { eraseFAT(); }});
#endif

    if (MAX_SPIFFS > 0)
        options.push_back({"Backup SPIFFS", [=]() { dumpPartition("spiffs", "/bkp/spiffs"); }});
    if (MAX_FAT_sys > 0 && dev_mode)
        options.push_back({"Backup FAT sys", [=]() { dumpPartition("sys", "/bkp/FAT_sys"); }}); // Test only
    if (MAX_FAT_vfs > 0)
        options.push_back({"Backup FAT vfs", [=]() { dumpPartition("vfs", "/bkp/FAT_vfs"); }});
    if (MAX_SPIFFS > 0) options.push_back({"Restore SPIFFS", [=]() { restorePartition("spiffs"); }});
    if (MAX_FAT_sys > 0 && dev_mode)
        options.push_back({"Restore FAT Sys", [=]() { restorePartition("sys"); }}); // Test only
    if (MAX_FAT_vfs > 0) options.push_back({"Restore FAT Vfs", [=]() { restorePartition("vfs"); }});
    if (dev_mode) options.push_back({"Boot Animation", [=]() { initDisplayLoop(); }});
    if (dev_mode) options.push_back({"Deactivate Dev", [=]() { dev_mode = false; }});
    options.push_back({"Restart", [=]() { FREE_TFT ESP.restart(); }});
#if defined(STICK_C_PLUS2) || defined(T_EMBED) || defined(STICK_C_PLUS) || defined(T_LORA_PAGER)
    options.push_back({"Turn-off", [=]() { powerOff(); }});
#endif

    options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
    loopOptions(options);
    tft->drawPixel(0, 0, 0);
    tft->fillScreen(BGCOLOR);
}

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
**  Function: setBrightness
**  save brightness value into EEPROM
**********************************************************************/
void setBrightness(int brightval, bool save) {
    if (brightval > 100) brightval = 100;

#if defined(HEADLESS)
// do Nothing
#else
    _setBrightness(brightval);
#endif

    if (save) { saveIntoNVS(); }
}

/*********************************************************************
**  Function: getBrightness
**  save brightness value into EEPROM
**********************************************************************/
void getBrightness() {
    if (bright > 100) {
        bright = 100;

#if defined(HEADLESS)
// do Nothing
#else
        _setBrightness(bright);
#endif
        setBrightness(100);
    }

#if defined(HEADLESS)
// do Nothing
#else
    _setBrightness(bright);
#endif
}

/*********************************************************************
**  Function: gsetOnlyBins
**  get onlyBins from EEPROM
**********************************************************************/
bool gsetOnlyBins(bool set, bool value) {
    bool result = false;

    if (onlyBins > 1) { set = true; }

    if (onlyBins == 0) result = false;
    else result = true;

    if (set) {
        result = value;
        onlyBins = value; // update the global variable
    }
    return result;
}

/*********************************************************************
**  Function: gsetAskSpiffs
**  get onlyBins from EEPROM
**********************************************************************/
bool gsetAskSpiffs(bool set, bool value) {
    bool result = false;

    if (askSpiffs > 1) { set = true; }

    if (askSpiffs == 0) result = false;
    else result = true;

    if (set) {
        result = value;
        askSpiffs = value; // update the global variable
    }
    return result;
}

/*********************************************************************
**  Function: gsetRotation
**  get onlyBins from EEPROM
**********************************************************************/
#if ROTATION == 0
#define DRV 0
#else
#define DRV 1
#endif
int gsetRotation(bool set) {
    int result = ROTATION;

    if (rotation > 3) {
        set = true;
        result = ROTATION;
    } else result = rotation;

    if (set) {
        options = {
            {"Default",                                              [&]() { result = ROTATION; }          },
#if TFT_WIDTH >= 200 && TFT_HEIGHT >= 200
            {String("Portrait " + String(DRV == 1 ? 0 : 1)).c_str(), [&]() { result = (DRV == 1 ? 0 : 1); }},
#endif
            {String("Landscape " + String(DRV)).c_str(),             [&]() { result = DRV; }               },
#if TFT_WIDTH >= 200 && TFT_HEIGHT >= 200
            {String("Portrait " + String(DRV == 1 ? 2 : 3)).c_str(), [&]() { result = (DRV == 1 ? 2 : 3); }},
#endif
            {String("Landscape " + String(DRV + 2)).c_str(),         [&]() { result = DRV + 2; }           }
        };
        loopOptions(options);
        rotation = result;

        if (rotation & 0b1) {
#if defined(HAS_TOUCH)
            tftHeight = TFT_WIDTH - (FM * LH + 4);
            ;
#else
            tftHeight = TFT_WIDTH;
#endif
            tftWidth = TFT_HEIGHT;
        } else {
#if defined(HAS_TOUCH)
            tftHeight = TFT_HEIGHT - (FM * LH + 4);
#else
            tftHeight = TFT_HEIGHT;
#endif
            tftWidth = TFT_WIDTH;
        }

        tft->setRotation(result);
        tft->fillScreen(BGCOLOR);
    }
    return result;
}
/*********************************************************************
**  Function: setBrightnessMenu
**  Handles Menu to set brightness
**********************************************************************/
void setBrightnessMenu() {
    options = {
        {"100%", [=]() { setBrightness(100); }},
        {"75 %", [=]() { setBrightness(75); } },
        {"50 %", [=]() { setBrightness(50); } },
        {"25 %", [=]() { setBrightness(25); } },
        {" 0 %", [=]() { setBrightness(1); }  },
    };
    loopOptions(options, true);
}
/*********************************************************************
**  Function: setUiColor
**  Change Ui Color scheme
**********************************************************************/
void setUiColor() {
    options = {
        {"Default",
         [&]() {
             FGCOLOR = 0x07E0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0x30c5;
             even_color = 0x32e5;
         }                 },
        {"Red",
         [&]() {
             FGCOLOR = 0xF800;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xE3E0;
             odd_color = 0xFBC0;
             even_color = 0xAAC0;
         }                 },
        {"Blue",
         [&]() {
             FGCOLOR = 0x94BF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xd81f;
             odd_color = 0xd69f;
             even_color = 0x079F;
         }                 },
        {"Yellow",
         [&]() {
             FGCOLOR = 0xFFE0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xFB80;
             odd_color = 0x9480;
             even_color = 0xbae0;
         }                 },
        {"Purple",
         [&]() {
             FGCOLOR = 0xe01f;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0xf57f;
             even_color = 0x89d3;
         }                 },
        {"White",
         [&]() {
             FGCOLOR = 0xFFFF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0x6b6d;
             odd_color = 0x630C;
             even_color = 0x8410;
         }                 },
        {"Black",   [&]() {
             FGCOLOR = 0x0000;
             BGCOLOR = 0xFFFF;
             ALCOLOR = 0x6b6d;
             odd_color = 0x8c71;
             even_color = 0xb596;
         }},
    };
    loopOptions(options);
    displayRedStripe("Saving...");
}
/*********************************************************************
**  Function: setdimmerSet
**  set dimmerSet time
**********************************************************************/
void setdimmerSet() {
    int time = 20;
    options = {
        {"10s",     [&]() { time = 10; }},
        {"15s",     [&]() { time = 15; }},
        {"30s",     [&]() { time = 30; }},
        {"45s",     [&]() { time = 45; }},
        {"60s",     [&]() { time = 60; }},
        {"Disable", [&]() { time = 0; } },
    };

    loopOptions(options);
    dimmerSet = time;
}

/*********************************************************************
**  Function: chargeMode
**  Enter in Charging mode
**********************************************************************/
void chargeMode() {
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
#endif
    setBrightness(5, false);
    vTaskDelay(pdTICKS_TO_MS(500));
    tft->fillScreen(BGCOLOR);
    unsigned long tmp = 0;
    while (!check(SelPress)) {
        if (millis() - tmp > 5000) {
            displayRedStripe(String(getBattery()) + " %");
            tmp = millis();
        }
    }
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
    setBrightness(bright, false);
}
String get_efuse_mac_as_string() {
    uint8_t mac[6] = {0};
    String str = "";
    esp_efuse_mac_get_default(mac);
    for (int i = 0; i < 6; i++) {
        if (i > 0) str += ":";
        str += String(mac[i], 16);
    }
    return str;
}
bool config_exists() {
    if (!SDM.exists(CONFIG_FILE)) {
        File conf = SDM.open(CONFIG_FILE, FILE_WRITE, true);
        ;
        if (conf) {
            conf.printf(
                "[{\"%s\":%d,\"dimmerSet\":10,\"onlyBins\":1,\"bright\":100,\"askSpiffs\":1,\"wui_usr\":"
                "\"admin\",\"wui_pwd\":\"launcher\",\"dwn_path\":\"/downloads/\",\"hub_url\":\"https://einkhub.com\""
                ",\"FGCOLOR\":2016,\"BGCOLOR\":0,\"ALCOLOR\":63488,\"even\":13029,\"odd\":12485,\"dev\":"
                "0,\"wifi\":[{\"ssid\":\"myNetSSID\",\"pwd\":\"myNetPassword\"}],\"favorite\":[]}]",
                get_efuse_mac_as_string().c_str(),
                ROTATION
            );
        }
        conf.close();
        vTaskDelay(pdTICKS_TO_MS(50));
        log_i("config_exists: config.conf created with default");
        return false;
    } else {
        log_i("config_exists: config.conf exists");
        return true;
    }
}

bool saveIntoNVS() {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    err |= nvsHandle->set_item("dimtime", dimmerSet);
    err |= nvsHandle->set_item("bright", bright);
    err |= nvsHandle->set_item("onlyBins", onlyBins);
    err |= nvsHandle->set_item("askSpiffs", askSpiffs);
    err |= nvsHandle->set_item("rotation", rotation);
    err |= nvsHandle->set_item("FGCOLOR", FGCOLOR);
    err |= nvsHandle->set_item("BGCOLOR", BGCOLOR);
    err |= nvsHandle->set_item("ALCOLOR", ALCOLOR);
    err |= nvsHandle->set_item("odd_color", odd_color);
    err |= nvsHandle->set_item("even_color", even_color);
    err |= nvsHandle->set_item("dev_mode", dev_mode);
    err |= nvsHandle->set_string("wui_usr", wui_usr.c_str());
    err |= nvsHandle->set_string("wui_pwd", wui_pwd.c_str());
    err |= nvsHandle->set_string("dwn_path", dwn_path.c_str());
    err |= nvsHandle->set_string("hub_url", hub_url.c_str());
#if defined(HEADLESS)
    // SD Pins
    err |= nvsHandle->set_item("miso", _miso);
    err |= nvsHandle->set_item("mosi", _mosi);
    err |= nvsHandle->set_item("sck", _sck);
    err |= nvsHandle->set_item("cs", _cs);
#endif
    if (err != ESP_OK) {
        log_i("Failed to store settings in NVS: %d", err);
    } else {
        log_i("Settings stored in NVS successfully");
    }

    nvsHandle->commit();
    if (!saveWifiIntoNVS()) { log_i("saveIntoNVS: failed to store WiFi list"); }
    return true;
}

bool saveSessionToken(const String &token) {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    if (token.isEmpty()) {
        err = nvsHandle->erase_item("token");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvsHandle->set_string("token", token.c_str());
    }

    if (err == ESP_OK) { err = nvsHandle->commit(); }
    return err == ESP_OK;
}

bool saveWifiIntoNVS() {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("l_wifi", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    err = nvsHandle->erase_all();
    if (err != ESP_OK) { log_i("saveWifiIntoNVS: failed to clear WiFi namespace: %d", err); }

    for (JsonObject wifiObj : wifiList) {
        String ssid = wifiObj["ssid"].as<String>();
        if (ssid.isEmpty()) continue;
        String pwd = wifiObj["pwd"].as<String>();
        uint32_t crc = crc32(reinterpret_cast<const uint8_t *>(ssid.c_str()), ssid.length());
        String ssidKey = makeWifiKey('s', crc);
        String pwdKey = makeWifiKey('p', crc);

        esp_err_t ssidErr = nvsHandle->set_string(ssidKey.c_str(), ssid.c_str());
        esp_err_t pwdErr = nvsHandle->set_string(pwdKey.c_str(), pwd.c_str());
        if (ssidErr != ESP_OK || pwdErr != ESP_OK) {
            log_i(
                "saveWifiIntoNVS: failed storing %s (ssid err=%d pwd err=%d)", ssid.c_str(), ssidErr, pwdErr
            );
        }
    }

    nvsHandle->commit();
    return true;
}

String loadSessionToken() {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READONLY, err);
    if (!nvsHandle) return "";

    char buffer[65] = {0};
    err = nvsHandle->get_string("token", buffer, sizeof(buffer));
    if (err != ESP_OK) return "";

    return String(buffer);
}

void defaultValues() {
    // rotation = ROTATION;
    dimmerSet = 20;
    bright = 100;
    onlyBins = true;
    askSpiffs = true;
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    FGCOLOR = 0x0000;
    BGCOLOR = 0xFFFF;
    ALCOLOR = 0x8888;
    odd_color = 0x5555;
    even_color = 0x2222;
#else
    FGCOLOR = 0x07E0;
    BGCOLOR = 0x0000;
    ALCOLOR = 0xF800;
    odd_color = 0x30c5;
    even_color = 0x32e5;
#endif
    dev_mode = false;
    wui_usr = "admin";
    wui_pwd = "launcher";
    dwn_path = "/downloads/";
    hub_url = "https://einkhub.com";
#if defined(HEADLESS)
    // SD Pins
    _miso = 0;
    _mosi = 0;
    _sck = 0;
    _cs = 0;
#endif
    saveIntoNVS();
}
bool getFromNVS() {
    esp_err_t err;
    std::unique_ptr<nvs::NVSHandle> nvsHandle = nvs::open_nvs_handle("launcher", NVS_READONLY, &err);
    if (err != ESP_OK) {
        // If NVS read fails, set default values
        log_i("Failed to retrieve settings from NVS: %d\nUsing Default values", err);
        defaultValues();
        return false;
    }
    // Get settings from NVS
    if (err != ESP_OK) {
        log_i("Failed to open NVS handle: %d", err);
        return false;
    }
    err = nvsHandle->get_item("dimtime", dimmerSet);
    err |= nvsHandle->get_item("bright", bright);
    err |= nvsHandle->get_item("onlyBins", onlyBins);
    err |= nvsHandle->get_item("askSpiffs", askSpiffs);
    err |= nvsHandle->get_item("rotation", rotation);
    err |= nvsHandle->get_item("FGCOLOR", FGCOLOR);
    err |= nvsHandle->get_item("BGCOLOR", BGCOLOR);
    err |= nvsHandle->get_item("ALCOLOR", ALCOLOR);
    err |= nvsHandle->get_item("odd_color", odd_color);
    err |= nvsHandle->get_item("even_color", even_color);
    err |= nvsHandle->get_item("dev_mode", dev_mode);
#if defined(HEADLESS)
    // SD Pins
    err |= nvsHandle->get_item("miso", _miso);
    err |= nvsHandle->get_item("mosi", _mosi);
    err |= nvsHandle->get_item("sck", _sck);
    err |= nvsHandle->get_item("cs", _cs);
#endif
    char buffer[64];
    err |= nvsHandle->get_string("wui_usr", buffer, sizeof(buffer));
    wui_usr = String(buffer);
    err |= nvsHandle->get_string("wui_pwd", buffer, sizeof(buffer));
    wui_pwd = String(buffer);
    err |= nvsHandle->get_string("dwn_path", buffer, sizeof(buffer));
    dwn_path = String(buffer);
    if (nvsHandle->get_string("hub_url", buffer, sizeof(buffer)) == ESP_OK) {
        hub_url = String(buffer);
    }
    if (err != ESP_OK) {
        log_i("Failed to retrieve settings from NVS: %d\nUsing Default values", err);
        defaultValues();
        return false;
    }

    return true;
}
bool getWifiFromNVS() {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;
    wifiList.clear();

    Serial.println("NVS: Finding keys in NVS...");
    nvs_handle_t rawHandle;
    esp_err_t err = nvs_open("l_wifi", NVS_READONLY, &rawHandle);
    if (err != ESP_OK) {
        Serial.printf("Error opening l_wifi: %s\n", esp_err_to_name(err));
        return false;
    }

    nvs_iterator_t it = nullptr;
    err = nvs_entry_find("nvs", "l_wifi", NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(rawHandle);
        return true;
    }
    if (err != ESP_OK) {
        Serial.printf("Error finding l_wifi entry, error: %s\n", esp_err_to_name(err));
        nvs_close(rawHandle);
        return false;
    }

    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        String key = String(info.key);
        if (key.startsWith("s_")) {
            size_t ssid_size = 0;
            err = nvs_get_str(rawHandle, info.key, NULL, &ssid_size);
            if (err != ESP_OK) {
                Serial.printf("Error %d looking for %s\n", err, info.key);
                break;
            }

            char *ssidBuff = static_cast<char *>(malloc(ssid_size));
            if (!ssidBuff) {
                Serial.println("Failed to allocate buffer for SSID");
                break;
            }

            err = nvs_get_str(rawHandle, info.key, ssidBuff, &ssid_size);
            if (err == ESP_OK) {
                String suffix = key.substring(2);
                String pwdKey = "p_" + suffix;
                size_t pwd_size = 0;
                String pwdValue;
                if (nvs_get_str(rawHandle, pwdKey.c_str(), NULL, &pwd_size) == ESP_OK) {
                    char *pwdBuff = static_cast<char *>(malloc(pwd_size));
                    if (pwdBuff) {
                        if (nvs_get_str(rawHandle, pwdKey.c_str(), pwdBuff, &pwd_size) == ESP_OK) {
                            pwdValue = String(pwdBuff);
                        } else {
                            Serial.printf("Error retrieving pwd for key %s\n", pwdKey.c_str());
                        }
                        free(pwdBuff);
                    } else {
                        Serial.println("Failed to allocate buffer for password");
                    }
                } else {
                    Serial.printf("Password key %s not found\n", pwdKey.c_str());
                }

                setWifiCredential(String(ssidBuff), pwdValue, false);
                Serial.printf("SSID: %s, PWD: %s\n", ssidBuff, pwdValue.c_str());
            } else {
                Serial.printf("Error %d retrieving %s\n", err, info.key);
            }
            free(ssidBuff);
        }

        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(rawHandle);
    return true;
}

/*********************************************************************
**  Function: getConfigs
**  getConfigurations from EEPROM or JSON
**********************************************************************/
void getConfigs() {
    if (setupSdCard()) {
        // check if config file exists, otherwise create it with default values
        config_exists();
        File file = SDM.open(CONFIG_FILE, FILE_READ);
        if (file) {
            DeserializationError error = deserializeJson(settings, file);
            if (error) {
                log_i("Failed to read file, using default configuration");
                goto Default;
            } else {
                log_i("getConfigs: deserialized correctly");
            }

            int count = 0;
            JsonObject setting = settings[0];
            if (setting["onlyBins"].is<bool>()) {
                onlyBins = gsetOnlyBins(setting["onlyBins"].as<bool>());
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["askSpiffs"].is<bool>()) {
                askSpiffs = gsetAskSpiffs(setting["askSpiffs"].as<bool>());
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["bright"].is<int>()) {
                bright = setting["bright"].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["dimmerSet"].is<int>()) {
                dimmerSet = setting["dimmerSet"].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
            char *mac;
            if (setting[get_efuse_mac_as_string()].is<int>()) {
                rotation = setting[get_efuse_mac_as_string()].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
#ifndef E_PAPER_DISPLAY
            if (setting["FGCOLOR"].is<uint16_t>()) {
                FGCOLOR = setting["FGCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["BGCOLOR"].is<uint16_t>()) {
                BGCOLOR = setting["BGCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["ALCOLOR"].is<uint16_t>()) {
                ALCOLOR = setting["ALCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["odd"].is<uint16_t>()) {
                odd_color = setting["odd"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["even"].is<uint16_t>()) {
                even_color = setting["even"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
#endif
            if (setting["dev"].is<bool>()) {
                dev_mode = setting["dev"].as<bool>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["wui_usr"].is<String>()) {
                wui_usr = setting["wui_usr"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["wui_pwd"].is<String>()) {
                wui_pwd = setting["wui_pwd"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["dwn_path"].is<String>()) {
                dwn_path = setting["dwn_path"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["hub_url"].is<String>()) {
                hub_url = setting["hub_url"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (!setting["wifi"].is<JsonArray>()) {
                ++count;
                log_i("Fail");
            }
            if (setting["favorite"].is<JsonArray>()) {
                favorite = setting["favorite"].as<JsonArray>();
            } else {
                ++count;
                log_i("Fail");
            }
            if (count > 0) saveConfigs();

            log_i("Brightness: %d", bright);
            setBrightness(bright);
            if (dimmerSet > 120) dimmerSet = 10;

            file.close();
            saveIntoNVS();
            log_i("Using config.conf setup file");
        } else {
        Default:
            file.close();
            saveConfigs();
            log_i("Using settings stored on EEPROM");
        }
    } else {
        getFromNVS();
        getWifiFromNVS();
    }
}
/*********************************************************************
**  Function: saveConfigs
**  save configs into JSON config.conf file
**********************************************************************/
void saveConfigs() {
    bool retry = true;

    while (true) {
        if (!setupSdCard()) break;

        if (SDM.remove(CONFIG_FILE)) log_i("config.conf deleted");
        else log_i("fail deleting config.conf");

        JsonArray settingsArray = settings.as<JsonArray>();
        if (settingsArray.isNull()) {
            settings.clear();
            settingsArray = settings.to<JsonArray>();
        }
        if (settingsArray.isNull()) {
            log_e("saveConfigs: failed to prepare settings array");
            break;
        }

        JsonObject setting;
        if (settingsArray.size() > 0 && settingsArray[0].is<JsonObject>()) {
            setting = settingsArray[0];
        } else {
            settingsArray.clear();
            setting = settingsArray.add<JsonObject>();
        }
        if (setting.isNull()) { setting = settingsArray.add<JsonObject>(); }
        if (setting.isNull()) {
            log_e("saveConfigs: failed to create root object");
            break;
        }
        favorite = setting["favorite"].as<JsonArray>();
        if (favorite.isNull()) favorite = setting.createNestedArray("favorite");

        JsonArray wifiList = setting["wifi"].as<JsonArray>();
        if (wifiList.isNull()) { wifiList = setting.createNestedArray("wifi"); }
        if (wifiList.isNull()) {
            log_e("saveConfigs: failed to create wifi array");
            break;
        }
        if (wifiList.size() == 0) {
            JsonObject wifiObj = wifiList.add<JsonObject>();
            if (wifiObj.isNull()) { wifiObj = wifiList.add<JsonObject>(); }
            if (!wifiObj.isNull()) {
                wifiObj["ssid"] = ssid.length() == 0 ? "myNetSSID" : ssid;
                wifiObj["pwd"] = pwd.length() == 0 ? "myNetPassword" : pwd;
            } else {
                log_e("saveConfigs: failed to allocate default wifi entry");
            }
        }
        // Update JSON document with current configuration
        setting["onlyBins"] = onlyBins;
        setting["askSpiffs"] = askSpiffs;
        setting["bright"] = bright;
        setting["dimmerSet"] = dimmerSet;
        setting[get_efuse_mac_as_string()] = rotation;
        setting["FGCOLOR"] = FGCOLOR;
        setting["BGCOLOR"] = BGCOLOR;
        setting["ALCOLOR"] = ALCOLOR;
        setting["odd"] = odd_color;
        setting["even"] = even_color;
        setting["dev"] = dev_mode;
        setting["wui_usr"] = wui_usr;
        setting["wui_pwd"] = wui_pwd;
        setting["dwn_path"] = dwn_path;
        setting["hub_url"] = hub_url;

        File file = SDM.open(CONFIG_FILE, FILE_WRITE, true);
        if (!file) {
            log_i("Failed to create file");
            break;
        }
        log_i("config.conf created");

        size_t written = serializeJsonPretty(settings, file);
        file.flush();
        file.close();

        if (written < 5) {
            if (retry) {
                log_i("Failed to write to file");
                SDM.remove(CONFIG_FILE);
                log_i("Creating default file");
                config_exists();
                File defaultFile = SDM.open(CONFIG_FILE, FILE_READ);
                if (defaultFile) {
                    DeserializationError err = deserializeJson(settings, defaultFile);
                    if (err) {
                        log_i("Failed to deserialize default config: %s", err.c_str());
                        settings.clear();
                    }
                    defaultFile.close();
                } else {
                    log_i("Failed to reopen config.conf for recovery");
                }
                retry = false;
                continue;
            }
            log_i("Create new file and Rewriting didn't work");
        } else {
            log_i("config.conf written successfully");
        }

        break;
    }
    saveIntoNVS();
    saveWifiIntoNVS();
}
