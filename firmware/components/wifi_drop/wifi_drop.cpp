#include "wifi_drop.h"
#include <cstring>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string>
#include <cstdio>
#include <dirent.h>
#include <strings.h>     // strcasecmp
#include <cerrno>
#include <cctype>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rsvp/uploadname.hpp"

static const char* TAG = "wifi_drop";
#define AP_SSID "RSVP-Reader"
#define AP_PASS "rsvpdrop8"

static httpd_handle_t s_httpd = nullptr;
static esp_netif_t*   s_ap_netif = nullptr;
static bool           s_sys_inited = false;

static esp_err_t root_get(httpd_req_t* req) {
    static const char page[] = R"PAGE(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>RSVP Reader</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:#0b0d10;color:#e6e9ef;margin:0;padding:18px;
display:flex;justify-content:center}
#card{width:100%;max-width:430px;background:#12151a;border-radius:14px;padding:20px 16px}
h1{font-size:17px;font-weight:600;margin:0 0 14px}h1 span{font-weight:400;color:#8a93a3}
#dz{border:2px dashed #ff3b3b;border-radius:12px;padding:26px 12px;text-align:center;
background:rgba(255,59,59,.07);cursor:pointer;margin-bottom:16px}
#dz.over{background:rgba(255,59,59,.18)}
#dz .arrow{font-size:26px;color:#ff3b3b;margin-bottom:6px}
#dz .hint{font-size:11px;color:#8a93a3;margin-top:4px}
#st{font-size:11px;color:#8a93a3;margin:0 0 5px;min-height:14px}
#bar{height:5px;background:#1d222a;border-radius:3px;margin-bottom:16px;display:none}
#fill{height:5px;width:0;background:#ff3b3b;border-radius:3px}
#hdr{font-size:10px;color:#8a93a3;letter-spacing:1.2px;margin-bottom:4px}
.b{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:9px 0;
border-bottom:1px solid #1d222a;font-size:13px}
.b:last-child{border-bottom:none}
.b span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.b button{flex:none;background:none;border:1px solid #4a2a2e;border-radius:6px;color:#ff6b6b;
font-size:9px;letter-spacing:.5px;padding:3px 9px;cursor:pointer}
</style></head><body><div id=card>
<h1>RSVP Reader <span>/ drop</span></h1>
<div id=dz><div class=arrow>&#11015;</div>Tap or drop a book here<div class=hint>.epub or .txt</div></div>
<input type=file id=f accept=".epub,.txt" style="display:none">
<p id=st></p><div id=bar><div id=fill></div></div>
<div id=hdr>ON THE CARD</div><div id=lst></div>
</div><script>
var dz=document.getElementById('dz'),fi=document.getElementById('f'),
st=document.getElementById('st'),bar=document.getElementById('bar'),
fill=document.getElementById('fill');
function load(){fetch('/list').then(r=>r.text()).then(t=>{
var names=t.split('\n').filter(x=>x),d=document.getElementById('lst');d.innerHTML='';
document.getElementById('hdr').textContent='ON THE CARD - '+(names.length==1?'1 BOOK':names.length+' BOOKS');
names.forEach(function(n){var e=document.createElement('div');e.className='b';
e.innerHTML='<span></span><button>DELETE</button>';e.children[0].textContent=n;
e.children[1].onclick=function(){if(confirm('Delete '+n+'?'))
fetch('/delete?name='+encodeURIComponent(n),{method:'POST'}).then(load);};d.appendChild(e);});});}
function up(f){if(!f)return;
bar.style.display='block';fill.style.width='0';
st.textContent='Uploading "'+f.name+'"...';
var x=new XMLHttpRequest();x.open('POST','/upload?name='+encodeURIComponent(f.name));
x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(100*e.loaded/e.total);
fill.style.width=p+'%';st.textContent='Uploading "'+f.name+'"... '+p+'%';}};
x.onload=function(){st.textContent=x.status==200?'Done.':'Failed ('+x.status+').';
bar.style.display='none';load();};
x.onerror=function(){st.textContent='Upload error.';bar.style.display='none';};x.send(f);}
dz.onclick=function(){fi.click();};
fi.onchange=function(){up(this.files[0]);this.value='';};
dz.ondragover=function(e){e.preventDefault();dz.classList.add('over');};
dz.ondragleave=function(){dz.classList.remove('over');};
dz.ondrop=function(e){e.preventDefault();dz.classList.remove('over');up(e.dataTransfer.files[0]);};
load();
</script></body></html>)PAGE";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static void url_decode(char* s) {   // in-place: %XX -> byte, '+' -> space
    auto hex = [](char c) { c = (char)tolower((unsigned char)c); return c <= '9' ? c - '0' : c - 'a' + 10; };
    char* o = s;
    for (char* p = s; *p; ) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            *o++ = (char)(hex(p[1]) * 16 + hex(p[2])); p += 3;
        } else if (*p == '+') { *o++ = ' '; p++; }
        else { *o++ = *p++; }
    }
    *o = 0;
}

static bool name_from_query(httpd_req_t* req, char* out, size_t n) {
    char q[512];
    out[0] = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof q) != ESP_OK) return false;
    if (httpd_query_key_value(q, "name", out, n) != ESP_OK) return false;
    url_decode(out);
    return true;
}

