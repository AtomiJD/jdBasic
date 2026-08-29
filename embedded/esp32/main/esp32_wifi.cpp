// The radio. The S3 can be a station on someone else's network or its
// own access point, and jdBasic reaches both through the same handful of
// verbs the RP2350 build uses, plus the ones only this chip can answer.
//
// It is a mode rather than a state: a started radio costs on the order
// of a hundred kilobytes of internal RAM, which is most of what the
// interpreter has to work with, so WIFI.OFF gives it back.

#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "../../../src/vm.h"

static bool s_stack_up = false;      // netif, event loop and nvs, once
static bool s_wifi_inited = false;   // esp_wifi_init holds the buffers
static bool s_radio_up = false;      // esp_wifi_start has been called
static wifi_mode_t s_mode = WIFI_MODE_NULL;
static esp_netif_t* s_sta = nullptr;
static esp_netif_t* s_ap = nullptr;
static EventGroupHandle_t s_events = nullptr;

#define GOT_IP_BIT   BIT0
#define FAILED_BIT   BIT1

static void on_wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        xEventGroupSetBits(s_events, FAILED_BIT);
}

static void on_ip_event(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_events, GOT_IP_BIT);
}

static bool stack_up() {
    if (s_stack_up) return true;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return false;
    if (esp_netif_init() != ESP_OK) return false;
    if (esp_event_loop_create_default() != ESP_OK) return false;

    s_events = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, nullptr, nullptr);

    s_stack_up = true;
    return true;
}

// esp_wifi_init is where the buffers come from, so it is what OFF has to
// undo. The netif and event machinery above it is one-time and stays.
static bool wifi_inited() {
    if (s_wifi_inited) return true;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    s_wifi_inited = true;
    return true;
}

// Stopping alone gives back a few kilobytes; deinit and dropping the
// interfaces is what returns the rest of the hundred-odd it costs.
static void radio_down() {
    if (s_radio_up) esp_wifi_stop();
    s_radio_up = false;
    if (s_wifi_inited) {
        esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_sta) { esp_netif_destroy_default_wifi(s_sta); s_sta = nullptr; }
    if (s_ap)  { esp_netif_destroy_default_wifi(s_ap);  s_ap  = nullptr; }
    s_mode = WIFI_MODE_NULL;
}

// Joins a network and waits for an address. Returns 0 on success, and a
// negative number for each way it can fail, the way the RP2350 build
// reports it.
static int wifi_join(const char* ssid, const char* pass, int timeout_ms) {
    if (!stack_up()) return -1;
    radio_down();
    if (!wifi_inited()) return -1;
    if (!s_sta) s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return -1;

    wifi_config_t wc = {};
    snprintf((char*)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char*)wc.sta.password, sizeof wc.sta.password, "%s", pass);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return -1;
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return -1;
    if (esp_wifi_start() != ESP_OK) return -1;
    s_radio_up = true;
    s_mode = WIFI_MODE_STA;

    xEventGroupClearBits(s_events, GOT_IP_BIT | FAILED_BIT);
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & GOT_IP_BIT) return 0;
    if (bits & FAILED_BIT) return -3;
    return -2;
}

// Its own network. With no password the access point is open, which is
// what an unattended board on a bench usually wants.
static int wifi_ap(const char* ssid, const char* pass, int channel) {
    if (!stack_up()) return -1;
    radio_down();
    if (!wifi_inited()) return -1;
    if (!s_ap) s_ap = esp_netif_create_default_wifi_ap();
    if (!s_ap) return -1;

    wifi_config_t wc = {};
    snprintf((char*)wc.ap.ssid, sizeof wc.ap.ssid, "%s", ssid);
    wc.ap.ssid_len = (uint8_t)strlen(ssid);
    wc.ap.channel = (uint8_t)channel;
    wc.ap.max_connection = 4;
    if (pass && strlen(pass) >= 8) {
        snprintf((char*)wc.ap.password, sizeof wc.ap.password, "%s", pass);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    }
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) return -1;
    if (esp_wifi_set_config(WIFI_IF_AP, &wc) != ESP_OK) return -1;
    if (esp_wifi_start() != ESP_OK) return -1;
    s_radio_up = true;
    s_mode = WIFI_MODE_AP;
    return 0;
}

