/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */

#include "crystal_declarative.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <limits>
#include <sstream>

#include "cJSON.h"
#include "crystal_core.hpp"
#include "crystal_hal.hpp"
#include "crystal_network.h"
#include "crystal_package.hpp"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "crystal_declarative";
constexpr unsigned kMaxDocumentBytes = 32768;
constexpr unsigned kMaxNodes = 96;
constexpr unsigned kMaxDepth = 8;
constexpr unsigned kMaxJsonDepth = 32;
constexpr unsigned kMaxBindings = 64;
constexpr unsigned kMaxActions = 64;
constexpr size_t kMaxImageSourceBytes = 256 * 1024;
constexpr size_t kMaxTotalImageBytes = 128 * 1024;
constexpr uint16_t kMaxImageDimension = 480;

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
const cJSON *field(const cJSON *object, const char *name)
{
    return object != nullptr ? cJSON_GetObjectItemCaseSensitive(object, name) : nullptr;
}
const char *string_field(const cJSON *object, const char *name)
{
    const cJSON *value = field(object, name);
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : nullptr;
}
bool finite_number(const cJSON *value, double *out)
{
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) return false;
    if (out) *out = value->valuedouble;
    return true;
}
bool json_depth_safe(const std::string &text)
{
    unsigned depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (char c : text) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{' || c == '[') {
            if (++depth > kMaxJsonDepth) return false;
        } else if (c == '}' || c == ']') {
            if (depth == 0) return false;
            --depth;
        }
    }
    return !in_string && depth == 0;
}
bool coord(const cJSON *object, const char *name, lv_coord_t *out)
{
    double value = 0;
    if (!finite_number(field(object, name), &value) || value < -32768 || value > 32767 || value != std::floor(value)) return false;
    *out = static_cast<lv_coord_t>(value);
    return true;
}
bool boolean_field(const cJSON *object, const char *name, bool fallback)
{
    const cJSON *value = field(object, name);
    return cJSON_IsBool(value) ? cJSON_IsTrue(value) : fallback;
}
const lv_font_t *font_value(const char *name)
{
    if (name == nullptr || !strcmp(name, "medium")) return &lv_font_montserrat_20;
    if (!strcmp(name, "caption")) return &lv_font_montserrat_14;
    if (!strcmp(name, "small")) return &lv_font_montserrat_16;
    if (!strcmp(name, "large")) return &lv_font_montserrat_28;
    if (!strcmp(name, "display")) return &lv_font_montserrat_48;
    return nullptr;
}
std::string trim(std::string value)
{
    size_t first = value.find_first_not_of(" \t\r\n");
    size_t last = value.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, last - first + 1);
}
bool parse_positive(const std::string &text, uint32_t *out)
{
    if (text.empty()) return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > UINT32_MAX) return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}
std::string quoted_action_argument(const std::string &command)
{
    const size_t open = command.find('(');
    if (open == std::string::npos || command.back() != ')') return {};
    std::string argument = trim(command.substr(open + 1, command.size() - open - 2));
    if (argument.size() >= 2 && argument.front() == '\'' && argument.back() == '\'') {
        argument = argument.substr(1, argument.size() - 2);
    } else if (argument.size() >= 2 && argument.front() == '"' && argument.back() == '"') {
        argument = argument.substr(1, argument.size() - 2);
    }
    return argument;
}
} // namespace

DeclarativeApp::DeclarativeApp(const char *name, const char *package_id, const char *entry,
                               bool system_timer_allowed)
    : CrystalApp(name), package_id_(package_id ? package_id : ""), entry_(entry ? entry : "ui.json"),
      system_timer_allowed_(system_timer_allowed)
{
    setStartupBudget(200); // Package parsing and a multi-control UI: Standard.
    ui_.configure([](const cJSON *spec, lv_obj_t *parent, void *context) {
        auto *app = static_cast<DeclarativeApp *>(context);
        const auto count = lv_obj_get_child_cnt(parent);
        const auto root_count = lv_obj_get_child_cnt(app->root_);
        const auto previous_count = app->node_count_;
        app->node_count_ = 0;
        auto *result = app->build_node(spec, parent, 1);
        app->node_count_ = previous_count;
        if (!result) while (lv_obj_get_child_cnt(parent) > count)
            lv_obj_del(lv_obj_get_child(parent, -1));
        if (!result && parent != app->root_) while (lv_obj_get_child_cnt(app->root_) > root_count)
            lv_obj_del(lv_obj_get_child(app->root_, -1));
        return result;
    }, [](lv_obj_t *object, const cJSON *style) { return apply_style(object, style); },
    [](lv_obj_t *object, const char *path, void *context) {
        return static_cast<DeclarativeApp *>(context)->load_image(object, path);
    }, this);
}

CrystalUI::Handle DeclarativeApp::widget(const char *id) const
{
    if (id) for (const auto &named : named_objects_) if (named.id == id) return ui_.handle(named.object);
    return 0;
}

bool DeclarativeApp::onCreate()
{
    reset_runtime();
    if (!load_document()) return false;
    refresh_bindings();
    return start_binding_timer();
}

bool DeclarativeApp::start_binding_timer()
{
    if (bindings_.empty() || binding_timer_ != nullptr) return true;
    binding_timer_ = lv_timer_create(binding_tick, 1000, this);
    if (binding_timer_ == nullptr) {
        return false;
    }
    binding_timer_handle_ = resources().track(CrystalResourceKind::Timer, binding_timer_);
    if (binding_timer_handle_ == 0) {
        lv_timer_del(binding_timer_);
        binding_timer_ = nullptr;
        return false;
    }
    return true;
}

void DeclarativeApp::stop_binding_timer()
{
    if (binding_timer_ != nullptr) {
        lv_timer_del(binding_timer_);
        binding_timer_ = nullptr;
    }
    if (binding_timer_handle_ != 0) {
        resources().release(binding_timer_handle_);
        binding_timer_handle_ = 0;
    }
}

bool DeclarativeApp::onPause()
{
    ui_.pause();
    stop_binding_timer();
    return true;
}

bool DeclarativeApp::onResume()
{
    ui_.resume();
    refresh_bindings();
    return start_binding_timer();
}

bool DeclarativeApp::onDestroy()
{
    stop_binding_timer();
    ui_.clear_callbacks();
    // Deleting the root also releases the single tracked object through the
    // CrystalApp LVGL delete callback. Children are owned by that root.
    if (root_ != nullptr) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
    ui_.reset();
    bindings_.clear();
    actions_.clear();
    button_maps_.clear();
    images_.clear();
    image_bytes_ = 0;
    named_objects_.clear();
    node_count_ = 0;
    return true;
}

void DeclarativeApp::reset_runtime()
{
    stop_binding_timer();
    ui_.clear_callbacks();
    if (root_ != nullptr) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
    ui_.reset();
    bindings_.clear();
    actions_.clear();
    button_maps_.clear();
    images_.clear();
    image_bytes_ = 0;
    named_objects_.clear();
    node_count_ = 0;
}

