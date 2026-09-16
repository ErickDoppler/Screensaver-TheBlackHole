/* Windows implementation of platform.h: registry store, the screensaver
 * preview child window, and the settings dialog. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdarg.h>
#include "platform.h"
#include "settings.h"
#include "resource.h"

/* ------------------------------------------------------------------ log -- */
static FILE *g_log;

void plat_log_set_file(const char *path) {
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
}

void plat_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

/* ------------------------------------------------------------- registry -- */
static const wchar_t *const REG_PATH = L"Software\\TheBlackHole";

static void to_wide(const char *s, wchar_t *out, int cap) {
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
}

int plat_store_read_int(const char *key, int *out) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD type = 0, val = 0, size = sizeof val;
    LSTATUS st = RegQueryValueExW(h, wk, NULL, &type, (BYTE *)&val, &size);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS || type != REG_DWORD) return 0;
    *out = (int)val;
    return 1;
}

int plat_store_write_int(const char *key, int value) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0, KEY_WRITE, NULL, &h, NULL) != ERROR_SUCCESS)
        return 0;
    wchar_t wk[128];
    to_wide(key, wk, 128);
    DWORD val = (DWORD)value;
    LSTATUS st = RegSetValueExW(h, wk, 0, REG_DWORD, (const BYTE *)&val, sizeof val);
    RegCloseKey(h);
    return st == ERROR_SUCCESS;
}

/* -------------------------------------------------------------- windows -- */
void plat_win32_virtual_screen(int *x, int *y, int *w, int *h) {
    *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (*w <= 0 || *h <= 0) { *x = 0; *y = 0; *w = 1280; *h = 720; }
}

static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    return DefWindowProcW(h, m, wp, lp);
}

void *plat_win32_create_preview_child(void *parent_hwnd, int *w, int *h) {
    HWND parent = (HWND)parent_hwnd;
    if (!IsWindow(parent)) return NULL;
    RECT rc;
    GetClientRect(parent, &rc);
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"TheBlackHolePreview";
    RegisterClassW(&wc);
    HWND child = CreateWindowExW(0, wc.lpszClassName, L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, rc.right, rc.bottom, parent, NULL, wc.hInstance, NULL);
    *w = rc.right > 0 ? rc.right : 1;
    *h = rc.bottom > 0 ? rc.bottom : 1;
    return child;
}

int plat_win32_window_alive(void *hwnd) { return IsWindow((HWND)hwnd) ? 1 : 0; }

/* -------------------------------------------------------- settings dialog -- */
static Settings g_s;
/* The scene picker ships hidden. Ctrl+Alt+S reveals it and grows the dialog. */
static int g_scenes_shown;
static int g_done;
static INT_PTR g_result;

/* How much taller the dialog gets, and how far the buttons move, when the
 * scene section is revealed. Dialog units, matching the .rc. */
#define SCENES_EXTRA_DU 84

static HWND item(HWND dlg, int id) { return GetDlgItem(dlg, id); }

