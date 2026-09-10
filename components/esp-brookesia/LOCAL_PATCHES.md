# Crystal OS local override

This directory vendors ESP-Brookesia 0.4.2 from the ESP-IDF Component Registry.
It is a project component so ESP-IDF selects it ahead of `managed_components/`.

Crystal disables Brookesia's Recents screen. The local manager source treats a
missing Recents object as a supported configuration in app-close, home-screen,
navigation, and gesture paths. Keep those null guards when updating the vendored
component, then rerun the clean-build and side-switch checks documented in
`docs/PHASE_11_SETTINGS.md`.

The upstream component remains licensed under Apache-2.0; see `license.txt`.