bool DeclarativeApp::load_document()
{
    std::string text;
    if (!crystal_package_read_file(package_id_, entry_, &text, kMaxDocumentBytes)) {
        ESP_LOGE(TAG, "%s: cannot read %s", package_id_.c_str(), entry_.c_str());
        return false;
    }
    if (!json_depth_safe(text)) {
        ESP_LOGE(TAG, "%s: UI JSON exceeds the nesting limit", package_id_.c_str());
        return false;
    }
    Json document(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true), cJSON_Delete);
    if (!document || !cJSON_IsObject(document.get())) {
        ESP_LOGE(TAG, "%s: invalid UI JSON", package_id_.c_str());
        return false;
    }
    const cJSON *screen = field(document.get(), "screen");
    if (!cJSON_IsObject(screen)) {
        ESP_LOGE(TAG, "%s: UI document has no screen object", package_id_.c_str());
        return false;
    }

    const lv_area_t area = getVisualArea();
    root_ = lv_obj_create(lv_scr_act());
    if (root_ == nullptr || !trackObject(root_)) {
        if (root_) lv_obj_del(root_);
        root_ = nullptr;
        return false;
    }
    // Dimensions and position are local styles too. Clear the theme before
    // assigning them, otherwise LVGL resets the root to its default 130x130.
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, lv_area_get_width(&area), lv_area_get_height(&area));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x11181F), 0);
    lv_obj_set_style_text_color(root_, lv_color_hex(0xE6EDF5), 0);
    lv_obj_set_style_text_font(root_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(root_, 16, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    ui_.set_root(root_);
    if (!ui_.adopt(root_)) return false;
    if (!apply_node_options(root_, screen, false)) return false;
    if (const char *id = string_field(screen, "id")) {
        if (!*id || strlen(id) > 31) return false;
        named_objects_.push_back({root_, nullptr, id});
    }

    const cJSON *children = field(screen, "children");
    if (children != nullptr && (!cJSON_IsArray(children) || cJSON_GetArraySize(children) > int(kMaxNodes))) return false;
    if (children != nullptr) {
        cJSON *child = nullptr;
        cJSON_ArrayForEach(child, children) {
            if (build_node(child, root_, 1) == nullptr) {
                ESP_LOGE(TAG, "%s: invalid or unsupported UI node", package_id_.c_str());
                return false;
            }
        }
    }

    const cJSON *bindings_field = field(document.get(), "bindings");
    if (bindings_field != nullptr && !cJSON_IsString(bindings_field)) return false;
    const char *bindings_file = string_field(document.get(), "bindings");
    if (bindings_file != nullptr) {
        std::string binding_text;
        if (!crystal_package_read_file(package_id_, bindings_file, &binding_text, kMaxDocumentBytes) ||
            !load_bindings(binding_text)) return false;
    }
    return true;
}

bool DeclarativeApp::load_bindings(const std::string &text)
{
    if (!json_depth_safe(text)) return false;
    Json document(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true), cJSON_Delete);
    if (!document || !cJSON_IsObject(document.get())) {
        ESP_LOGE(TAG, "%s: invalid bindings JSON", package_id_.c_str());
        return false;
    }
    const cJSON *list = field(document.get(), "bindings");
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > int(kMaxBindings)) return false;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, list) {
        const char *id = string_field(item, "id");
        const char *property = string_field(item, "property");
        const char *expression = string_field(item, "value");
        if (id == nullptr || property == nullptr || expression == nullptr) return false;
        const NamedObject *target = nullptr;
        for (const auto &named : named_objects_) if (named.id == id) { target = &named; break; }
        if (target == nullptr || bindings_.size() >= kMaxBindings) return false;
        Binding::Property binding_property;
        if (!strcmp(property, "text")) binding_property = Binding::Property::Text;
        else if (!strcmp(property, "visible")) binding_property = Binding::Property::Visible;
        else return false;
        lv_obj_t *binding_target = binding_property == Binding::Property::Text
            ? target->text_object : target->object;
        if (binding_target == nullptr) return false;
        bindings_.push_back({binding_target, expression, binding_property});
    }
    return true;
}

bool DeclarativeApp::load_image(lv_obj_t *object, const char *path)
{
    if (object == nullptr || path == nullptr || !*path || strlen(path) > 127) return false;
    if (strncmp(path, "assets/", 7) && strncmp(path, "data/", 5)) return false;
    for (const auto &image : images_) if (image.path == path) {
        lv_img_set_src(object, &image.descriptor); return true;
    }
    std::string bytes;
    if (!crystal_package_read_file(package_id_, path, &bytes, kMaxImageSourceBytes)) return false;
    std::istringstream input(bytes);
    std::string magic;
    unsigned width = 0, height = 0;
    if (!(input >> magic >> width >> height) || magic != "CR16" || width == 0 || height == 0 ||
        width > kMaxImageDimension || height > kMaxImageDimension) return false;
    const size_t pixels = static_cast<size_t>(width) * height;
    if (pixels > kMaxTotalImageBytes / sizeof(lv_color_t) ||
        image_bytes_ > kMaxTotalImageBytes - pixels * sizeof(lv_color_t)) return false;
    EmbeddedImage decoded;
    EmbeddedImage &image = decoded;
    image.pixels.resize(pixels * sizeof(lv_color_t));
    for (size_t index = 0; index < pixels; ++index) {
        std::string token;
        if (!(input >> token) || token.empty() || token.size() > 8) return false;
        char *end = nullptr;
        const unsigned long color = strtoul(token.c_str(), &end, 16);
        if (end == token.c_str() || *end != '\0' || color > 0xFFFFFF) return false;
        const lv_color_t pixel = lv_color_hex(static_cast<uint32_t>(color));
        memcpy(image.pixels.data() + index * sizeof(pixel), &pixel, sizeof(pixel));
    }
    std::string trailing;
    if (input >> trailing) return false;
    image_bytes_ += image.pixels.size();
    image.path = path;
    image.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    image.descriptor.header.always_zero = 0;
    image.descriptor.header.reserved = 0;
    image.descriptor.header.w = static_cast<uint16_t>(width);
    image.descriptor.header.h = static_cast<uint16_t>(height);
    image.descriptor.data_size = static_cast<uint32_t>(image.pixels.size());
    image.descriptor.data = image.pixels.data();
    images_.push_back(std::move(decoded));
    lv_img_set_src(object, &images_.back().descriptor);
    return true;
}

