/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
// Build the shell's real Quick Settings panel into the host capture executable.
#include "../../components/crystal_shell/src/crystal_shell.cpp"

extern "C" bool crystal_capture_quick_settings(ESP_Brookesia_Phone *phone)
{
    if (phone == nullptr) return false;
    s_phone = phone;
    if (!create_quick_settings()) return false;
    lv_obj_set_y(s_quick_panel, s_quick_y_rest);
    lv_anim_t animation{};
    quick_anim_ready(&animation);
    return s_quick_settings_open && s_quick_catcher != nullptr;
}
