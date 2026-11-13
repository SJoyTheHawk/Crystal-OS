/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Runs against production renderer/facade, real LVGL, ASan and UBSan.
#include "cJSON.h"
#include "crystal_shell.hpp"
static bool test_keyboard_open = false;
void crystal_shell_set_keyboard_open(bool open) { test_keyboard_open = open; }
static void count_ui_callback(CrystalUI::Handle, void *user) { ++*static_cast<unsigned *>(user); }
static void check_ui_facade(DeclarativeApp &app)
{
    auto &ui = app.ui();
    CrystalUI::set_focus_hooks(crystal_keyboard_show, crystal_keyboard_hide_for);
    // Exercise the JSON button action, including the opening click dispatch.
    // Calling ui.set_focus directly misses dismissal of that same click.
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        click_button_text(lv_disp_get_default(), "Focus input"); settle();
        assert(crystal_keyboard_is_open() && test_keyboard_open);
        click_button_text(lv_disp_get_default(), "Focus input"); settle();
        assert(crystal_keyboard_is_open());
        auto *keyboard = find_object_type(lv_layer_top(), &lv_keyboard_class); assert(keyboard);
        assert(ui.set_text(app.widget("sample_input"), ""));
        lv_btnmatrix_set_selected_btn(keyboard, 0);
        lv_event_send(keyboard, LV_EVENT_VALUE_CHANGED, nullptr); settle();
        std::string typed;
        assert(crystal_keyboard_is_open() && ui.get_text(app.widget("sample_input"), &typed) && typed == "q");
        if (attempt == 0) {
            unsigned done = 0;
            while (strcmp(lv_btnmatrix_get_btn_text(keyboard, done), LV_SYMBOL_OK)) ++done;
            lv_btnmatrix_set_selected_btn(keyboard, done);
            lv_event_send(keyboard, LV_EVENT_VALUE_CHANGED, nullptr);
        } else {
            click_button_text(lv_disp_get_default(), "Update chart");
        }
        settle();
        assert(!crystal_keyboard_is_open() && !test_keyboard_open);
    }
    CrystalResources other_resources;
    CrystalUI other(other_resources);
    auto root = ui.screen(); assert(root && !other.set_hidden(root, true));
    auto options = [](const char *json) { auto *value = cJSON_Parse(json); assert(value); return value; };
    auto *spec = options(R"({"id":"dynamic_label","text":"Initial","font":"small"})");
    auto label = ui.create("label", root, spec); cJSON_Delete(spec);
    assert(label && app.widget("dynamic_label") == label);
    std::string text; assert(ui.get_text(label, &text) && text == "Initial");
    assert(ui.set_text(label, "Updated") && ui.get_text(label, &text) && text == "Updated");
    assert(!ui.get_text(root, &text) && !ui.set_value(label, 1));
    auto *style = options(R"({"opacity":120,"text_color":16711680,"radius":4})");
    assert(ui.set_style(label, style)); cJSON_Delete(style);
    style = options(R"({"opacity":240,"radius":-1})");
    assert(!ui.set_style(label, style)); cJSON_Delete(style);
    auto *label_object = find_display_label(lv_disp_get_default(), "Updated"); assert(label_object);
    assert(lv_obj_get_style_opa(label_object, 0) == 120); // Invalid style is atomic.
    unsigned clicks = 0, deleted = 0;
    assert(ui.on(label, "click", count_ui_callback, &clicks));
    lv_event_send(label_object, LV_EVENT_CLICKED, nullptr); assert(clicks == 1);
    assert(ui.off(label, "click")); lv_event_send(label_object, LV_EVENT_CLICKED, nullptr); assert(clicks == 1);
    assert(!ui.on(label, "invented_event", count_ui_callback));
    CrystalUI::Size size{}; assert(ui.get_size(label, &size) && size.w > 0 && size.h > 0);
    assert(ui.set_hidden(label, true) && ui.set_hidden(label, false));
    assert(ui.set_enabled(label, false) && !ui.set_focus(label)); assert(ui.set_enabled(label, true));
    assert(ui.scroll_to(label, false, "top") && !ui.scroll_to(label, false, "invalid"));
    auto group = ui.create("col", root); assert(group && ui.set_parent(label, group));
    assert(!ui.set_parent(group, label) && !ui.set_parent(root, group));
    assert(ui.on(label, "delete", count_ui_callback, &deleted));
    assert(ui.remove(group) && deleted == 1 && !ui.set_text(label, "stale") && !app.widget("dynamic_label"));
    for (unsigned i = 0; i < 256; ++i) {
        auto fresh = ui.create("label", root); assert(fresh && fresh != label && ui.remove(fresh));
    }
    // Dialog actions and their retained LVGL button maps die with the widget.
    for (unsigned i = 0; i < 100; ++i) {
        auto *dialog_options = options(R"({"title":"Confirm","message":"Run action?","buttons":[{"text":"OK","on_click":{"call":"ui.set_text","target":"sample_input","value":"Confirmed"}},"Cancel"]})");
        auto dialog = ui.create("dialog", root, dialog_options); cJSON_Delete(dialog_options); assert(dialog);
        auto *title = find_display_label(lv_disp_get_default(), "Confirm"); assert(title);
        auto *object = lv_obj_get_parent(title);
        auto *backdrop = lv_obj_get_parent(object);
        assert(lv_obj_check_type(backdrop, &lv_msgbox_backdrop_class));
        assert(ui.set_hidden(dialog, true) && lv_obj_has_flag(backdrop, LV_OBJ_FLAG_HIDDEN));
        assert(ui.set_hidden(dialog, false) && !lv_obj_has_flag(backdrop, LV_OBJ_FLAG_HIDDEN));
        auto *buttons = lv_msgbox_get_btns(object);
        lv_btnmatrix_set_selected_btn(buttons, 0);
        lv_event_send(buttons, LV_EVENT_VALUE_CHANGED, nullptr);
        assert(!ui.remove(dialog));
        assert(!lv_obj_is_valid(backdrop));
        assert(ui.get_text(app.widget("sample_input"), &text) && text == "Confirmed");
        auto *matrix_options = options(R"({"buttons":["One","Two"]})");
        auto matrix = ui.create("button_matrix", root, matrix_options); cJSON_Delete(matrix_options);
        assert(matrix && ui.remove(matrix));
    }
    auto *add_options = options(R"({"text":"Insert rich row","on_click":{"call":"ui.list_add","target":"sample_list","value":{"primary":"Dynamic action row","secondary":"Tap here","on_click":{"call":"ui.set_text","target":"sample_input","value":"Row clicked"}}}})");
    auto add = ui.create("button", root, add_options); cJSON_Delete(add_options); assert(add);
    unsigned initial_count = 0; assert(ui.list_count(app.widget("sample_list"), &initial_count));
    lv_event_send(lv_obj_get_parent(find_display_label(lv_disp_get_default(), "Insert rich row")), LV_EVENT_CLICKED, nullptr);
    auto *added = find_display_label(lv_disp_get_default(), "Dynamic action row\nTap here"); assert(added);
    lv_event_send(lv_obj_get_parent(added), LV_EVENT_CLICKED, nullptr);
    assert(ui.get_text(app.widget("sample_input"), &text) && text == "Row clicked");
    assert(ui.list_remove(app.widget("sample_list"), initial_count) && ui.remove(add));
    auto input = ui.create("textarea", root); assert(input);
    auto *outside_options = options(R"({"text":"Tap outside"})");
    auto outside_button = ui.create("button", root, outside_options); cJSON_Delete(outside_options); assert(outside_button);
    auto *tab_options = options(R"({"tab_labels":["First","Second"]})");
    auto tabs = ui.create("tabview", root, tab_options); cJSON_Delete(tab_options);
    assert(tabs && ui.tab(tabs, 0) && ui.tab(tabs, 1) && !ui.tab(tabs, 2) && !ui.tab(tabs, UINT32_MAX));
    assert(ui.remove(tabs));
    unsigned focused = 0, blurred = 0, outside_clicks = 0;
    assert(ui.on(input, "focus", count_ui_callback, &focused) && ui.on(input, "blur", count_ui_callback, &blurred));
    assert(ui.on(outside_button, "click", count_ui_callback, &outside_clicks));
    assert(ui.set_focus(input) && crystal_keyboard_is_open() && test_keyboard_open && focused == 1);
    assert(ui.set_text(input, "Keyboard") && ui.get_text(input, &text) && text == "Keyboard");
    ui.pause(); assert(!crystal_keyboard_is_open() && !test_keyboard_open && blurred == 1);
    assert(!ui.set_focus(input)); ui.resume(); assert(ui.set_focus(input));
    auto *outside_object = lv_obj_get_parent(find_display_label(lv_disp_get_default(), "Tap outside"));
    assert(outside_object);
    lv_event_send(outside_object, LV_EVENT_CLICKED, nullptr);
    assert(outside_clicks == 1); settle();
    assert(!crystal_keyboard_is_open() && !test_keyboard_open);
    assert(ui.set_focus(input));
    auto *background = lv_obj_get_parent(outside_object);
    assert(background);
    lv_event_send(background, LV_EVENT_CLICKED, nullptr); settle();
    assert(!crystal_keyboard_is_open() && !test_keyboard_open);
    assert(ui.remove(outside_button));
    assert(ui.remove(input) && !crystal_keyboard_is_open());
    // App teardown must leave an independently owned Settings keyboard alone.
    auto *settings = lv_obj_create(lv_layer_top());
    auto *field = lv_textarea_create(settings);
    assert(crystal_keyboard_show(field, settings)); ui.pause();
    assert(crystal_keyboard_is_open()); crystal_keyboard_hide(); lv_obj_del(settings); ui.resume();
    auto list = ui.create("list", root); assert(list);
    assert(ui.list_add(list, {"First"}));
    assert(ui.list_insert(list, 0, {"Rich", "Subtitle", "assets/weather.cr16", count_ui_callback, &clicks}));
    unsigned count = 99; assert(ui.list_count(list, &count) && count == 2);
    auto *rich = find_display_label(lv_disp_get_default(), "Rich\nSubtitle"); assert(rich);
    lv_event_send(lv_obj_get_parent(rich), LV_EVENT_CLICKED, nullptr); assert(clicks == 2);
    assert(!ui.list_insert(list, 3, {"Bad"}) && !ui.list_remove(list, 2));
    assert(!ui.list_add(list, {"Bad image", nullptr, "../outside"}));
    assert(ui.list_count(list, &count) && count == 2);
    assert(ui.list_remove(list, 0) && ui.list_clear(list) && ui.list_count(list, &count) && count == 0);
    auto chart = ui.create("chart", root); assert(chart);
    auto series = ui.chart_add_series(chart, 0x53c8b6); assert(series);
    assert(ui.chart_set_data(chart, series, {-20, 0, 40}));
    assert(!ui.chart_set_data(chart, series, {32767}) && !ui.chart_set_data(chart, list, {1}));
    assert(!other.chart_set_data(chart, series, {1}));
    assert(ui.chart_set_range(chart, -50, 100) && !ui.chart_set_range(chart, 10, 10));
    assert(ui.chart_clear(chart) && ui.chart_set_data(chart, series, {1, 2, 3}));
    assert(ui.remove(chart) && !ui.chart_set_data(chart, series, {1}));
    assert(ui.remove(list));
    unsigned fired = 0;
    auto timer = ui.timer(10, count_ui_callback, &fired); assert(timer);
    lv_tick_inc(20); lv_timer_handler(); assert(fired == 1);
    ui.pause(); lv_tick_inc(100); lv_timer_handler(); assert(fired == 1);
    ui.resume(); lv_tick_inc(20); lv_timer_handler(); assert(fired == 2);
    assert(ui.timer_delete(timer) && !ui.timer_delete(timer));
    auto once = ui.after(10, count_ui_callback, &fired); assert(once);
    lv_tick_inc(20); lv_timer_handler(); lv_tick_inc(20); lv_timer_handler();
    assert(fired == 3 && !ui.timer_delete(once));
    auto animated = ui.create("label", root); assert(animated);
    auto *props = options(R"({"opacity":0,"radius":10,"text_color":65280,"bg_color":255})");
    unsigned completed = 0;
    auto animation = ui.anim(animated, props, 30, "linear", 0, count_ui_callback, &completed); assert(animation);
    for (unsigned i = 0; i < 10; ++i) { lv_tick_inc(10); lv_timer_handler(); }
    assert(completed == 1 && !ui.anim_cancel(animation));
    animation = ui.anim(animated, props, 1000); assert(animation && ui.anim_cancel(animation));
    animation = ui.anim(animated, props, 1000); assert(animation && ui.remove(animated) && !ui.anim_cancel(animation));
    cJSON_Delete(props);
    // A callback can remove its own object without retaining a live pointer.
    auto self = ui.create("button", root); assert(self);
    assert(ui.on(self, "click", [](CrystalUI::Handle id, void *owner) { assert(static_cast<CrystalUI *>(owner)->remove(id)); }, &ui));
    auto *button_object = lv_obj_get_child(lv_scr_act(), -1);
    // Use the facade's screen root to find its last owned object in the fixture.
    auto *app_root = lv_obj_get_parent(find_display_label(lv_disp_get_default(), "Network: connected"));
    while (lv_obj_get_parent(app_root) != lv_scr_act()) app_root = lv_obj_get_parent(app_root);
    button_object = lv_obj_get_child(app_root, -1);
    lv_event_send(button_object, LV_EVENT_CLICKED, nullptr);
    assert(!ui.remove(self));
    puts("PASS: shared UI operations, stale/cross-app handles, dynamic rich lists/charts, bounded arguments, events, timers/after, animations and teardown");
}