lv_obj_t *DeclarativeApp::build_node(const void *raw_node, lv_obj_t *parent, unsigned depth)
{
    const cJSON *node = static_cast<const cJSON *>(raw_node);
    if (!cJSON_IsObject(node) || parent == nullptr || depth > kMaxDepth || ++node_count_ > kMaxNodes) return nullptr;
    const char *type = string_field(node, "type");
    if (type == nullptr) return nullptr;
    lv_obj_t *object = nullptr;
    const bool container = !strcmp(type, "container") || !strcmp(type, "row") || !strcmp(type, "col") || !strcmp(type, "scroll_view");
    if (container) object = lv_obj_create(parent);
    else if (!strcmp(type, "label")) object = lv_label_create(parent);
    else if (!strcmp(type, "button")) object = lv_btn_create(parent);
    else if (!strcmp(type, "switch")) object = lv_switch_create(parent);
    else if (!strcmp(type, "slider")) object = lv_slider_create(parent);
    else if (!strcmp(type, "arc")) object = lv_arc_create(parent);
    else if (!strcmp(type, "image")) object = lv_img_create(parent);
    else if (!strcmp(type, "list")) object = lv_list_create(parent);
    else if (!strcmp(type, "chart")) object = lv_chart_create(parent);
    else if (!strcmp(type, "textarea")) object = lv_textarea_create(parent);
    else if (!strcmp(type, "spinner")) {
        double spin_time = 1000, arc_length = 60;
        if (field(node, "spin_time") && !finite_number(field(node, "spin_time"), &spin_time)) return nullptr;
        if (field(node, "arc_length") && !finite_number(field(node, "arc_length"), &arc_length)) return nullptr;
        if (spin_time < 100 || spin_time > 60000 || arc_length < 10 || arc_length > 350) return nullptr;
        object = lv_spinner_create(parent, static_cast<uint32_t>(spin_time), static_cast<uint32_t>(arc_length));
    }
    else if (!strcmp(type, "dialog")) {
        const char *title = string_field(node, "title");
        const char *message = string_field(node, "message");
        const cJSON *buttons = field(node, "buttons");
        if (title == nullptr || message == nullptr || !cJSON_IsArray(buttons) ||
            cJSON_GetArraySize(buttons) == 0 || cJSON_GetArraySize(buttons) > 2) return nullptr;
        ButtonMap map;
        cJSON *button = nullptr;
        cJSON_ArrayForEach(button, buttons) {
            const char *text = cJSON_IsString(button) ? button->valuestring : string_field(button, "text");
            if (!text || !*text || strlen(text) > 32) return nullptr;
            map.labels.emplace_back(text);
            const auto *action = field(button, "on_click");
            char *encoded = cJSON_IsObject(action) ? cJSON_PrintUnformatted(action) : nullptr;
            const char *command = encoded ? encoded : string_field(button, "on_click");
            if (action && !command) { cJSON_free(encoded); return nullptr; }
            map.commands.emplace_back(command ? command : "");
            cJSON_free(encoded);
        }
        button_maps_.push_back(std::move(map));
        auto &stored = button_maps_.back();
        for (const auto &label : stored.labels) stored.pointers.push_back(label.c_str());
        stored.pointers.push_back("");
        object = lv_msgbox_create(nullptr, title, message, stored.pointers.data(), false);
        if (!object) { button_maps_.pop_back(); return nullptr; }
        // Keep LVGL's modal backdrop inside this app's owned tree. It blocks
        // app content, follows app visibility, and disappears at teardown.
        auto *backdrop = lv_obj_get_parent(object);
        lv_obj_set_parent(backdrop, root_);
        lv_obj_add_flag(backdrop, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
        lv_obj_center(object);
        stored.object = lv_msgbox_get_btns(object);
        lv_obj_add_event_cb(stored.object, node_deleted, LV_EVENT_DELETE, this);
    }
    else if (!strcmp(type, "tabview")) {
        double bar_height = 44;
        if (field(node, "tab_bar_height") && !finite_number(field(node, "tab_bar_height"), &bar_height)) return nullptr;
        if (bar_height < 24 || bar_height > 80) return nullptr;
        object = lv_tabview_create(parent, LV_DIR_TOP, static_cast<lv_coord_t>(bar_height));
    }
    else if (!strcmp(type, "button_matrix")) object = lv_btnmatrix_create(parent);
    else return nullptr;
    if (object == nullptr) return nullptr;
    if (!ui_.adopt(object)) { lv_obj_del(object); return nullptr; }
    lv_obj_add_event_cb(object, node_deleted, LV_EVENT_DELETE, this);
    if (container) {
        // Layout groups fill their parent's content width and fit their own
        // children; scroll views retain scrolling within an explicit height.
        lv_obj_remove_style_all(object);
        lv_obj_set_width(object, LV_PCT(100));
        lv_obj_set_height(object, LV_SIZE_CONTENT);
        if (strcmp(type, "scroll_view")) lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    }
    // Match the OS's dark slate surfaces and quiet teal accent. Package JSON
    // may refine these through the small style allowlist below.
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    if (!strcmp(type, "label")) {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), 0);
    } else if (!strcmp(type, "button")) {
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x273541), 0);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x344553), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(object, lv_color_hex(0xF4F7FA), 0);
        lv_obj_set_style_radius(object, 8, 0);
    } else if (!strcmp(type, "switch")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x53C8B6),
            static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_INDICATOR) |
                                            static_cast<uint32_t>(LV_STATE_CHECKED)));
        lv_obj_set_style_bg_color(object, lv_color_hex(0xF4F7FA), LV_PART_KNOB);
    } else if (!strcmp(type, "slider")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(object, lv_color_hex(0xF4F7FA), LV_PART_KNOB);
    } else if (!strcmp(type, "arc")) {
        lv_obj_set_style_arc_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_arc_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x53C8B6), LV_PART_KNOB);
    } else if (!strcmp(type, "image")) {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    } else if (!strcmp(type, "list")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x1C2733), LV_PART_MAIN);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), LV_PART_MAIN);
    } else if (!strcmp(type, "chart")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x1C2733), LV_PART_MAIN);
        lv_obj_set_style_line_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
    } else if (!strcmp(type, "textarea")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x1C2733), LV_PART_MAIN);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), LV_PART_MAIN);
        lv_obj_set_style_border_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(object, 8, LV_PART_MAIN);
    } else if (!strcmp(type, "spinner")) {
        lv_obj_set_style_arc_color(object, lv_color_hex(0x344553), LV_PART_MAIN);
        lv_obj_set_style_arc_color(object, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
    } else if (!strcmp(type, "dialog")) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x1C2733), LV_PART_MAIN);
        lv_obj_set_style_text_color(object, lv_color_hex(0xE6EDF5), LV_PART_MAIN);
        auto *buttons = lv_msgbox_get_btns(object);
        lv_obj_set_height(buttons, 44);
        lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(buttons, lv_color_hex(0x273541), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(buttons, lv_color_hex(0x344553),
            static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_PRESSED)));
        lv_obj_set_style_text_color(buttons, lv_color_hex(0xF4F7FA), LV_PART_ITEMS);
        lv_obj_set_style_radius(buttons, 8, LV_PART_ITEMS);
    } else if (!strcmp(type, "button_matrix")) {
        lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(object, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_column(object, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x11181F), LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x273541), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x344553),
            static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) |
                                            static_cast<uint32_t>(LV_STATE_PRESSED)));
        lv_obj_set_style_text_color(object, lv_color_hex(0xF4F7FA), LV_PART_ITEMS);
        lv_obj_set_style_radius(object, 8, LV_PART_ITEMS);
    } else {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    }
    if (!apply_node_options(object, node, !container && strcmp(type, "label"))) return nullptr;

    if (!strcmp(type, "image")) {
        if (!load_image(object, string_field(node, "src"))) return nullptr;
        // Scale inside the layout's declared bounds. Virtual mode treats an
        // enlarged object as tiled source space and paints repeated icons.
        lv_img_set_size_mode(object, LV_IMG_SIZE_MODE_REAL);
        if (field(node, "scale")) {
            double scale = 1;
            if (!finite_number(field(node, "scale"), &scale) || scale < 0.25 || scale > 8) return nullptr;
            lv_img_set_zoom(object, static_cast<uint16_t>(scale * 256));
        }
    } else if (!strcmp(type, "textarea")) {
        if (const char *text = string_field(node, "text")) lv_textarea_set_text(object, text);
        if (const char *placeholder = string_field(node, "placeholder"))
            lv_textarea_set_placeholder_text(object, placeholder);
        if (field(node, "max_length")) {
            double value = 0;
            if (!finite_number(field(node, "max_length"), &value) || value < 1 || value > 4096) return nullptr;
            lv_textarea_set_max_length(object, static_cast<uint32_t>(value));
        }
        if (field(node, "one_line")) {
            if (!cJSON_IsBool(field(node, "one_line"))) return nullptr;
            lv_textarea_set_one_line(object, cJSON_IsTrue(field(node, "one_line")));
        }
    } else if (!strcmp(type, "list")) {
        const cJSON *items = field(node, "items");
        if (items != nullptr && (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > 64)) return nullptr;
        cJSON *item = nullptr;
        if (items != nullptr) cJSON_ArrayForEach(item, items)
            if (!insert_list_item(object, lv_obj_get_child_cnt(object), item)) return nullptr;
        if (field(node, "item_height")) {
            double height = 0;
            if (!finite_number(field(node, "item_height"), &height) || height < 44 || height > 480) return nullptr;
            if (height != std::floor(height) || !ui_.list_set_item_height(ui_.handle(object), static_cast<unsigned>(height))) return nullptr;
        }
    } else if (!strcmp(type, "chart")) {
        double point_count = 16;
        if (field(node, "point_count") && !finite_number(field(node, "point_count"), &point_count)) return nullptr;
        if (point_count < 1 || point_count > 128) return nullptr;
        lv_chart_set_point_count(object, static_cast<uint16_t>(point_count));
        double minimum = 0, maximum = 100;
        if (field(node, "min") && !finite_number(field(node, "min"), &minimum)) return nullptr;
        if (field(node, "max") && !finite_number(field(node, "max"), &maximum)) return nullptr;
        if (minimum >= maximum || minimum < -32768 || maximum > 32767) return nullptr;
        if ((field(node, "min") || field(node, "max")) &&
            !ui_.chart_set_range(ui_.handle(object), static_cast<int>(minimum), static_cast<int>(maximum))) return nullptr;
        const char *chart_type = string_field(node, "chart_type");
        if (chart_type == nullptr || !strcmp(chart_type, "line")) lv_chart_set_type(object, LV_CHART_TYPE_LINE);
        else if (!strcmp(chart_type, "bar")) lv_chart_set_type(object, LV_CHART_TYPE_BAR);
        else if (!strcmp(chart_type, "scatter")) lv_chart_set_type(object, LV_CHART_TYPE_SCATTER);
        else return nullptr;
        const cJSON *series = field(node, "series");
        if (series != nullptr && !cJSON_IsArray(series)) return nullptr;
        if (series != nullptr && cJSON_GetArraySize(series) > 4) return nullptr;
        cJSON *item = nullptr;
        if (series != nullptr) cJSON_ArrayForEach(item, series) {
            if (!cJSON_IsObject(item)) return nullptr;
            double color = 0x53C8B6;
            if (field(item, "color") && !finite_number(field(item, "color"), &color)) return nullptr;
            const cJSON *values = field(item, "values");
            if (!cJSON_IsArray(values) || cJSON_GetArraySize(values) > static_cast<int>(point_count)) return nullptr;
            if (color < 0 || color > 0xFFFFFF) return nullptr;
            const auto line = ui_.chart_add_series(ui_.handle(object), static_cast<uint32_t>(color));
            if (!line) return nullptr;
            std::vector<int> data;
            for (int index = 0; index < cJSON_GetArraySize(values); ++index) {
                double value = 0;
                if (!finite_number(cJSON_GetArrayItem(values, index), &value) || value < -32768 || value > 32767)
                    return nullptr;
                if (value != std::floor(value)) return nullptr;
                data.push_back(static_cast<int>(value));
            }
            if (!ui_.chart_set_data(ui_.handle(object), line, data)) return nullptr;
        }
        lv_chart_refresh(object);
    }

    if (!strcmp(type, "button_matrix")) {
        const cJSON *buttons = field(node, "buttons");
        if (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons) == 0 || cJSON_GetArraySize(buttons) > 64) return nullptr;
        ButtonMap map;
        map.object = object;
        cJSON *button = nullptr;
        cJSON_ArrayForEach(button, buttons) {
            if (!cJSON_IsString(button) || button->valuestring == nullptr || strlen(button->valuestring) > 32) return nullptr;
            map.labels.emplace_back(button->valuestring);
        }
        button_maps_.push_back(std::move(map));
        auto &stored = button_maps_.back();
        for (const auto &label : stored.labels) stored.pointers.push_back(label.c_str());
        stored.pointers.push_back("");
        lv_btnmatrix_set_map(object, stored.pointers.data());
    }

    const char *node_id = string_field(node, "id");
    if (node_id != nullptr) {
        if (!*node_id || strlen(node_id) > 31) return nullptr;
        for (const auto &named : named_objects_) if (named.id == node_id) return nullptr;
    }
    lv_obj_t *text_object = nullptr;
    if (!strcmp(type, "label") || !strcmp(type, "button")) {
        const char *text = string_field(node, "text");
        if (text == nullptr) text = "";
        text_object = !strcmp(type, "button") ? lv_label_create(object) : object;
        lv_label_set_text(text_object, text);
        if (!strcmp(type, "button")) lv_obj_center(text_object);
        if (field(node, "font") != nullptr) {
            const char *font = string_field(node, "font");
            const lv_font_t *value = font_value(font);
            if (value == nullptr) return nullptr;
            lv_obj_set_style_text_font(text_object, value, 0);
        }
        if (strstr(text, "{{") != nullptr) {
            if (bindings_.size() >= kMaxBindings) return nullptr;
            bindings_.push_back({text_object, text, Binding::Property::Text});
        }
        if (text_object != object) lv_obj_add_event_cb(text_object, node_deleted, LV_EVENT_DELETE, this);
    }
    if (!ui_.adopt(object, text_object)) return nullptr;
    if (node_id != nullptr) named_objects_.push_back({object, text_object, node_id});
    if (const cJSON *visible = field(node, "visible")) {
        if (cJSON_IsBool(visible)) {
            if (!ui_.set_hidden(ui_.handle(object), !cJSON_IsTrue(visible))) return nullptr;
        } else if (cJSON_IsString(visible) && visible->valuestring != nullptr &&
                   strstr(visible->valuestring, "{{") != nullptr) {
            if (bindings_.size() >= kMaxBindings) return nullptr;
            bindings_.push_back({object, visible->valuestring, Binding::Property::Visible});
        } else return nullptr;
    }
    constexpr struct { const char *name; lv_event_code_t event; } node_events[] = {
        {"on_click", LV_EVENT_CLICKED}, {"on_long_press", LV_EVENT_LONG_PRESSED},
        {"on_change", LV_EVENT_VALUE_CHANGED}, {"on_focus", LV_EVENT_FOCUSED},
        {"on_blur", LV_EVENT_DEFOCUSED}, {"on_scroll", LV_EVENT_SCROLL}, {"on_delete", LV_EVENT_DELETE},
    };
    for (const auto &spec : node_events) {
        const auto *action = field(node, spec.name);
        if (!action) continue;
        char *encoded = cJSON_IsObject(action) ? cJSON_PrintUnformatted(action) : nullptr;
        const char *command = encoded ? encoded : string_field(node, spec.name);
        auto *target = !strcmp(type, "dialog") && spec.event == LV_EVENT_CLICKED ? lv_msgbox_get_btns(object) : object;
        const auto code = spec.event == LV_EVENT_CLICKED && (!strcmp(type, "dialog") || !strcmp(type, "button_matrix"))
            ? LV_EVENT_VALUE_CHANGED : spec.event;
        const bool ok = command && apply_action(target, command, code);
        cJSON_free(encoded);
        if (!ok) return nullptr;
    }
    if (!strcmp(type, "dialog")) lv_obj_add_event_cb(lv_msgbox_get_btns(object), dialog_event, LV_EVENT_VALUE_CHANGED, this);
    const cJSON *children = field(node, "children");
    if (children != nullptr) {
        if (!cJSON_IsArray(children) || cJSON_GetArraySize(children) > int(kMaxNodes)) return nullptr;
        if (!strcmp(type, "tabview")) {
            if (cJSON_GetArraySize(children) == 0 || cJSON_GetArraySize(children) > 8) return nullptr;
            cJSON *tab_spec = nullptr;
            cJSON_ArrayForEach(tab_spec, children) {
                if (!cJSON_IsObject(tab_spec) || ++node_count_ > kMaxNodes || depth + 1 > kMaxDepth) return nullptr;
                const char *tab_type = string_field(tab_spec, "type");
                const char *title = string_field(tab_spec, "title");
                if (tab_type == nullptr || strcmp(tab_type, "tab") || title == nullptr || strlen(title) > 24) return nullptr;
                lv_obj_t *tab = lv_tabview_add_tab(object, title);
                if (tab == nullptr) return nullptr;
                if (!ui_.adopt(tab)) return nullptr;
                lv_obj_add_event_cb(tab, node_deleted, LV_EVENT_DELETE, this);
                lv_obj_set_style_bg_color(tab, lv_color_hex(0x11181F), 0);
                lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(tab, 0, 0);
                lv_obj_set_style_radius(tab, 0, 0);
                lv_obj_set_style_pad_all(tab, 16, 0);
                if (!apply_node_options(tab, tab_spec, false)) return nullptr;
                if (const char *tab_id = string_field(tab_spec, "id")) {
                    if (!*tab_id || strlen(tab_id) > 31) return nullptr;
                    for (const auto &named : named_objects_) if (named.id == tab_id) return nullptr;
                    named_objects_.push_back({tab, nullptr, tab_id});
                }
                const cJSON *tab_children = field(tab_spec, "children");
                if (tab_children != nullptr) {
                    if (!cJSON_IsArray(tab_children) || cJSON_GetArraySize(tab_children) > int(kMaxNodes)) return nullptr;
                    cJSON *child = nullptr;
                    cJSON_ArrayForEach(child, tab_children)
                        if (build_node(child, tab, depth + 2) == nullptr) return nullptr;
                }
            }
            lv_obj_clear_flag(lv_tabview_get_content(object), LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *content = lv_tabview_get_content(object);
            lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(content, 0, 0);
            lv_obj_t *tab_buttons = lv_tabview_get_tab_btns(object);
            lv_obj_set_style_bg_color(tab_buttons, lv_color_hex(0x1C2733), LV_PART_MAIN);
            lv_obj_set_style_bg_color(tab_buttons, lv_color_hex(0x53C8B6), LV_PART_INDICATOR);
            lv_obj_set_style_text_color(tab_buttons, lv_color_hex(0xE6EDF5), LV_PART_ITEMS);
            lv_obj_set_style_text_color(tab_buttons, lv_color_hex(0x53C8B6),
                static_cast<lv_style_selector_t>(static_cast<uint32_t>(LV_PART_ITEMS) |
                                                 static_cast<uint32_t>(LV_STATE_CHECKED)));
            const auto checked = static_cast<lv_style_selector_t>(
                static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_CHECKED));
            lv_obj_set_style_bg_color(tab_buttons, lv_color_hex(0x273541), checked);
            lv_obj_set_style_border_color(tab_buttons, lv_color_hex(0x53C8B6), checked);
            lv_obj_set_style_border_width(tab_buttons, 2, checked);
            lv_obj_set_style_border_side(tab_buttons, LV_BORDER_SIDE_BOTTOM, checked);
        } else {
            if (!container) return nullptr;
            cJSON *child = nullptr;
            cJSON_ArrayForEach(child, children) if (build_node(child, object, depth + 1) == nullptr) return nullptr;
        }
    }
    else if (!strcmp(type, "tabview")) return nullptr;
    return object;
}

