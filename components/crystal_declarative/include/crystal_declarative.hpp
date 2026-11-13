/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#pragma once

#include <stdint.h>
#include <cstddef>
#include <deque>
#include <list>
#include <string>
#include <vector>

#include "crystal_app.hpp"
#include "crystal_ui.hpp"
#include "lvgl.h"

// Phase 16's package-backed runtime. It intentionally exposes no LVGL
// pointers to package content: the document creates an internal handle tree
// owned by this app and all handles die with onDestroy().
class DeclarativeApp final : public CrystalApp {
public:
    DeclarativeApp(const char *name, const char *package_id, const char *entry, bool system_timer_allowed);
    CrystalUI &ui() { return ui_; }
    CrystalUI::Handle widget(const char *id) const;

protected:
    bool onCreate() override;
    bool onPause() override;
    bool onResume() override;
    bool onDestroy() override;

private:
    struct Binding {
        enum class Property : uint8_t { Text, Visible };
        lv_obj_t *object = nullptr;
        std::string expression;
        Property property = Property::Text;
    };
    struct NamedObject {
        lv_obj_t *object = nullptr;
        lv_obj_t *text_object = nullptr;
        std::string id;
    };
    struct Action {
        DeclarativeApp *app = nullptr;
        std::string command;
        lv_obj_t *object = nullptr;
    };
    struct EmbeddedImage {
        std::string path;
        lv_img_dsc_t descriptor{};
        std::vector<uint8_t> pixels;
    };
    struct ButtonMap {
        lv_obj_t *object = nullptr;
        std::vector<std::string> labels, commands;
        std::vector<const char *> pointers;
    };

    static void binding_tick(lv_timer_t *timer);
    static void action_event(lv_event_t *event);
    static void dialog_event(lv_event_t *event);
    static void node_deleted(lv_event_t *event);
    bool load_document();
    bool load_bindings(const std::string &text);
    bool start_binding_timer();
    void stop_binding_timer();
    lv_obj_t *build_node(const void *node, lv_obj_t *parent, unsigned depth);
    bool apply_node_options(lv_obj_t *object, const void *node, bool interactive);
    static bool apply_style(lv_obj_t *object, const void *style);
    bool apply_action(lv_obj_t *object, const char *command, lv_event_code_t event);
    bool insert_list_item(lv_obj_t *list, unsigned index, const void *item);
    bool load_image(lv_obj_t *object, const char *path);
    void refresh_bindings();
    std::string resolve_expression(const std::string &expression) const;
    bool dispatch_action(const std::string &command);
    bool dispatch_ui_action(const void *action);
    void reset_runtime();

    CrystalUI ui_{resources()};
    std::string package_id_;
    std::string entry_;
    bool system_timer_allowed_ = false;
    lv_obj_t *root_ = nullptr;
    lv_timer_t *binding_timer_ = nullptr;
    CrystalResources::Handle binding_timer_handle_ = 0;
    std::vector<Binding> bindings_;
    std::list<Action> actions_;
    std::list<ButtonMap> button_maps_;
    std::deque<EmbeddedImage> images_;
    size_t image_bytes_ = 0;
    std::vector<NamedObject> named_objects_;
    unsigned node_count_ = 0;
};