static void set_slider(HWND dlg, int id, int mn, int mx, int pos) {
    HWND t = item(dlg, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(t, TBM_SETTICFREQ, (mx - mn) / 10, 0);
    SendMessageW(t, TBM_SETPAGESIZE, 0, (mx - mn) / 10);
    SendMessageW(t, TBM_SETPOS, TRUE, pos);
}
static int  get_slider(HWND dlg, int id) { return (int)SendMessageW(item(dlg, id), TBM_GETPOS, 0, 0); }
static void set_check(HWND dlg, int id, int v) { CheckDlgButton(dlg, id, v ? BST_CHECKED : BST_UNCHECKED); }
static int  get_check(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static void set_text(HWND dlg, int id, const wchar_t *text) { SetDlgItemTextW(dlg, id, text); }

static void set_num(HWND dlg, int id, const wchar_t *suffix, int v) {
    wchar_t b[64];
    _snwprintf(b, 63, L"%d%s", v, suffix ? suffix : L"");
    b[63] = 0;
    set_text(dlg, id, b);
}

static void refresh_labels(HWND dlg) {
    wchar_t b[64];
    set_num(dlg, IDC_SPEED_VAL, L" %", g_s.movement_speed);
    set_num(dlg, IDC_SENS_VAL, L" %", g_s.mouse_sensitivity);
    set_num(dlg, IDC_ROTSEC_VAL, L" s", g_s.rotate_seconds);
    if (g_s.warp_minutes == 0) set_text(dlg, IDC_WARP_VAL, L"off");
    else set_num(dlg, IDC_WARP_VAL, L" min", g_s.warp_minutes);
    set_num(dlg, IDC_STARS_VAL, L"", g_s.star_count);
    set_num(dlg, IDC_PSIZE_VAL, L" px", g_s.particle_size);
    set_num(dlg, IDC_GHOST_VAL, L" %", g_s.ghost_tail);
    set_num(dlg, IDC_BLUR_VAL, L" %", g_s.blur);
    set_num(dlg, IDC_TWINKLE_VAL, L" %", g_s.twinkle);
    /* Spin is a fraction of the most a hole can have, lensing a multiple of
     * the truth. Both read better as the physical quantity than as a
     * percentage of a slider. */
    _snwprintf(b, 63, L"%d.%03d", g_s.spin / 1000, g_s.spin % 1000); b[63] = 0;
    set_text(dlg, IDC_SPIN_VAL, b);
    _snwprintf(b, 63, L"%d.%02dx", g_s.lensing / 100, g_s.lensing % 100); b[63] = 0;
    set_text(dlg, IDC_LENSING_VAL, b);
    set_num(dlg, IDC_TIMEDIL_VAL, L" %", g_s.time_dilation);
    if (g_s.damage_grace == 0) set_text(dlg, IDC_DMG_GRACE_VAL, L"none");
    else set_num(dlg, IDC_DMG_GRACE_VAL, L" min", g_s.damage_grace);
    set_num(dlg, IDC_DMG_HEAL_VAL, L" s", g_s.damage_heal);
    if (g_s.quality == 0) set_text(dlg, IDC_QUALITY_VAL, L"auto");
    else set_num(dlg, IDC_QUALITY_VAL, L" %", g_s.quality);
    set_num(dlg, IDC_FPS_VAL, L" fps", g_s.target_fps);
}

/* Show/enable rules:
 *  - movement speed only matters with side movement on;
 *  - "Exit on mouse move" and its children appear only when the mouse does
 *    not rotate the view; the children are enabled only when it is checked;
 *  - the 360 mode and duration follow the 360 checkbox;
 *  - the damage controls follow the damage checkbox, and the custom colour
 *    follows the Custom palette radio. */
static void enable_group(HWND dlg, const int *ids, int n, int on) {
    for (int i = 0; i < n; ++i) EnableWindow(item(dlg, ids[i]), on);
}

static void refresh_dependencies(HWND dlg) {
    static const int speed_group[] = { IDC_SPEED_LBL, IDC_SPEED, IDC_SPEED_VAL };
    enable_group(dlg, speed_group, 3, get_check(dlg, IDC_SIDE));

    int rot = get_check(dlg, IDC_MOUSEROT);
    int show = rot ? SW_HIDE : SW_SHOW;
    static const int mouse_group[] = { IDC_EXITMOUSE, IDC_SENS_LBL, IDC_SENS,
                                       IDC_SENS_VAL, IDC_CLICKRESET };
    for (int i = 0; i < 5; ++i) ShowWindow(item(dlg, mouse_group[i]), show);
    int em = get_check(dlg, IDC_EXITMOUSE);
    static const int sens_group[] = { IDC_SENS_LBL, IDC_SENS, IDC_SENS_VAL, IDC_CLICKRESET };
    enable_group(dlg, sens_group, 4, em);

    static const int rot_group[] = { IDC_ROTMODE_LBL, IDC_ROT_ORBIT, IDC_ROT_YAW,
                                     IDC_ROT_TUMBLE, IDC_ROTSEC_LBL, IDC_ROTSEC, IDC_ROTSEC_VAL };
    enable_group(dlg, rot_group, 7, get_check(dlg, IDC_ROT360));

    int dmg = get_check(dlg, IDC_DAMAGE);
    static const int dmg_group[] = { IDC_DMG_GRACE_LBL, IDC_DMG_GRACE, IDC_DMG_GRACE_VAL,
                                     IDC_DMG_HEAL_LBL, IDC_DMG_HEAL, IDC_DMG_HEAL_VAL,
                                     IDC_DMG_GLASS, IDC_DMG_MATRIX, IDC_DMG_PAL_LBL,
                                     IDC_DMG_WHITE, IDC_DMG_GREEN, IDC_DMG_BLACK, IDC_DMG_CUSTOM };
    enable_group(dlg, dmg_group, 13, dmg);
    EnableWindow(item(dlg, IDC_DMGCOL), dmg && get_check(dlg, IDC_DMG_CUSTOM));

    /* The post-merger hole only exists if mergers are allowed to happen. */
    EnableWindow(item(dlg, IDC_SCENE_FIRST + SCENE_AFTERMATH), get_check(dlg, IDC_EV_MERGER));
}

static const int color_ctls[] = { IDC_STARCOL, IDC_SPACECOL, IDC_DISKCOL,
                                  IDC_RINGCOL, IDC_SHADOWCOL, IDC_NEBULACOL, IDC_DMGCOL };

static void invalidate_colors(HWND dlg) {
    for (int i = 0; i < (int)(sizeof color_ctls / sizeof color_ctls[0]); ++i)
        InvalidateRect(item(dlg, color_ctls[i]), NULL, TRUE);
}

static unsigned *color_for_ctl(int id) {
    switch (id) {
    case IDC_STARCOL:   return &g_s.star_color;
    case IDC_SPACECOL:  return &g_s.space_color;
    case IDC_DISKCOL:   return &g_s.disk_color;
    case IDC_RINGCOL:   return &g_s.ring_color;
    case IDC_SHADOWCOL: return &g_s.shadow_color;
    case IDC_NEBULACOL: return &g_s.nebula_color;
    case IDC_DMGCOL:    return &g_s.damage_color;
    default:            return NULL;
    }
}

static void settings_to_controls(HWND dlg) {
    set_check(dlg, IDC_SIDE, g_s.side_movement);
    set_slider(dlg, IDC_SPEED, 0, 100, g_s.movement_speed);
    set_check(dlg, IDC_MOUSEROT, g_s.mouse_rotation);
    set_check(dlg, IDC_EXITMOUSE, g_s.exit_on_mouse_move);
    set_slider(dlg, IDC_SENS, 0, 100, g_s.mouse_sensitivity);
    set_check(dlg, IDC_CLICKRESET, g_s.click_resets_view);
    set_check(dlg, IDC_EXITKEY, g_s.exit_on_any_key);
    set_check(dlg, IDC_ROT360, g_s.rotate_360);
    CheckRadioButton(dlg, IDC_ROT_ORBIT, IDC_ROT_TUMBLE, IDC_ROT_ORBIT + g_s.rotate_mode);
    set_slider(dlg, IDC_ROTSEC, 3, 999, g_s.rotate_seconds);
    set_slider(dlg, IDC_WARP, 0, 120, g_s.warp_minutes);

    set_slider(dlg, IDC_STARS, 1000, 20000, g_s.star_count);
    set_slider(dlg, IDC_PSIZE, 1, 50, g_s.particle_size);
    set_slider(dlg, IDC_GHOST, 0, 100, g_s.ghost_tail);
    set_slider(dlg, IDC_BLUR, 0, 100, g_s.blur);
    set_slider(dlg, IDC_TWINKLE, 0, 100, g_s.twinkle);
    set_check(dlg, IDC_NATURAL, g_s.natural_star_color);

    set_slider(dlg, IDC_SPIN, 0, 999, g_s.spin);
    set_slider(dlg, IDC_LENSING, 50, 300, g_s.lensing);
    set_slider(dlg, IDC_TIMEDIL, 0, 100, g_s.time_dilation);
    set_check(dlg, IDC_DOPPLER, g_s.doppler == DOPPLER_TRUE);
    set_check(dlg, IDC_REDSHIFT, g_s.redshift);
    set_check(dlg, IDC_HIGHORDER, g_s.higher_order);

    set_check(dlg, IDC_JETS, g_s.jets);
    set_check(dlg, IDC_EV_MERGER, g_s.event_merger);
    set_check(dlg, IDC_EV_FALLIN, g_s.event_fallin);
    set_check(dlg, IDC_EV_FLARE, g_s.event_flare);

    set_check(dlg, IDC_DAMAGE, g_s.damage);
    set_slider(dlg, IDC_DMG_GRACE, 0, 30, g_s.damage_grace);
    set_slider(dlg, IDC_DMG_HEAL, 5, 300, g_s.damage_heal);
    set_check(dlg, IDC_DMG_GLASS, g_s.damage_glass);
    set_check(dlg, IDC_DMG_MATRIX, g_s.damage_matrix);
    CheckRadioButton(dlg, IDC_DMG_WHITE, IDC_DMG_CUSTOM, IDC_DMG_WHITE + g_s.damage_palette);

    set_slider(dlg, IDC_QUALITY, 0, 100, g_s.quality);
    set_slider(dlg, IDC_FPS, 10, 120, g_s.target_fps);

    for (int i = 0; i < SCENE_COUNT; ++i)
        set_check(dlg, IDC_SCENE_FIRST + i, settings_scene_enabled(&g_s, i));

    refresh_labels(dlg);
    refresh_dependencies(dlg);
    invalidate_colors(dlg);
}

static void controls_to_settings(HWND dlg) {
    g_s.side_movement      = get_check(dlg, IDC_SIDE);
    g_s.movement_speed     = get_slider(dlg, IDC_SPEED);
    g_s.mouse_rotation     = get_check(dlg, IDC_MOUSEROT);
    g_s.exit_on_mouse_move = get_check(dlg, IDC_EXITMOUSE);
    g_s.mouse_sensitivity  = get_slider(dlg, IDC_SENS);
    g_s.click_resets_view  = get_check(dlg, IDC_CLICKRESET);
    g_s.exit_on_any_key    = get_check(dlg, IDC_EXITKEY);
    g_s.rotate_360         = get_check(dlg, IDC_ROT360);
    g_s.rotate_mode = get_check(dlg, IDC_ROT_TUMBLE) ? ROT_TUMBLE
                    : get_check(dlg, IDC_ROT_YAW) ? ROT_YAW : ROT_ORBIT;
    g_s.rotate_seconds     = get_slider(dlg, IDC_ROTSEC);
    g_s.warp_minutes       = get_slider(dlg, IDC_WARP);

    g_s.star_count         = get_slider(dlg, IDC_STARS);
    g_s.particle_size      = get_slider(dlg, IDC_PSIZE);
    g_s.ghost_tail         = get_slider(dlg, IDC_GHOST);
    g_s.blur               = get_slider(dlg, IDC_BLUR);
    g_s.twinkle            = get_slider(dlg, IDC_TWINKLE);
    g_s.natural_star_color = get_check(dlg, IDC_NATURAL);

    g_s.spin               = get_slider(dlg, IDC_SPIN);
    g_s.lensing            = get_slider(dlg, IDC_LENSING);
    g_s.time_dilation      = get_slider(dlg, IDC_TIMEDIL);
    g_s.doppler            = get_check(dlg, IDC_DOPPLER) ? DOPPLER_TRUE : DOPPLER_INTERSTELLAR;
    g_s.redshift           = get_check(dlg, IDC_REDSHIFT);
    g_s.higher_order       = get_check(dlg, IDC_HIGHORDER);

    g_s.jets               = get_check(dlg, IDC_JETS);
    g_s.event_merger       = get_check(dlg, IDC_EV_MERGER);
    g_s.event_fallin       = get_check(dlg, IDC_EV_FALLIN);
    g_s.event_flare        = get_check(dlg, IDC_EV_FLARE);

    g_s.damage             = get_check(dlg, IDC_DAMAGE);
    g_s.damage_grace       = get_slider(dlg, IDC_DMG_GRACE);
    g_s.damage_heal        = get_slider(dlg, IDC_DMG_HEAL);
    g_s.damage_glass       = get_check(dlg, IDC_DMG_GLASS);
    g_s.damage_matrix      = get_check(dlg, IDC_DMG_MATRIX);
    g_s.damage_palette = get_check(dlg, IDC_DMG_CUSTOM) ? DMG_CUSTOM
                       : get_check(dlg, IDC_DMG_BLACK) ? DMG_BLACK
                       : get_check(dlg, IDC_DMG_GREEN) ? DMG_GREEN_PURPLE : DMG_WHITE;

    g_s.quality            = get_slider(dlg, IDC_QUALITY);
    g_s.target_fps         = get_slider(dlg, IDC_FPS);

    for (int i = 0; i < SCENE_COUNT; ++i)
        settings_set_scene(&g_s, i, get_check(dlg, IDC_SCENE_FIRST + i));
    settings_clamp(&g_s);
}

static COLORREF to_colorref(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static unsigned from_colorref(COLORREF c) {
    return ((unsigned)GetRValue(c) << 16) | ((unsigned)GetGValue(c) << 8) | GetBValue(c);
}

static void pick_color(HWND dlg, int ctl) {
    static COLORREF custom[16];
    unsigned *rgb = color_for_ctl(ctl);
    if (!rgb) return;
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = dlg;
    cc.lpCustColors = custom;
    cc.rgbResult = to_colorref(*rgb);
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) {
        *rgb = from_colorref(cc.rgbResult);
        InvalidateRect(item(dlg, ctl), NULL, TRUE);
    }
}

/* Reveals (or hides again) the scene section, growing the dialog and taking
 * the buttons down with it. */
static void toggle_scenes(HWND dlg) {
    g_scenes_shown = !g_scenes_shown;
    int show = g_scenes_shown ? SW_SHOW : SW_HIDE;
    ShowWindow(item(dlg, IDC_SCENE_BOX), show);
    for (int i = 0; i < SCENE_COUNT; ++i) ShowWindow(item(dlg, IDC_SCENE_FIRST + i), show);
    ShowWindow(item(dlg, IDC_SCENE_ALL), show);
    ShowWindow(item(dlg, IDC_SCENE_NONE), show);

    RECT du = { 0, 0, 4, SCENES_EXTRA_DU };
    MapDialogRect(dlg, &du);
    int dy = g_scenes_shown ? du.bottom : -du.bottom;

    static const int buttons[] = { IDC_DEFAULTS, IDC_PREVIEW, IDOK, IDCANCEL };
    for (int i = 0; i < 4; ++i) {
        HWND h = item(dlg, buttons[i]);
        RECT rc;
        GetWindowRect(h, &rc);
        MapWindowPoints(NULL, dlg, (POINT *)&rc, 2);
        SetWindowPos(h, NULL, rc.left, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    RECT wr;
    GetWindowRect(dlg, &wr);
    SetWindowPos(dlg, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top + dy,
                 SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(dlg, NULL, TRUE);
}

/* The dialog is closed by setting a result; the message loop in
 * plat_win32_config_dialog notices and tears the window down. */
static void finish(INT_PTR result) {
    g_result = result;
    g_done = 1;
}

/* Ctrl+Alt+S, read from this thread's own keyboard messages. Whichever
 * control has the focus would swallow the key before the dialog procedure saw
 * it, so the dialog runs its own loop and looks at each message first. The
 * key state comes from GetKeyState, which reports what this thread's queue has
 * already delivered; polling the global keyboard instead (GetAsyncKeyState on
 * a timer) is what keyloggers do, and antivirus heuristics score it as one. */
static int is_scene_chord(const MSG *m) {
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return 0;
    if (m->wParam != 'S' || (m->lParam & (1L << 30))) return 0;   /* no autorepeat */
    return (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000);
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)icon);
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        }
        settings_load(&g_s);
        g_scenes_shown = 0;
        settings_to_controls(dlg);
        return TRUE;
    }
    case WM_HSCROLL:
        controls_to_settings(dlg);
        refresh_labels(dlg);
        return TRUE;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
        const unsigned *c = color_for_ctl((int)d->CtlID);
        if (!c) break;
        HBRUSH b = CreateSolidBrush(to_colorref(*c));
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        FrameRect(d->hDC, &d->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));
        if (d->itemState & ODS_FOCUS) DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_SCENE_FIRST && id <= IDC_SCENE_LAST) {
            controls_to_settings(dlg);
            return TRUE;
        }
        switch (id) {
        case IDOK:
            controls_to_settings(dlg);
            settings_save(&g_s);
            finish(IDOK);
            return TRUE;
        case IDCANCEL:
            finish(IDCANCEL);
            return TRUE;
        case IDC_DEFAULTS:
            settings_defaults(&g_s);
            settings_to_controls(dlg);
            return TRUE;
        case IDC_PREVIEW:
            /* Save, then hand back to the caller, which runs the preview in
             * this same process: a program relaunching itself looks
             * suspicious to antivirus heuristics. */
            controls_to_settings(dlg);
            settings_save(&g_s);
            finish(IDC_PREVIEW);
            return TRUE;
        case IDC_SCENE_ALL:
        case IDC_SCENE_NONE: {
            int on = id == IDC_SCENE_ALL;
            for (int i = 0; i < SCENE_COUNT; ++i) set_check(dlg, IDC_SCENE_FIRST + i, on);
            controls_to_settings(dlg);
            settings_to_controls(dlg);
            return TRUE;
        }
        case IDC_STARCOL: case IDC_SPACECOL: case IDC_DISKCOL:
        case IDC_RINGCOL: case IDC_SHADOWCOL: case IDC_NEBULACOL: case IDC_DMGCOL:
            pick_color(dlg, id);
            return TRUE;
        default:
            /* Everything else is a checkbox or a radio that may change what is
             * enabled around it. */
            if (HIWORD(wp) == BN_CLICKED) {
                controls_to_settings(dlg);
                refresh_dependencies(dlg);
                return TRUE;
            }
            break;
        }
        break;
    }
    case WM_CLOSE:
        finish(IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

int plat_win32_config_dialog(void *parent_hwnd) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    HWND parent = IsWindow((HWND)parent_hwnd) ? (HWND)parent_hwnd : NULL;

    /* Modal by hand: a modeless dialog with the owner disabled, so the loop
     * below gets to see the keyboard before the focused control does. */
    g_done = 0;
    g_result = IDCANCEL;
    HWND dlg = CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CONFIG),
                                  parent, dlg_proc, 0);
    if (!dlg) {
        plat_log("settings dialog failed to open: error %lu", (unsigned long)GetLastError());
        return 0;
    }
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);

    MSG m;
    int quit = 0;
    while (!g_done) {
        BOOL got = GetMessageW(&m, NULL, 0, 0);
        if (got == 0) { quit = 1; break; }
        if (got == -1) break;
        if (is_scene_chord(&m)) { toggle_scenes(dlg); continue; }
        if (!IsDialogMessageW(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    /* Re-enable the owner before the dialog goes, or Windows hands the
     * activation to some other application's window. */
    if (parent) EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    if (parent) SetForegroundWindow(parent);
    if (quit) PostQuitMessage((int)m.wParam);
    return g_result == IDC_PREVIEW ? 1 : 0;
}