bool DeclarativeApp::apply_node_options(lv_obj_t *object, const void *raw_node, bool interactive)
{
    const cJSON *node = static_cast<const cJSON *>(raw_node);
    if (object == nullptr) return false;
    lv_coord_t width = 0, height = 0, x = 0, y = 0;
    if (field(node, "width") && !coord(node, "width", &width)) return false;
    if (field(node, "height") && !coord(node, "height", &height)) return false;
    if (field(node, "x") && !coord(node, "x", &x)) return false;
    if (field(node, "y") && !coord(node, "y", &y)) return false;
    if (field(node, "width")) lv_obj_set_width(object, width);
    if (field(node, "height")) lv_obj_set_height(object, height);
    if (field(node, "x") || field(node, "y")) lv_obj_set_pos(object, x, y);
    if (interactive && boolean_field(node, "enabled", true)) lv_obj_clear_state(object, LV_STATE_DISABLED);
    if (interactive && !boolean_field(node, "enabled", true)) lv_obj_add_state(object, LV_STATE_DISABLED);

    const char *flow = string_field(node, "flex_direction");
    const char *type = string_field(node, "type");
    if (flow == nullptr && type != nullptr) {
        if (!strcmp(type, "row")) flow = "row";
        else if (!strcmp(type, "col")) flow = "column";
    }
    if (flow != nullptr && (!strcmp(flow, "row") || !strcmp(flow, "column"))) {
        const bool wrap = boolean_field(node, "flex_wrap", false);
        lv_obj_set_flex_flow(object, !strcmp(flow, "row")
            ? (wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW)
            : (wrap ? LV_FLEX_FLOW_COLUMN_WRAP : LV_FLEX_FLOW_COLUMN));
        lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    if (type != nullptr && !strcmp(type, "scroll_view")) lv_obj_set_scroll_dir(object, LV_DIR_VER);
    if (const cJSON *gap = field(node, "gap")) {
        double value = 0;
        if (!finite_number(gap, &value) || value < 0 || value > 255) return false;
        lv_obj_set_style_pad_row(object, static_cast<lv_coord_t>(value), 0);
        lv_obj_set_style_pad_column(object, static_cast<lv_coord_t>(value), 0);
    }
    if (type != nullptr && !strcmp(type, "button") && !field(node, "height"))
        lv_obj_set_height(object, 44);
    if (type != nullptr && !strcmp(type, "button")) lv_obj_set_style_min_height(object, 44, 0);
    if (const cJSON *color = field(node, "color")) {
        double value = 0;
        if (!finite_number(color, &value) || value < 0 || value > 0xFFFFFF || value != std::floor(value)) return false;
        lv_obj_set_style_text_color(object, lv_color_hex(static_cast<uint32_t>(value)), 0);
        if (type && !strcmp(type, "spinner")) lv_obj_set_style_arc_color(object, lv_color_hex(static_cast<uint32_t>(value)), LV_PART_INDICATOR);
    }
    if (type && !strcmp(type, "spinner") && field(node, "size")) {
        double size = 0;
        if (!finite_number(field(node, "size"), &size) || size < 8 || size > 480 || size != std::floor(size)) return false;
        lv_obj_set_size(object, static_cast<lv_coord_t>(size), static_cast<lv_coord_t>(size));
    }
    if (type != nullptr && (!strcmp(type, "slider") || !strcmp(type, "arc"))) {
        double minimum = 0, maximum = 100;
        if (field(node, "min") && !finite_number(field(node, "min"), &minimum)) return false;
        if (field(node, "max") && !finite_number(field(node, "max"), &maximum)) return false;
        if (minimum < std::numeric_limits<int32_t>::min() ||
            maximum > std::numeric_limits<int32_t>::max() || minimum >= maximum) return false;
        if (!strcmp(type, "slider")) lv_slider_set_range(object, static_cast<int32_t>(minimum), static_cast<int32_t>(maximum));
        else lv_arc_set_range(object, static_cast<int32_t>(minimum), static_cast<int32_t>(maximum));
    }
    if (const cJSON *style = field(node, "style")) if (!apply_style(object, style)) return false;
    if (const char *align = string_field(node, "align")) {
        if (!strcmp(align, "left")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        else if (!strcmp(align, "center")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
        else if (!strcmp(align, "right")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, 0);
        else return false;
    }
    if (field(node, "value") == nullptr) return true;
    const cJSON *value = field(node, "value");
    if (type != nullptr && !strcmp(type, "switch")) {
        bool checked = false;
        if (cJSON_IsBool(value)) checked = cJSON_IsTrue(value);
        else {
            double number = 0;
            if (!finite_number(value, &number) || (number != 0 && number != 1)) return false;
            checked = number == 1;
        }
        if (checked) lv_obj_add_state(object, LV_STATE_CHECKED);
        else lv_obj_clear_state(object, LV_STATE_CHECKED);
        return true;
    }
    double number = 0;
    if (type == nullptr || (strcmp(type, "slider") && strcmp(type, "arc")) ||
        !finite_number(value, &number) || number < -1000000 || number > 1000000) return false;
    if (!strcmp(type, "slider")) lv_slider_set_value(object, static_cast<int32_t>(number), LV_ANIM_OFF);
    else lv_arc_set_value(object, static_cast<int32_t>(number));
    return true;
}

bool DeclarativeApp::apply_style(lv_obj_t *object, const void *raw_style)
{
    const cJSON *style = static_cast<const cJSON *>(raw_style);
    if (!cJSON_IsObject(style)) return false;
    static const char *const allowed[] = {"bg_color", "text_color", "radius", "padding", "opacity",
                                           "font_size", "border_width", "border_color", "align"};
    for (const cJSON *item = style->child; item != nullptr; item = item->next) {
        bool known = false;
        for (const char *key : allowed) if (item->string != nullptr && !strcmp(item->string, key)) known = true;
        if (!known) return false;
        const std::string key(item->string);
        if (key == "align") {
            if (!cJSON_IsString(item) || !item->valuestring ||
                (strcmp(item->valuestring, "left") && strcmp(item->valuestring, "center") && strcmp(item->valuestring, "right"))) return false;
        } else {
            double value = 0;
            if (!finite_number(item, &value) || value != std::floor(value) || value < 0) return false;
            if (key == "font_size") {
                if (value != 14 && value != 16 && value != 20 && value != 28 && value != 48) return false;
            } else if (value > (key == "bg_color" || key == "text_color" || key == "border_color" ? 0xFFFFFF
                               : key == "border_width" ? 32 : 255)) return false;
        }
    }
    if (const cJSON *value = field(style, "bg_color")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 0xFFFFFF) return false;
        lv_obj_set_style_bg_color(object, lv_color_hex(static_cast<uint32_t>(number)), 0);
    }
    if (const cJSON *value = field(style, "text_color")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 0xFFFFFF) return false;
        lv_obj_set_style_text_color(object, lv_color_hex(static_cast<uint32_t>(number)), 0);
    }
    if (const cJSON *value = field(style, "radius")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 255) return false;
        lv_obj_set_style_radius(object, static_cast<lv_coord_t>(number), 0);
    }
    if (const cJSON *value = field(style, "padding")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 255) return false;
        lv_obj_set_style_pad_all(object, static_cast<lv_coord_t>(number), 0);
    }
    if (const cJSON *value = field(style, "opacity")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 255) return false;
        lv_obj_set_style_opa(object, static_cast<lv_opa_t>(number), 0);
    }
    if (const cJSON *value = field(style, "border_width")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 32) return false;
        lv_obj_set_style_border_width(object, static_cast<lv_coord_t>(number), 0);
    }
    if (const cJSON *value = field(style, "border_color")) {
        double number = 0; if (!finite_number(value, &number) || number < 0 || number > 0xFFFFFF) return false;
        lv_obj_set_style_border_color(object, lv_color_hex(static_cast<uint32_t>(number)), 0);
    }
    if (const char *align = string_field(style, "align")) {
        if (!strcmp(align, "left")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_LEFT, 0);
        else if (!strcmp(align, "center")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
        else if (!strcmp(align, "right")) lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, 0);
        else return false;
    }
    if (const cJSON *value = field(style, "font_size")) {
        double number = 0; if (!finite_number(value, &number)) return false;
        if (number == 14) lv_obj_set_style_text_font(object, &lv_font_montserrat_14, 0);
        else if (number == 16) lv_obj_set_style_text_font(object, &lv_font_montserrat_16, 0);
        else if (number == 20) lv_obj_set_style_text_font(object, &lv_font_montserrat_20, 0);
        else if (number == 28) lv_obj_set_style_text_font(object, &lv_font_montserrat_28, 0);
        else if (number == 48) lv_obj_set_style_text_font(object, &lv_font_montserrat_48, 0);
        else return false;
    }
    return true;
}