// The address of whichever interface is carrying traffic.
static bool current_ip(char* out, size_t cap) {
    esp_netif_t* n = (s_mode == WIFI_MODE_AP) ? s_ap : s_sta;
    if (!s_radio_up || !n) return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(n, &ip) != ESP_OK) return false;
    if (ip.ip.addr == 0) return false;
    snprintf(out, cap, IPSTR, IP2STR(&ip.ip));
    return true;
}

static int ap_clients() {
    if (s_mode != WIFI_MODE_AP || !s_radio_up) return 0;
    wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return 0;
    return list.num;
}

void register_esp32_wifi(VM& vm) {
    vm.register_native("WIFI.CONNECT", 2, 3, [](const std::vector<Value>& args) -> Value {
        int timeout = args.size() >= 3 ? (int)args[2].to_double() : 20000;
        return Value::make_i64(wifi_join(args[0].to_string().c_str(),
                                         args[1].to_string().c_str(), timeout));
    });

    // The same two lines every networked program would otherwise carry:
    // ssid and password from wifi.txt on the flash store.
    vm.register_native("WIFI.AUTO", 0, 0, [](const std::vector<Value>&) -> Value {
        FILE* f = fopen("wifi.txt", "r");
        if (!f) return Value::make_i64(-2);
        char ssid[64] = {0}, pass[80] = {0};
        bool ok = fgets(ssid, sizeof ssid, f) && fgets(pass, sizeof pass, f);
        fclose(f);
        if (!ok) return Value::make_i64(-2);
        for (char* p = ssid; *p; p++) if (*p == '\n' || *p == '\r') { *p = 0; break; }
        for (char* p = pass; *p; p++) if (*p == '\n' || *p == '\r') { *p = 0; break; }
        return Value::make_i64(wifi_join(ssid, pass, 20000));
    });

    vm.register_native("WIFI.AP", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string pass = args.size() >= 2 ? args[1].to_string() : "";
        int channel = args.size() >= 3 ? (int)args[2].to_double() : 1;
        return Value::make_i64(wifi_ap(args[0].to_string().c_str(),
                                       pass.c_str(), channel));
    });

    vm.register_native("WIFI.OFF", 0, 0, [](const std::vector<Value>&) -> Value {
        radio_down();
        return Value::make_none();
    });

    // 0 down, 1 an access point serving, 2 a station with an address.
    vm.register_native("WIFI.STATUS", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!s_radio_up) return Value::make_i64(0);
        if (s_mode == WIFI_MODE_AP) return Value::make_i64(1);
        char ip[20];
        return Value::make_i64(current_ip(ip, sizeof ip) ? 2 : 0);
    });

    vm.register_native("WIFI.IP$", 0, 0, [](const std::vector<Value>&) -> Value {
        char ip[20];
        return Value::make_string(current_ip(ip, sizeof ip) ? ip : "");
    });

    vm.register_native("WIFI.CLIENTS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(ap_clients());
    });

    vm.register_native("WIFI.MAC$", 0, 0, [](const std::vector<Value>&) -> Value {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, s_mode == WIFI_MODE_AP ? ESP_MAC_WIFI_SOFTAP : ESP_MAC_WIFI_STA);
        char buf[20];
        snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return Value::make_string(buf);
    });

    vm.register_native("WIFI.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char ip[20] = "";
        current_ip(ip, sizeof ip);
        char buf[160];
        if (!s_radio_up) {
            snprintf(buf, sizeof buf, "radio off");
        } else if (s_mode == WIFI_MODE_AP) {
            wifi_config_t wc = {};
            esp_wifi_get_config(WIFI_IF_AP, &wc);
            snprintf(buf, sizeof buf, "ap %s ip %s clients %d",
                     (const char*)wc.ap.ssid, ip, ap_clients());
        } else {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
                snprintf(buf, sizeof buf, "station %s ip %s rssi %d ch %d",
                         (const char*)ap.ssid, ip, ap.rssi, ap.primary);
            else
                snprintf(buf, sizeof buf, "station not associated");
        }
        return Value::make_string(buf);
    });
}
