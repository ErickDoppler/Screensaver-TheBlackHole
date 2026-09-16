/* Platform services used by the portable core. Implemented per OS. */
#ifndef BH_PLATFORM_H
#define BH_PLATFORM_H

/* Persistent settings store (Windows: HKCU\Software\TheBlackHole). */
int plat_store_read_int(const char *key, int *out);   /* 1 if the key existed */
int plat_store_write_int(const char *key, int value); /* 1 on success */

/* Diagnostics. Goes to the debugger/stderr and, if enabled, a log file. */
void plat_log_set_file(const char *path);
void plat_log(const char *fmt, ...);

#ifdef _WIN32
/* Bounding box of all monitors, in virtual-screen pixels. */
void  plat_win32_virtual_screen(int *x, int *y, int *w, int *h);
/* Creates a child HWND filling `parent` (the little monitor in the Windows
 * screensaver dialog). Returns the child HWND and its size in pixels. */
void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h);
int   plat_win32_window_alive(void *hwnd);
/* Runs the modal settings dialog (the /c mode). Returns 0 when the dialog has
 * dealt with everything, or 1 if it wants the caller to run in-process after
 * it closes (the live preview). */
int   plat_win32_config_dialog(void *parent_hwnd);
#endif
#endif