bool DeclarativeApp::apply_action(lv_obj_t *object, const char *command, lv_event_code_t event)
{
    if (object == nullptr || command == nullptr || !*command || actions_.size() >= kMaxActions) return false;
    actions_.push_back({this, command, object});
    if (event == LV_EVENT_DELETE) {
        return ui_.on(ui_.handle(object), "delete", [](CrystalUI::Handle, void *context) {
            auto *action = static_cast<Action *>(context);
            auto *app = action->app; const auto copy = action->command;
            (void)app->dispatch_action(copy);
        }, &actions_.back());
    }
    lv_obj_add_event_cb(object, action_event, event, &actions_.back());
    return true;
}

bool DeclarativeApp::insert_list_item(lv_obj_t *list, unsigned index, const void *raw_item)
{
    const auto *item = static_cast<const cJSON *>(raw_item);
    CrystalUI::ListItem value{};
    if (cJSON_IsString(item)) value.primary = item->valuestring;
    else if (cJSON_IsObject(item)) {
        value.primary = string_field(item, "primary");
        value.secondary = string_field(item, "secondary");
        value.icon = string_field(item, "icon");
    } else return false;
    const auto *action = field(item, "on_click");
    char *encoded = cJSON_IsObject(action) ? cJSON_PrintUnformatted(action) : nullptr;
    const char *command = encoded ? encoded : string_field(item, "on_click");
    if (action && (!command || !*command || actions_.size() >= kMaxActions)) { cJSON_free(encoded); return false; }
    if (!ui_.list_insert(ui_.handle(list), index, value)) { cJSON_free(encoded); return false; }
    auto *row = lv_obj_get_child(list, static_cast<int32_t>(index));
    lv_obj_add_event_cb(row, node_deleted, LV_EVENT_DELETE, this);
    const bool ok = !action || apply_action(row, command, LV_EVENT_CLICKED);
    cJSON_free(encoded);
    if (!ok) lv_obj_del(row);
    return ok;
}

