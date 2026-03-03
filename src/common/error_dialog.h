#pragma once
// Native OS error dialog for fatal errors that occur before/after the GUI is available.
// Always logs to stderr. Shows a native message box when possible.

void show_error_dialog(const char* title, const char* message);