static esp_err_t list_get(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/plain");
    DIR* d = opendir("/sdcard");
    if (d) {
        struct dirent* e;
        char line[300];
        while ((e = readdir(d)) != nullptr) {
            const char* nm = e->d_name;
            if (nm[0] == '.') continue;
            size_t L = std::strlen(nm);
            bool ok = (L > 5 && strcasecmp(nm + L - 5, ".epub") == 0) ||
                      (L > 4 && strcasecmp(nm + L - 4, ".txt") == 0);
            if (!ok) continue;
            int m = std::snprintf(line, sizeof line, "%s\n", nm);
            httpd_resp_send_chunk(req, line, m);
        }
        closedir(d);
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t upload_post(httpd_req_t* req) {
    char name[256];
    name_from_query(req, name, sizeof name);
    std::string safe = rsvp::sanitizeUploadName(name);
    if (safe.empty()) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name"); return ESP_FAIL; }
    ESP_LOGI(TAG, "upload '%s' (%d bytes)", safe.c_str(), (int)req->content_len);
    std::string path = "/sdcard/" + safe;
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) { ESP_LOGW(TAG, "fopen('%s') failed errno=%d", path.c_str(), errno);
               httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed"); return ESP_FAIL; }
    char buf[2048];
    int remaining = req->content_len;
    bool err = false;
    while (remaining > 0) {
        int want = remaining < (int)sizeof buf ? remaining : (int)sizeof buf;
        int r = httpd_req_recv(req, buf, want);
        if (r <= 0) { ESP_LOGW(TAG, "recv r=%d remaining=%d", r, remaining); err = true; break; }
        // SD writes share scarce internal DMA RAM with WiFi; a write can transiently fail
        // under a WiFi burst. Retry briefly before failing the whole upload.
        size_t off = 0;
        int tries = 0;
        while (off < (size_t)r) {
            size_t w = std::fwrite(buf + off, 1, (size_t)r - off, fp);
            off += w;
            if (w == 0) {
                if (++tries > 3) break;
                ESP_LOGW(TAG, "fwrite errno=%d, retry %d", errno, tries);
                clearerr(fp);
                vTaskDelay(pdMS_TO_TICKS(50));     // let WiFi drain its buffers
            } else {
                tries = 0;
            }
        }
        if (off < (size_t)r) { ESP_LOGW(TAG, "fwrite gave up errno=%d", errno); err = true; break; }
        remaining -= r;
    }
    std::fclose(fp);
    if (err) { std::remove(path.c_str()); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed"); return ESP_FAIL; }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t delete_post(httpd_req_t* req) {
    char name[256];
    name_from_query(req, name, sizeof name);
    std::string safe = rsvp::sanitizeUploadName(name, /*forCreate=*/false);   // must match on-disk name
    if (safe.empty()) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name"); return ESP_FAIL; }
    std::string path = "/sdcard/" + safe;
    std::remove(path.c_str());
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static void start_httpd() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;     // evict stale sockets (a reconnecting phone's dead
                                     // connections otherwise exhaust the pool -> page hangs)
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) { s_httpd = nullptr; return; }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_uri_t list = { .uri = "/list", .method = HTTP_GET, .handler = list_get, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &list);
    httpd_uri_t up = { .uri = "/upload", .method = HTTP_POST, .handler = upload_post, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &up);
    httpd_uri_t del = { .uri = "/delete", .method = HTTP_POST, .handler = delete_post, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &del);
}

void wifi_drop_start(wifi_drop_info_t* out) {
    if (out) std::memset(out, 0, sizeof(*out));
    if (!s_sys_inited) { esp_netif_init(); esp_event_loop_create_default(); s_sys_inited = true; }
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&icfg);
    if (e != ESP_OK) { ESP_LOGW(TAG, "wifi_init failed: %s", esp_err_to_name(e)); return; }
    wifi_config_t wcfg = {};
    std::strncpy((char*)wcfg.ap.ssid, AP_SSID, sizeof(wcfg.ap.ssid));
    wcfg.ap.ssid_len = std::strlen(AP_SSID);
    std::strncpy((char*)wcfg.ap.password, AP_PASS, sizeof(wcfg.ap.password));
    wcfg.ap.channel = 1;
    wcfg.ap.max_connection = 2;
    wcfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_AP);
    e = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
    if (e != ESP_OK) ESP_LOGW(TAG, "set_config: %s", esp_err_to_name(e));
    e = esp_wifi_start();
    if (e != ESP_OK) { ESP_LOGW(TAG, "wifi_start failed: %s", esp_err_to_name(e)); return; }
    start_httpd();
    esp_netif_ip_info_t ipinfo = {};
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ipinfo) == ESP_OK)
        ESP_LOGI(TAG, "httpd=%p AP IP=" IPSTR, (void*)s_httpd, IP2STR(&ipinfo.ip));
    if (out) {
        std::strncpy(out->ssid, AP_SSID, sizeof(out->ssid) - 1);
        std::strncpy(out->pass, AP_PASS, sizeof(out->pass) - 1);
        std::strncpy(out->url, "http://192.168.4.1", sizeof(out->url) - 1);
        out->ok = (s_httpd != nullptr);
    }
    ESP_LOGI(TAG, "AP up: %s", AP_SSID);
}

void wifi_drop_stop(void) {
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = nullptr; }
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap_netif) { esp_netif_destroy_default_wifi(s_ap_netif); s_ap_netif = nullptr; }
}