void DeclarativeApp::action_event(lv_event_t *event)
{
    auto *action = static_cast<Action *>(lv_event_get_user_data(event));
    // Dispatch may delete the event's object and its Action record.
    if (action != nullptr && action->app != nullptr) {
        auto *app = action->app; const auto command = action->command;
        (void)app->dispatch_action(command);
    }
}

void DeclarativeApp::node_deleted(lv_event_t *event)
{
    auto *app = static_cast<DeclarativeApp *>(lv_event_get_user_data(event));
    auto *object = lv_event_get_target(event);
    app->bindings_.erase(std::remove_if(app->bindings_.begin(), app->bindings_.end(),
        [object](const Binding &binding) { return binding.object == object; }), app->bindings_.end());
    app->named_objects_.erase(std::remove_if(app->named_objects_.begin(), app->named_objects_.end(),
        [object](const NamedObject &named) { return named.object == object || named.text_object == object; }), app->named_objects_.end());
    app->actions_.remove_if([object](const Action &action) { return action.object == object; });
    app->button_maps_.remove_if([object](const ButtonMap &map) { return map.object == object; });
}

void DeclarativeApp::dialog_event(lv_event_t *event)
{
    auto *app = static_cast<DeclarativeApp *>(lv_event_get_user_data(event));
    auto *buttons = lv_event_get_target(event);
    auto *dialog = lv_obj_get_parent(buttons);
    const auto selected = lv_btnmatrix_get_selected_btn(buttons);
    const auto id = app->ui_.handle(dialog);
    std::string command;
    for (const auto &map : app->button_maps_) if (map.object == buttons && selected < map.commands.size()) {
        command = map.commands[selected];
        break;
    }
    if (!command.empty()) (void)app->dispatch_action(command);
    // The callback may already have removed the dialog or one of its parents.
    (void)app->ui_.remove(id);
}

