/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "ethernet_app.hpp"
#include "crystal_ethernet.hpp"
#include "crystal_network_policy.hpp"
#include "lwip/ip4_addr.h"
#include <cstdio>
#include <cstring>
namespace {
void tick(lv_timer_t *timer) { static_cast<EthernetInfoApp *>(timer->user_data)->refresh(); }
}
EthernetInfoApp::EthernetInfoApp() : CrystalApp("Ethernet", &crystal_ethernet_icon) {}
bool EthernetInfoApp::onCreate()
{
    const auto area = getVisualArea();
    root_ = lv_obj_create(lv_scr_act());
    if (!root_) return false;
    if (!trackObject(root_)) { lv_obj_del(root_); root_ = nullptr; return false; }
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x11151b), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 24, 0);
    lv_obj_set_style_pad_row(root_, 8, 0);
    lv_obj_set_scroll_dir(root_, LV_DIR_VER);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    auto *title = lv_label_create(root_);
    lv_label_set_text(title, "Ethernet");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf2f4f7), 0);
    const char *names[] = {"Status", "Active Connection", "IP Address", "Subnet Mask", "Gateway", "DNS Server", "Ethernet MAC", "Link Speed"};
    for (size_t i = 0; i < 8; ++i) {
        auto *row = lv_obj_create(root_);
        lv_obj_set_size(row, LV_PCT(100), 76);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1b2028), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 24, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        auto *name = lv_label_create(row);
        lv_label_set_text(name, names[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(0xf2f4f7), 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 10);
        auto *value = values_[i] = lv_label_create(row);
        lv_obj_set_size(value, LV_PCT(100), 25);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(0x91a0b3), 0);
        lv_obj_align(value, LV_ALIGN_TOP_LEFT, 0, 40);
    }
    refresh();
    timer_ = lv_timer_create(tick, 1000, this);
    return timer_ != nullptr;
}
bool EthernetInfoApp::onPause()
{
    if (timer_) lv_timer_del(timer_);
    timer_ = nullptr;
    return true;
}
bool EthernetInfoApp::onResume()
{
    refresh();
    if (!timer_) timer_ = lv_timer_create(tick, 1000, this);
    return timer_ != nullptr;
}
bool EthernetInfoApp::onDestroy()
{
    onPause();
    root_ = nullptr;
    for (auto &value : values_) value = nullptr;
    return true;
}
void EthernetInfoApp::refresh()
{
    if (!root_) return;
    const auto info = crystal_ethernet_info();
    const auto set = [this](size_t index, const char *text) {
        if (values_[index] && strcmp(lv_label_get_text(values_[index]), text) != 0)
            lv_label_set_text(values_[index], text);
    };
    set(0, !info.supported ? "Not available on this board" : info.error != ESP_OK ? "Unavailable - WiFi still works" :
        !info.initialized ? "Starting..." : !info.started ? "Stopped" : !info.link ? "Cable disconnected" :
        info.has_ip ? "Connected" : "Getting an IP address...");
    set(1, crystal_network_type_name(crystal_network_active_type()));
    const uint32_t addresses[] = {info.ip, info.mask, info.gateway, info.dns};
    char text[64];
    for (size_t i = 0; i < 4; ++i) {
        if (info.has_ip && addresses[i]) ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&addresses[i]), text, sizeof(text));
        else snprintf(text, sizeof(text), "Not assigned");
        set(i + 2, text);
    }
    if (info.initialized) snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", info.mac[0], info.mac[1], info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
    else snprintf(text, sizeof(text), "Unavailable");
    set(6, text);
    if (info.link && info.speed_mbps) snprintf(text, sizeof(text), "%u Mbps - %s duplex", info.speed_mbps, info.full_duplex ? "Full" : "Half");
    else snprintf(text, sizeof(text), "No link");
    set(7, text);
}