bool DeclarativeApp::dispatch_ui_action(const void *raw_action)
{
    const auto *action = static_cast<const cJSON *>(raw_action);
    const char *call = string_field(action, "call");
    const char *target = string_field(action, "target");
    if (!call || !target) return false;
    const auto id = widget(target);
    if (!id) return false;
    const auto *value = field(action, "value");
    if (!strcmp(call, "ui.set_text")) return cJSON_IsString(value) && ui_.set_text(id, value->valuestring);
    if (!strcmp(call, "ui.set_hidden")) return cJSON_IsBool(value) && ui_.set_hidden(id, cJSON_IsTrue(value));
    if (!strcmp(call, "ui.set_enabled")) return cJSON_IsBool(value) && ui_.set_enabled(id, cJSON_IsTrue(value));
    if (!strcmp(call, "ui.set_style")) return ui_.set_style(id, value);
    if (!strcmp(call, "ui.set_focus")) return ui_.set_focus(id);
    if (!strcmp(call, "ui.delete")) return ui_.remove(id);
    if (!strcmp(call, "ui.list_clear")) return ui_.list_clear(id);
    if (!strcmp(call, "ui.chart_clear")) return ui_.chart_clear(id);
    if (!strcmp(call, "ui.chart_add_series")) {
        double color = 0;
        return finite_number(value, &color) && color >= 0 && color <= 0xffffff && color == std::floor(color) &&
            ui_.chart_add_series(id, static_cast<uint32_t>(color));
    }
    if (!strcmp(call, "ui.chart_set_range")) {
        double lo = 0, hi = 0;
        return finite_number(field(action, "min"), &lo) && finite_number(field(action, "max"), &hi) &&
            lo >= -32768 && hi <= 32767 && lo < hi && lo == std::floor(lo) && hi == std::floor(hi) &&
            ui_.chart_set_range(id, static_cast<int>(lo), static_cast<int>(hi));
    }
    if (!strcmp(call, "ui.chart_set_data")) {
        if (!cJSON_IsArray(value) || cJSON_GetArraySize(value) > 128) return false;
        double index = 0;
        if (field(action, "series") && (!finite_number(field(action, "series"), &index) || index < 0 || index >= 4 || index != std::floor(index))) return false;
        std::vector<int> values;
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, value) {
            double number = 0;
            if (!finite_number(item, &number) || number < -32768 || number >= 32767 || number != std::floor(number)) return false;
            values.push_back(static_cast<int>(number));
        }
        return ui_.chart_set_data(id, ui_.chart_series(id, static_cast<unsigned>(index)), values);
    }
    if (!strcmp(call, "ui.set_value")) {
        double number = 0;
        return finite_number(value, &number) && number >= -32768 && number <= 32767 &&
            number == std::floor(number) && ui_.set_value(id, static_cast<int>(number));
    }
    if (!strcmp(call, "ui.list_add") || !strcmp(call, "ui.list_insert")) {
        lv_obj_t *list = nullptr;
        for (const auto &named : named_objects_) if (ui_.handle(named.object) == id) { list = named.object; break; }
        unsigned count = 0;
        if (!list || !ui_.list_count(id, &count)) return false;
        if (!strcmp(call, "ui.list_add")) return insert_list_item(list, count, value);
        double index = 0;
        return finite_number(field(action, "index"), &index) && index >= 0 && index <= 64 && index == std::floor(index) &&
            insert_list_item(list, static_cast<unsigned>(index), value);
    }
    if (!strcmp(call, "ui.list_remove")) {
        double index = 0;
        return finite_number(field(action, "index"), &index) && index >= 0 && index < 64 && index == std::floor(index) &&
            ui_.list_remove(id, static_cast<unsigned>(index));
    }
    if (!strcmp(call, "ui.scroll_to"))
        return ui_.scroll_to(id, boolean_field(action, "animate", false), string_field(action, "align") ? string_field(action, "align") : "center");
    if (!strcmp(call, "ui.set_parent")) return ui_.set_parent(id, widget(string_field(action, "parent")));
    return false;
}

bool DeclarativeApp::dispatch_action(const std::string &raw_command)
{
    const std::string command = trim(raw_command);
    if (!command.empty() && command.front() == '{') {
        Json action(cJSON_Parse(command.c_str()), cJSON_Delete);
        return action && dispatch_ui_action(action.get());
    }
    const bool timer_action = command.rfind("timer.", 0) == 0 || command.rfind("crystal.service.timer.", 0) == 0;
    if (timer_action && !system_timer_allowed_) {
        ESP_LOGW(TAG, "%s: timer action rejected; package lacks system_timer permission", package_id_.c_str());
        return false;
    }
    if (command == "timer.pause" || command == "crystal.service.timer.pause()") return crystal_timer_pause();
    if (command == "timer.resume" || command == "crystal.service.timer.resume()") return crystal_timer_resume();
    if (command == "timer.reset" || command == "crystal.service.timer.reset()") { crystal_timer_reset(); return true; }
    if (command.rfind("timer.arm(", 0) == 0 || command.rfind("crystal.service.timer.arm(", 0) == 0) {
        uint32_t seconds = 0;
        if (!parse_positive(quoted_action_argument(command), &seconds) || seconds == 0 || seconds > 86400) return false;
        return crystal_timer_start(seconds);
    }
    ESP_LOGW(TAG, "%s: rejected unsupported declarative action '%s'", package_id_.c_str(), command.c_str());
    return false;
}

std::string DeclarativeApp::resolve_expression(const std::string &expression) const
{
    std::string value = expression;
    size_t start = 0;
    while ((start = value.find("{{", start)) != std::string::npos) {
        const size_t end = value.find("}}", start + 2);
        if (end == std::string::npos) break;
        const std::string token = trim(value.substr(start + 2, end - start - 2));
        std::string path = token;
        std::string format;
        std::string replacement;
        const size_t equal = token.find("==");
        const size_t unequal = token.find("!=");
        const size_t comparison = equal != std::string::npos ? equal : unequal;
        if (comparison != std::string::npos) {
            path = trim(token.substr(0, comparison));
            std::string expected = trim(token.substr(comparison + 2));
            if (expected.size() >= 2 && ((expected.front() == '\'' && expected.back() == '\'') ||
                                         (expected.front() == '"' && expected.back() == '"'))) {
                expected = expected.substr(1, expected.size() - 2);
            }
            std::string actual;
            bool known = true;
            if (path == "network.status.state" || path == "crystal.service.net.status.state")
                actual = crystal_network_has_ip() ? "connected" : "disconnected";
            else if (path == "timer.running" || path == "crystal.service.timer.running")
                actual = crystal_timer_state().running ? "true" : "false";
            else if (path == "timer.paused" || path == "crystal.service.timer.paused")
                actual = crystal_timer_state().paused ? "true" : "false";
            else known = false;
            if (!known) ESP_LOGW(TAG, "%s: unsupported condition '%s'", package_id_.c_str(), token.c_str());
            const bool matches = known && (equal != std::string::npos
                ? actual == expected : actual != expected);
            replacement = matches ? "true" : "false";
        } else {
            const size_t pipe = token.find("| format");
            if (pipe != std::string::npos) {
                path = trim(token.substr(0, pipe));
                format = trim(token.substr(pipe + 8));
                if (format.size() >= 2 && format.front() == '\'' && format.back() == '\'')
                    format = format.substr(1, format.size() - 2);
            }
            if (path == "time.now" || path == "crystal.service.time.now") {
                time_t now = time(nullptr); struct tm local{}; localtime_r(&now, &local);
                char buffer[32] = {};
                strftime(buffer, sizeof(buffer), format.empty() ? "%H:%M:%S" : format.c_str(), &local);
                replacement = buffer;
            } else if (path == "network.status.state" || path == "crystal.service.net.status.state") {
                replacement = crystal_network_has_ip() ? "connected" : "disconnected";
            } else if (path == "timer.remaining" || path == "crystal.service.timer.remaining") {
                replacement = std::to_string(crystal_timer_state().remaining_seconds);
            } else if (path == "timer.running" || path == "crystal.service.timer.running") {
                replacement = crystal_timer_state().running ? "true" : "false";
            } else if (path == "timer.paused" || path == "crystal.service.timer.paused") {
                replacement = crystal_timer_state().paused ? "true" : "false";
            } else {
                ESP_LOGW(TAG, "%s: unknown binding '%s'", package_id_.c_str(), path.c_str());
                replacement = "";
            }
        }
        value.replace(start, end + 2 - start, replacement);
        start += replacement.size();
    }
    return value;
}

void DeclarativeApp::refresh_bindings()
{
    for (const auto &binding : bindings_) {
        if (binding.object == nullptr || !lv_obj_is_valid(binding.object)) continue;
        const std::string value = resolve_expression(binding.expression);
        if (binding.property == Binding::Property::Visible) {
            (void)ui_.set_hidden(ui_.handle(binding.object), value != "true");
        } else {
            lv_label_set_text(binding.object, value.c_str());
        }
    }
}

void DeclarativeApp::binding_tick(lv_timer_t *timer)
{
    auto *app = static_cast<DeclarativeApp *>(timer != nullptr ? timer->user_data : nullptr);
    if (app != nullptr) app->refresh_bindings();
}
