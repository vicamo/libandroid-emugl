/* Copyright (C) 2006-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <process.h>
#endif
#include "libslirp.h"
#include "sockets.h"

#include "android.h"
#include "vl.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include "math.h"

#include "android.h"
#include "android_charmap.h"
#include "modem_driver.h"
#include "shaper.h"
#include "proxy_http.h"

#include "android_utils.h"
#include "android_resource.h"
#include "android_config.h"

#include "skin_image.h"
#include "skin_trackball.h"
#include "skin_keyboard.h"
#include "skin_file.h"
#include "skin_window.h"
#include "skin_keyset.h"

#include "android_timezone.h"
#include "android_gps.h"
#include "android_qemud.h"
#include "android_kmsg.h"
#include "android_hw_control.h"
#include "android/utils/dirscanner.h"

#include "android_option.h"
#include "android_help.h"
#include "hw/goldfish_nand.h"

#include "android/globals.h"

#include "framebuffer.h"
AndroidRotation  android_framebuffer_rotation;

#define  STRINGIFY(x)   _STRINGIFY(x)
#define  _STRINGIFY(x)  #x

#define  VERSION_STRING  STRINGIFY(ANDROID_VERSION_MAJOR)"."STRINGIFY(ANDROID_VERSION_MINOR)

#define  KEYSET_FILE    "default.keyset"
SkinKeyset*      android_keyset;

#define  D(...)  do {  if (VERBOSE_CHECK(init)) dprint(__VA_ARGS__); } while (0)

extern int  control_console_start( int  port );  /* in control.c */

extern int qemu_milli_needed;

/* the default device DPI if none is specified by the skin
 */
#define  DEFAULT_DEVICE_DPI  165

static const AKeyCharmap*  android_charmap;

int    android_base_port;

#if 0
static int  opts->flashkeys;      /* forward */
#endif

static void  handle_key_command( void*  opaque, SkinKeyCommand  command, int  param );

#ifdef CONFIG_TRACE
extern void  start_tracing(void);
extern void  stop_tracing(void);
#endif

unsigned long   android_verbose;

int   qemu_cpu_delay = 0;
int   qemu_cpu_delay_count;

/***********************************************************************/
/***********************************************************************/
/*****                                                             *****/
/*****            U T I L I T Y   R O U T I N E S                  *****/
/*****                                                             *****/
/***********************************************************************/
/***********************************************************************/

/*** APPLICATION DIRECTORY
 *** Where are we ?
 ***/

const char*  get_app_dir(void)
{
    char  buffer[1024];
    char* p   = buffer;
    char* end = p + sizeof(buffer);
    p = bufprint_app_dir(p, end);
    if (p >= end)
        return NULL;

    return strdup(buffer);
}

/***  CONFIGURATION
 ***/

static AConfig*  emulator_config;
static int       emulator_config_found;
static char      emulator_configpath[256];

void
emulator_config_init( void )
{
    char*  end = emulator_configpath + sizeof(emulator_configpath);
    char*  p;
    void*  config;

    emulator_config = aconfig_node("","");

    p = bufprint_config_file(emulator_configpath, end, "emulator.cfg");
    if (p >= end) {
        dwarning( "emulator configuration path too long" );
        emulator_configpath[0] = 0;
        return;
    }

    config = load_text_file( emulator_configpath );
    if (config == NULL)
        D( "cannot load emulator configuration at '%s'\n",
           emulator_configpath );
    else {
        aconfig_load( emulator_config, config );
        emulator_config_found = 1;
    }
}

/* only call this function on normal exits, so that ^C doesn't save the configuration */
void
emulator_config_done( void )
{
    int     save = 0;  /* only save config if we see changes */
    AConfig*  guid_node;
    char    guid_value[32];
    AConfig*  window_node;
    int     prev_x, prev_y, win_x, win_y;

    if (!emulator_configpath[0]) {
        D("no config path ?");
        return;
    }

    /* compare window positions */
    {
        SDL_WM_GetPos( &win_x, &win_y );
        prev_x = win_x - 1;
        prev_y = win_y - 1;

        window_node = aconfig_find( emulator_config, "window" );
        if (window_node == NULL) {
            aconfig_set( emulator_config, "window", "" );
            window_node = aconfig_find( emulator_config, "window" );
            save = 1;
        }

        prev_x = (int)aconfig_unsigned( window_node, "x", 0 );
        prev_y = (int)aconfig_unsigned( window_node, "y", 0 );

        save = (prev_x != win_x) || (prev_y != win_y);

       /* Beware: If the new window position is definitely off-screen,
        * we don't want to save it in the configuration file. This can
        * happen for example on Linux where certain window managers
        * will 'minimize' a window by moving it to coordinates like
        * (-5000,-5000)
        */
        if ( !SDL_WM_IsFullyVisible(0) ) {
            D( "not saving new emulator window position since it is not fully visible" );
            save = 0;
        }
        else if (save)
            D( "emulator window position changed and will be saved as (%d, %d)", win_x, win_y );
    }

    /* If there is no guid node, create one with the current time in
     * milliseconds. Thus, the value doesn't correspond to any system or
     * user-specific data
     */
#define  GUID_NAME  "unique-id"

    guid_node = aconfig_find( emulator_config, GUID_NAME );
    if (!guid_node) {
        struct timeval  tm;
        gettimeofday( &tm, NULL );
        sprintf( guid_value, "%lld", (long long)tm.tv_sec*1000 + tm.tv_usec/1000 );
        save = 1;
        aconfig_set( emulator_config, GUID_NAME, guid_value );
    }

    if (save) {
        char  xbuf[16], ybuf[16];

        sprintf( xbuf, "%d", win_x );
        sprintf( ybuf, "%d", win_y );

        aconfig_set( window_node, "x", xbuf );
        aconfig_set( window_node, "y", ybuf );

        /* do we need to create the $HOME/.android directory ? */
        if ( !path_exists(emulator_configpath) ) {
            char*  dir = path_parent(emulator_configpath, 1);
            if (dir == NULL) {
                D("invalid user-specific config directory: '%s'", emulator_configpath);
                return;
            }
            if ( path_mkdir_if_needed(dir, 0755) < 0 ) {
                D("cannot create directory '%s', configuration lost", dir);
                free(dir);
                return;
            }
            free(dir);
        }
        if ( aconfig_save_file( emulator_config, emulator_configpath ) < 0 ) {
            D( "cannot save configuration to %s", emulator_configpath);
        } else
            D( "configuration saved to %s", emulator_configpath );
    }
}

void *loadpng(const char *fn, unsigned *_width, unsigned *_height);
void *readpng(const unsigned char*  base, size_t  size, unsigned *_width, unsigned *_height);

#ifdef CONFIG_DARWIN
#  define  ANDROID_ICON_PNG  "android_icon_256.png"
#else
#  define  ANDROID_ICON_PNG  "android_icon_16.png"
#endif

static void
sdl_set_window_icon( void )
{
    static int  window_icon_set;

    if (!window_icon_set)
    {
#ifdef _WIN32
        HANDLE         handle = GetModuleHandle( NULL );
        HICON          icon   = LoadIcon( handle, MAKEINTRESOURCE(1) );
        SDL_SysWMinfo  wminfo;

        SDL_GetWMInfo(&wminfo);

        SetClassLong( wminfo.window, GCL_HICON, (LONG)icon );
#else  /* !_WIN32 */
        unsigned              icon_w, icon_h;
        size_t                icon_bytes;
        const unsigned char*  icon_data;
        void*                 icon_pixels;

        window_icon_set = 1;

        icon_data = android_icon_find( ANDROID_ICON_PNG, &icon_bytes );
        if ( !icon_data )
            return;

        icon_pixels = readpng( icon_data, icon_bytes, &icon_w, &icon_h );
        if ( !icon_pixels )
            return;

       /* the data is loaded into memory as RGBA bytes by libpng. we want to manage
        * the values as 32-bit ARGB pixels, so swap the bytes accordingly depending
        * on our CPU endianess
        */
        {
            unsigned*  d     = icon_pixels;
            unsigned*  d_end = d + icon_w*icon_h;

            for ( ; d < d_end; d++ ) {
                unsigned  pix = d[0];
#if WORDS_BIGENDIAN
                /* R,G,B,A read as RGBA => ARGB */
                pix = ((pix >> 8) & 0xffffff) | (pix << 24);
#else
                /* R,G,B,A read as ABGR => ARGB */
                pix = (pix & 0xff00ff00) | ((pix >> 16) & 0xff) | ((pix & 0xff) << 16);
#endif
                d[0] = pix;
            }
        }

        SDL_Surface* icon = sdl_surface_from_argb32( icon_pixels, icon_w, icon_h );
        if (icon != NULL) {
            SDL_WM_SetIcon(icon, NULL);
            SDL_FreeSurface(icon);
            free( icon_pixels );
        }
#endif	/* !_WIN32 */
    }
}


/***********************************************************************/
/***********************************************************************/
/*****                                                             *****/
/*****            S K I N   I M A G E S                            *****/
/*****                                                             *****/
/***********************************************************************/
/***********************************************************************/

void send_key_event(unsigned code, unsigned down)
{
    if(code == 0) {
        return;
    }
    if (VERBOSE_CHECK(keys))
        printf(">> KEY [0x%03x,%s]\n", (code & 0x1ff), down ? "down" : " up " );
    kbd_put_keycode((code & 0x1ff) | (down ? 0x200 : 0));
}



typedef struct {
    AConfig*       aconfig;
    SkinFile*      layout_file;
    SkinLayout*    layout;
    SkinKeyboard*  keyboard;
    SkinWindow*    window;
    int            win_x;
    int            win_y;
    int            show_trackball;
    SkinTrackBall* trackball;
    int            lcd_brightness;
    SkinImage*     onion;
    SkinRotation   onion_rotation;
    int            onion_alpha;

    AndroidOptions  opts[1];  /* copy of options */
} QEmulator;

static QEmulator   qemulator[1];

static void
qemulator_done( QEmulator*  emulator )
{
    if (emulator->window) {
        skin_window_free(emulator->window);
        emulator->window = NULL;
    }
    if (emulator->trackball) {
        skin_trackball_destroy(emulator->trackball);
        emulator->trackball = NULL;
    }
    if (emulator->keyboard) {
        skin_keyboard_free(emulator->keyboard);
        emulator->keyboard = NULL;
    }
    emulator->layout = NULL;
    if (emulator->layout_file) {
        skin_file_free(emulator->layout_file);
        emulator->layout_file = NULL;
    }
}


static void
qemulator_setup( QEmulator*  emulator );

static void
qemulator_fb_update( void*   _emulator, int  x, int  y, int  w, int  h )
{
    QEmulator*  emulator = _emulator;

    if (emulator->window)
        skin_window_update_display( emulator->window, x, y, w, h );
}

static void
qemulator_fb_rotate( void*  _emulator, int  rotation )
{
    QEmulator*     emulator = _emulator;

    qemulator_setup( emulator );
}



static int
qemulator_init( QEmulator*       emulator,
                AConfig*         aconfig,
                const char*      basepath,
                int              x,
                int              y,
                AndroidOptions*  opts )
{
    emulator->aconfig     = aconfig;
    emulator->layout_file = skin_file_create_from_aconfig(aconfig, basepath);
    emulator->layout      = emulator->layout_file->layouts;
    emulator->keyboard    = skin_keyboard_create_from_aconfig(aconfig, opts->raw_keys);
    emulator->window      = NULL;
    emulator->win_x       = x;
    emulator->win_y       = y;
    emulator->opts[0]     = opts[0];

    /* register as a framebuffer clients for all displays defined in the skin file */
    SKIN_FILE_LOOP_PARTS( emulator->layout_file, part )
        SkinDisplay*  disp = part->display;
        if (disp->valid) {
            qframebuffer_add_client( disp->qfbuff,
                                        emulator,
                                        qemulator_fb_update,
                                        qemulator_fb_rotate,
                                        NULL );
        }
    SKIN_FILE_LOOP_END_PARTS
    return 0;
}


static AndroidKeyCode
qemulator_rotate_keycode( QEmulator*      emulator,
                          AndroidKeyCode  sym )
{
    switch (skin_layout_get_dpad_rotation(emulator->layout)) {
        case SKIN_ROTATION_90:
            switch (sym) {
                case kKeyCodeDpadLeft:  sym = kKeyCodeDpadDown; break;
                case kKeyCodeDpadRight: sym = kKeyCodeDpadUp; break;
                case kKeyCodeDpadUp:    sym = kKeyCodeDpadLeft; break;
                case kKeyCodeDpadDown:  sym = kKeyCodeDpadRight; break;
                default: ;
            }
            break;

        case SKIN_ROTATION_180:
            switch (sym) {
                case kKeyCodeDpadLeft:  sym = kKeyCodeDpadRight; break;
                case kKeyCodeDpadRight: sym = kKeyCodeDpadLeft; break;
                case kKeyCodeDpadUp:    sym = kKeyCodeDpadDown; break;
                case kKeyCodeDpadDown:  sym = kKeyCodeDpadUp; break;
                default: ;
            }
            break;

        case SKIN_ROTATION_270:
            switch (sym) {
                case kKeyCodeDpadLeft:  sym = kKeyCodeDpadUp; break;
                case kKeyCodeDpadRight: sym = kKeyCodeDpadDown; break;
                case kKeyCodeDpadUp:    sym = kKeyCodeDpadRight; break;
                case kKeyCodeDpadDown:  sym = kKeyCodeDpadLeft; break;
                default: ;
            }
            break;

        default: ;
    }
    return sym;
}

static int
get_device_dpi( AndroidOptions*  opts )
{
    int    dpi_device  = DEFAULT_DEVICE_DPI;

    if (opts->dpi_device != NULL) {
        char*  end;
        dpi_device = strtol( opts->dpi_device, &end, 0 );
        if (end == NULL || *end != 0 || dpi_device <= 0) {
            fprintf(stderr, "argument for -dpi-device must be a positive integer. Aborting\n" );
            exit(1);
        }
    }
    return  dpi_device;
}

static double
get_default_scale( AndroidOptions*  opts )
{
    int     dpi_device  = get_device_dpi( opts );
    int     dpi_monitor = -1;
    double  scale       = 0.0;

    /* possible values for the 'scale' option are
     *   'auto'        : try to determine the scale automatically
     *   '<number>dpi' : indicates the host monitor dpi, compute scale accordingly
     *   '<fraction>'  : use direct scale coefficient
     */

    if (opts->scale) {
        if (!strcmp(opts->scale, "auto"))
        {
            /* we need to get the host dpi resolution ? */
            int   xdpi, ydpi;

            if ( get_monitor_resolution( &xdpi, &ydpi ) < 0 ) {
                fprintf(stderr, "could not get monitor DPI resolution from system. please use -dpi-monitor to specify one\n" );
                exit(1);
            }
            D( "system reported monitor resolutions: xdpi=%d ydpi=%d\n", xdpi, ydpi);
            dpi_monitor = (xdpi + ydpi+1)/2;
        }
        else
        {
            char*   end;
            scale = strtod( opts->scale, &end );

            if (end && end[0] == 'd' && end[1] == 'p' && end[2] == 'i' && end[3] == 0) {
                if ( scale < 20 || scale > 1000 ) {
                    fprintf(stderr, "emulator: ignoring bad -scale argument '%s': %s\n", opts->scale,
                            "host dpi number must be between 20 and 1000" );
                    exit(1);
                }
                dpi_monitor = scale;
                scale       = 0.0;
            }
            else if (end == NULL || *end != 0) {
                fprintf(stderr, "emulator: ignoring bad -scale argument '%s': %s\n", opts->scale,
                        "not a number or the 'auto' keyword" );
                exit(1);
            }
            else if ( scale < 0.1 || scale > 3. ) {
                fprintf(stderr, "emulator: ignoring bad -window-scale argument '%s': %s\n", opts->scale,
                        "must be between 0.1 and 3.0" );
                exit(1);
            }
        }
    }

    if (scale == 0.0 && dpi_monitor > 0)
        scale = dpi_monitor*1.0/dpi_device;

    if (scale == 0.0)
        scale = 1.0;

    return scale;
}

void
android_emulator_set_window_scale( double  scale, int  is_dpi )
{
    QEmulator*  emulator = qemulator;

    if (is_dpi)
        scale /= get_device_dpi( emulator->opts );

    if (emulator->window)
        skin_window_set_scale( emulator->window, scale );
}


static void
qemulator_set_title( QEmulator*  emulator )
{
    char  temp[64];

    if (emulator->window == NULL)
        return;

    snprintf( temp, sizeof(temp), "Android Emulator (%s:%d)",
              avmInfo_getName( android_vmInfo ),
              android_base_port );

    skin_window_set_title( emulator->window, temp );
}

/* called by the emulated framebuffer device each time the content of the
 * framebuffer has changed. the rectangle is the bounding box of all changes
 */
static void
sdl_update(DisplayState *ds, int x, int y, int w, int h)
{
    /* this function is being called from the console code that is currently inactive
    ** simple totally ignore it...
    */
    (void)ds;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}



static void
qemulator_light_brightness( void* opaque, const char*  light, int  value )
{
    QEmulator*  emulator = opaque;

    D("%s: light='%s' value=%d window=%p", __FUNCTION__, light, value, emulator->window);
    if ( !strcmp(light, "lcd_backlight") ) {
        emulator->lcd_brightness = value;
        if (emulator->window)
            skin_window_set_lcd_brightness( emulator->window, value );
        return;
    }
}


static void
qemulator_setup( QEmulator*  emulator )
{
    AndroidOptions*  opts = emulator->opts;

    if ( !emulator->window && !opts->no_window ) {
        SkinLayout*  layout = emulator->layout;
        double       scale  = get_default_scale(emulator->opts);

        emulator->window = skin_window_create( layout, emulator->win_x, emulator->win_y, scale, 0);
        if (emulator->window == NULL)
            return;

        {
            SkinTrackBall*           ball;
            SkinTrackBallParameters  params;

            params.diameter   = 30;
            params.ring       = 2;
            params.ball_color = 0xffe0e0e0;
            params.dot_color  = 0xff202020;
            params.ring_color = 0xff000000;

            ball = skin_trackball_create( &params );
            emulator->trackball = ball;
            skin_window_set_trackball( emulator->window, ball );

            emulator->lcd_brightness = 128;  /* 50% */
            skin_window_set_lcd_brightness( emulator->window, emulator->lcd_brightness );
        }

        if ( emulator->onion != NULL )
            skin_window_set_onion( emulator->window,
                                   emulator->onion,
                                   emulator->onion_rotation,
                                   emulator->onion_alpha );

        qemulator_set_title( emulator );

        skin_window_enable_touch ( emulator->window, android_hw->hw_touchScreen != 0 );
        skin_window_enable_dpad  ( emulator->window, android_hw->hw_dPad != 0 );
        skin_window_enable_qwerty( emulator->window, android_hw->hw_keyboard != 0 );
        skin_window_enable_trackball( emulator->window, android_hw->hw_trackBall != 0 );
    }

    /* initialize hardware control support */
    {
        AndroidHwControlFuncs  funcs;

        funcs.light_brightness = qemulator_light_brightness;
        android_hw_control_init( emulator, &funcs );
    }
}


/* called by the emulated framebuffer device each time the framebuffer
 * is resized or rotated */
static void
sdl_resize(DisplayState *ds, int w, int h, int rotation)
{
    fprintf(stderr, "weird, sdl_resize being called with framebuffer interface\n");
    exit(1);
}


static void sdl_refresh(DisplayState *ds)
{
    QEmulator*     emulator = ds->opaque;
    SDL_Event      ev;
    SkinWindow*    window   = emulator->window;
    SkinKeyboard*  keyboard = emulator->keyboard;

   /* this will eventually call sdl_update if the content of the VGA framebuffer
    * has changed */
    qframebuffer_check_updates();

    if (window == NULL)
        return;

    while(SDL_PollEvent(&ev)){
        switch(ev.type){
        case SDL_VIDEOEXPOSE:
            skin_window_redraw( window, NULL );
            break;

        case SDL_KEYDOWN:
#ifdef _WIN32
            /* special code to deal with Alt-F4 properly */
            if (ev.key.keysym.sym == SDLK_F4 &&
                ev.key.keysym.mod & KMOD_ALT) {
              goto CleanExit;
            }
#endif
#ifdef __APPLE__
            /* special code to deal with Command-Q properly */
            if (ev.key.keysym.sym == SDLK_q &&
                ev.key.keysym.mod & KMOD_META) {
              goto CleanExit;
            }
#endif
            skin_keyboard_process_event( keyboard, &ev, 1 );
            break;

        case SDL_KEYUP:
            skin_keyboard_process_event( keyboard, &ev, 0 );
            break;

        case SDL_MOUSEMOTION:
            skin_window_process_event( window, &ev );
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            {
                int  down = (ev.type == SDL_MOUSEBUTTONDOWN);
                if (ev.button.button == 4)
                {
                    /* scroll-wheel simulates DPad up */
                    AndroidKeyCode  kcode;

                    kcode = qemulator_rotate_keycode(emulator, kKeyCodeDpadUp);
                    send_key_event( kcode, down );
                }
                else if (ev.button.button == 5)
                {
                    /* scroll-wheel simulates DPad down */
                    AndroidKeyCode  kcode;

                    kcode = qemulator_rotate_keycode(emulator, kKeyCodeDpadDown);
                    send_key_event( kcode, down );
                }
                else if (ev.button.button == SDL_BUTTON_LEFT) {
                    skin_window_process_event( window, &ev );
                }
#if 0
                else {
                fprintf(stderr, "... mouse button %s: button=%d state=%04x x=%d y=%d\n",
                                down ? "down" : "up  ",
                                ev.button.button, ev.button.state, ev.button.x, ev.button.y);
                }
#endif
            }
            break;

        case SDL_QUIT:
#if defined _WIN32 || defined __APPLE__
        CleanExit:
#endif
            /* only save emulator config through clean exit */
            qemulator_done( emulator );
            qemu_system_shutdown_request();
            return;
        }
    }

    skin_keyboard_flush( keyboard );
}


/* used to respond to a given keyboard command shortcut
 */
static void
handle_key_command( void*  opaque, SkinKeyCommand  command, int  down )
{
    static const struct { SkinKeyCommand  cmd; AndroidKeyCode  kcode; }  keycodes[] =
    {
        { SKIN_KEY_COMMAND_BUTTON_CALL,   kKeyCodeCall },
        { SKIN_KEY_COMMAND_BUTTON_HOME,   kKeyCodeHome },
        { SKIN_KEY_COMMAND_BUTTON_BACK,   kKeyCodeBack },
        { SKIN_KEY_COMMAND_BUTTON_HANGUP, kKeyCodeEndCall },
        { SKIN_KEY_COMMAND_BUTTON_POWER,  kKeyCodePower },
        { SKIN_KEY_COMMAND_BUTTON_SEARCH,      kKeyCodeSearch },
        { SKIN_KEY_COMMAND_BUTTON_MENU,        kKeyCodeMenu },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_UP,     kKeyCodeDpadUp },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_LEFT,   kKeyCodeDpadLeft },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_RIGHT,  kKeyCodeDpadRight },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_DOWN,   kKeyCodeDpadDown },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_CENTER, kKeyCodeDpadCenter },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_UP,   kKeyCodeVolumeUp },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_DOWN, kKeyCodeVolumeDown },
        { SKIN_KEY_COMMAND_NONE, 0 }
    };
    int          nn;
    static int   tracing = 0;
    QEmulator*   emulator = opaque;


    for (nn = 0; keycodes[nn].kcode != 0; nn++) {
        if (command == keycodes[nn].cmd) {
            unsigned  code = keycodes[nn].kcode;
            if (down)
                code |= 0x200;
            kbd_put_keycode( code );
            return;
        }
    }

    // for the show-trackball command, handle down events to enable, and
    // up events to disable
    if (command == SKIN_KEY_COMMAND_SHOW_TRACKBALL) {
        emulator->show_trackball = (down != 0);
        skin_window_show_trackball( emulator->window, emulator->show_trackball );
        //qemulator_set_title( emulator );
        return;
    }

    // only handle down events for the rest
    if (down == 0)
        return;

    switch (command)
    {
    case SKIN_KEY_COMMAND_TOGGLE_NETWORK:
        {
            qemu_net_disable = !qemu_net_disable;
            if (android_modem) {
                amodem_set_data_registration(
                        android_modem,
                qemu_net_disable ? A_REGISTRATION_UNREGISTERED
                    : A_REGISTRATION_HOME);
            }
            D( "network is now %s", qemu_net_disable ? "disconnected" : "connected" );
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_FULLSCREEN:
        if (emulator->window) {
            skin_window_toggle_fullscreen(emulator->window);
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_TRACING:
        {
#ifdef CONFIG_TRACE
            tracing = !tracing;
            if (tracing)
                start_tracing();
            else
                stop_tracing();
#endif
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_TRACKBALL:
        emulator->show_trackball = !emulator->show_trackball;
        skin_window_show_trackball( emulator->window, emulator->show_trackball );
        break;

    case SKIN_KEY_COMMAND_ONION_ALPHA_UP:
    case SKIN_KEY_COMMAND_ONION_ALPHA_DOWN:
        if (emulator->onion)
        {
            int  alpha = emulator->onion_alpha;

            if (command == SKIN_KEY_COMMAND_ONION_ALPHA_UP)
                alpha += 16;
            else
                alpha -= 16;

            if (alpha > 256)
                alpha = 256;
            else if (alpha < 0)
                alpha = 0;

            emulator->onion_alpha = alpha;
            skin_window_set_onion( emulator->window, emulator->onion, emulator->onion_rotation, alpha );
            skin_window_redraw( emulator->window, NULL );
            //dprint( "onion alpha set to %d (%.f %%)", alpha, alpha/2.56 );
        }
        break;

    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV:
    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT:
        {
            SkinLayout*  layout = NULL;

            if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT) {
                layout = emulator->layout->next;
                if (layout == NULL)
                    layout = emulator->layout_file->layouts;
            }
            else if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV) {
                layout = emulator->layout_file->layouts;
                while (layout->next && layout->next != emulator->layout)
                    layout = layout->next;
            }
            if (layout != NULL) {
                SkinRotation  rotation;

                emulator->layout = layout;
                skin_window_reset( emulator->window, layout );

                rotation = skin_layout_get_dpad_rotation( layout );

                if (emulator->keyboard)
                    skin_keyboard_set_rotation( emulator->keyboard, rotation );

                if (emulator->trackball) {
                    skin_trackball_set_rotation( emulator->trackball, rotation );
                    skin_window_set_trackball( emulator->window, emulator->trackball );
                    skin_window_show_trackball( emulator->window, emulator->show_trackball );
                }

                skin_window_set_lcd_brightness( emulator->window, emulator->lcd_brightness );

                qframebuffer_invalidate_all();
                qframebuffer_check_updates();
            }
        }
        break;

    default:
        /* XXX: TODO ? */
        ;
    }
}


static void sdl_at_exit(void)
{
    emulator_config_done();
    qemulator_done( qemulator );
    SDL_Quit();
}


void sdl_display_init(DisplayState *ds, int full_screen)
{
    QEmulator*    emulator = qemulator;
    SkinDisplay*  disp     = skin_layout_get_display(emulator->layout);

//    fprintf(stderr,"*** sdl_display_init ***\n");
    ds->opaque = emulator;

    if (disp->rotation & 1) {
        ds->width  = disp->rect.size.h;
        ds->height = disp->rect.size.w;
    } else {
        ds->width  = disp->rect.size.w;
        ds->height = disp->rect.size.h;
    }

    ds->dpy_update  = sdl_update;
    ds->dpy_resize  = sdl_resize;
    ds->dpy_refresh = sdl_refresh;

    skin_keyboard_enable( emulator->keyboard, 1 );
    skin_keyboard_on_command( emulator->keyboard, handle_key_command, emulator );
}


extern SkinKeyboard*  android_emulator_get_keyboard(void)
{
    return qemulator->keyboard;
}

static const char*  skin_network_speed = NULL;
static const char*  skin_network_delay = NULL;

/* list of skin aliases */
static const struct {
    const char*  name;
    const char*  alias;
} skin_aliases[] = {
    { "QVGA-L", "320x240" },
    { "QVGA-P", "240x320" },
    { "HVGA-L", "480x320" },
    { "HVGA-P", "320x480" },
    { "QVGA", "320x240" },
    { "HVGA", "320x480" },
    { NULL, NULL }
};

/* this is used by hw/events_device.c to send the charmap name to the system */
const char*    android_skin_keycharmap = NULL;

void init_skinned_ui(const char *path, const char *name, AndroidOptions*  opts)
{
    char      tmp[1024];
    AConfig*  root;
    AConfig*  n;
    int       win_x, win_y, flags;

    signal(SIGINT, SIG_DFL);
#ifndef _WIN32
    signal(SIGQUIT, SIG_DFL);
#endif

    /* we're not a game, so allow the screensaver to run */
    putenv("SDL_VIDEO_ALLOW_SCREENSAVER=1");

    flags = SDL_INIT_NOPARACHUTE;
    if (!opts->no_window)
        flags |= SDL_INIT_VIDEO;

    if(SDL_Init(flags)){
        fprintf(stderr, "SDL init failure, reason is: %s\n", SDL_GetError() );
        exit(1);
    }

    if (!opts->no_window) {
        SDL_EnableUNICODE(!opts->raw_keys);
        SDL_EnableKeyRepeat(0,0);

        sdl_set_window_icon();
    }
    else
    {
#ifndef _WIN32
       /* prevent SIGTTIN and SIGTTOUT from stopping us. this is necessary to be
        * able to run the emulator in the background (e.g. "emulator &").
        * despite the fact that the emulator should not grab input or try to
        * write to the output in normal cases, we're stopped on some systems
        * (e.g. OS X)
        */
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
#endif
    }
    atexit(sdl_at_exit);

    root = aconfig_node("", "");

    if(name) {
        /* Support skin aliases like QVGA-H QVGA-P, etc...
           But first we check if it's a directory that exist before applying
           the alias */
        int  checkAlias = 1;

        if (path != NULL) {
            bufprint(tmp, tmp+sizeof(tmp), "%s/%s", path, name);
            if (path_exists(tmp)) {
                checkAlias = 0;
            } else {
                D("there is no '%s' skin in '%s'", name, path);
            }
        }

        if (checkAlias) {
            int  nn;

            for (nn = 0; ; nn++ ) {
                const char*  skin_name  = skin_aliases[nn].name;
                const char*  skin_alias = skin_aliases[nn].alias;

                if ( !skin_name )
                    break;

                if ( !strcasecmp( skin_name, name ) ) {
                    D("skin name '%s' aliased to '%s'", name, skin_alias);
                    name = skin_alias;
                    break;
                }
            }
        }

        /* Magically support skins like "320x240" */
        if(isdigit(name[0])) {
            char *x = strchr(name, 'x');
            if(x && isdigit(x[1])) {
                int width = atoi(name);
                int height = atoi(x + 1);
                sprintf(tmp,"display {\n  width %d\n  height %d\n}\n", 
                        width, height);
                aconfig_load(root, strdup(tmp));
                path = ":";
                goto found_a_skin;
            }
        }

        if (path == NULL) {
            derror("unknown skin name '%s'", name);
            exit(1);
        }

        sprintf(tmp, "%s/%s/layout", path, name);
        D("trying to load skin file '%s'", tmp);

        if(aconfig_load_file(root, tmp) >= 0) {
            sprintf(tmp, "%s/%s/", path, name);
            path = tmp;
            goto found_a_skin;
        } else {
            dwarning("could not load skin file '%s', using built-in one\n", 
                     tmp);
        }
    }

    {
        const unsigned char*  layout_base;
        size_t                layout_size;

        name = "<builtin>";

        layout_base = android_resource_find( "layout", &layout_size );
        if (layout_base != NULL) {
            char*  base = malloc( layout_size+1 );
            memcpy( base, layout_base, layout_size );
            base[layout_size] = 0;

            D("parsing built-in skin layout file (size=%d)", (int)layout_size);
            aconfig_load(root, base);
            path = ":";
        } else {
            fprintf(stderr, "Couldn't load builtin skin\n");
            exit(1);
        }
    }

found_a_skin:
    {
        AConfig*  node = aconfig_find( emulator_config, "window" );

        win_x = 10;
        win_y = 10;

        if (node == NULL) {
            if (emulator_config_found)
                dwarning( "broken configuration file doesn't have 'window' element" );
        } else {
            win_x = aconfig_int( node, "x", win_x );
            win_y = aconfig_int( node, "y", win_y );
        }
    }
    if ( qemulator_init( qemulator, root, path, win_x, win_y, opts ) < 0 ) {
        fprintf(stderr, "### Error: could not load emulator skin '%s'\n", name);
        exit(1);
    }

    android_skin_keycharmap = skin_keyboard_charmap_name(qemulator->keyboard);

    /* the default network speed and latency can now be specified by the device skin */
    n = aconfig_find(root, "network");
    if (n != NULL) {
        skin_network_speed = aconfig_str(n, "speed", 0);
        skin_network_delay = aconfig_str(n, "delay", 0);
    }

#if 0
    /* create a trackball if needed */
    n = aconfig_find(root, "trackball");
    if (n != NULL) {
        SkinTrackBallParameters  params;

        params.x        = aconfig_unsigned(n, "x", 0);
        params.y        = aconfig_unsigned(n, "y", 0);
        params.diameter = aconfig_unsigned(n, "diameter", 20);
        params.ring     = aconfig_unsigned(n, "ring", 1);

        params.ball_color = aconfig_unsigned(n, "ball-color", 0xffe0e0e0);
        params.dot_color  = aconfig_unsigned(n, "dot-color",  0xff202020 );
        params.ring_color = aconfig_unsigned(n, "ring-color", 0xff000000 );

        qemu_disp->trackball = skin_trackball_create( &params );
        skin_trackball_refresh( qemu_disp->trackball );
    }
#endif

    /* add an onion overlay image if needed */
    if (opts->onion) {
        SkinImage*  onion = skin_image_find_simple( opts->onion );
        int         alpha, rotate;

        if ( opts->onion_alpha && 1 == sscanf( opts->onion_alpha, "%d", &alpha ) ) {
            alpha = (256*alpha)/100;
        } else
            alpha = 128;

        if ( opts->onion_rotation && 1 == sscanf( opts->onion_rotation, "%d", &rotate ) ) {
            rotate &= 3;
        } else
            rotate = SKIN_ROTATION_0;

        qemulator->onion          = onion;
        qemulator->onion_alpha    = alpha;
        qemulator->onion_rotation = rotate;
    }
}

int qemu_main(int argc, char **argv);

/* this function dumps the QEMU help */
extern void  help( void );
extern void  emulator_help( void );

#define  VERBOSE_OPT(str,var)   { str, &var }

#define  _VERBOSE_TAG(x,y)   { #x, VERBOSE_##x, y },
static const struct { const char*  name; int  flag; const char*  text; }
verbose_options[] = {
    VERBOSE_TAG_LIST
    { 0, 0, 0 }
};

int
android_parse_network_speed(const char*  speed)
{
    int          n;
    char*  end;
    double       sp;

    if (speed == NULL || speed[0] == 0) {
        speed = DEFAULT_NETSPEED;
    }

    for (n = 0; android_netspeeds[n].name != NULL; n++) {
        if (!strcmp(android_netspeeds[n].name, speed)) {
            qemu_net_download_speed = android_netspeeds[n].download;
            qemu_net_upload_speed   = android_netspeeds[n].upload;
            return 0;
        }
    }

    /* is this a number ? */
    sp = strtod(speed, &end);
    if (end == speed) {
        return -1;
    }

    qemu_net_download_speed = qemu_net_upload_speed = sp*1000.;
    if (*end == ':') {
        speed = end+1;
        sp = strtod(speed, &end);
        if (end > speed) {
            qemu_net_download_speed = sp*1000.;
        }
    }

    if (android_modem)
        amodem_set_data_network_type( android_modem, 
                                      android_parse_network_type(speed) );
    return 0;
}


int
android_parse_network_latency(const char*  delay)
{
    int  n;
    char*  end;
    double  sp;

    if (delay == NULL || delay[0] == 0)
        delay = DEFAULT_NETDELAY;

    for (n = 0; android_netdelays[n].name != NULL; n++) {
        if ( !strcmp( android_netdelays[n].name, delay ) ) {
            qemu_net_min_latency = android_netdelays[n].min_ms;
            qemu_net_max_latency = android_netdelays[n].max_ms;
            return 0;
        }
    }

    /* is this a number ? */
    sp = strtod(delay, &end);
    if (end == delay) {
        return -1;
    }

    qemu_net_min_latency = qemu_net_max_latency = (int)sp;
    if (*end == ':') {
        delay = (const char*)end+1;
        sp = strtod(delay, &end);
        if (end > delay) {
            qemu_net_max_latency = (int)sp;
        }
    }
    return 0;
}


static int
load_keyset(const char*  path)
{
    if (path_can_read(path)) {
        AConfig*  root = aconfig_node("","");
        if (!aconfig_load_file(root, path)) {
            android_keyset = skin_keyset_new(root);
            if (android_keyset != NULL) {
                D( "keyset loaded from: %s", path);
                return 0;
            }
        }
    }
    return -1;
}

static void
parse_keyset(const char*  keyset, AndroidOptions*  opts)
{
    char   kname[MAX_PATH];
    char   temp[MAX_PATH];
    char*  p;
    char*  end;

    /* append .keyset suffix if needed */
    if (strchr(keyset, '.') == NULL) {
        p   =  kname;
        end = p + sizeof(kname);
        p   = bufprint(p, end, "%s.keyset", keyset);
        if (p >= end) {
            derror( "keyset name too long: '%s'\n", keyset);
            exit(1);
        }
        keyset = kname;
    }

    /* look for a the keyset file */
    p   = temp;
    end = p + sizeof(temp);
    p = bufprint_config_file(p, end, keyset);
    if (p < end && load_keyset(temp) == 0)
        return;

    p = temp;
    p = bufprint(p, end, "%s" PATH_SEP "keysets" PATH_SEP "%s", opts->system, keyset);
    if (p < end && load_keyset(temp) == 0)
        return;

    p = temp;
    p = bufprint_app_dir(p, end);
    p = bufprint(p, end, PATH_SEP "keysets" PATH_SEP "%s", keyset);
    if (p < end && load_keyset(temp) == 0)
        return;

    return;
}

static void
write_default_keyset( void )
{
    char   path[MAX_PATH];

    bufprint_config_file( path, path+sizeof(path), KEYSET_FILE );

    /* only write if there is no file here */
    if ( !path_exists(path) ) {
        int          fd = open( path, O_WRONLY | O_CREAT, 0666 );
        int          ret;
        const char*  ks = skin_keyset_get_default();


        D( "writing default keyset file to %s", path );

        if (fd < 0) {
            D( "%s: could not create file: %s", __FUNCTION__, strerror(errno) );
            return;
        }
        CHECKED(ret, write(fd, ks, strlen(ks)));
        close(fd);
    }
}

#ifdef CONFIG_NAND_LIMITS

static uint64_t
parse_nand_rw_limit( const char*  value )
{
    char*     end;
    uint64_t  val = strtoul( value, &end, 0 );

    if (end == value) {
        derror( "bad parameter value '%s': expecting unsigned integer", value );
        exit(1);
    }

    switch (end[0]) {
        case 'K':  val <<= 10; break;
        case 'M':  val <<= 20; break;
        case 'G':  val <<= 30; break;
        case 0: break;
        default:
            derror( "bad read/write limit suffix: use K, M or G" );
            exit(1);
    }
    return val;
}

static void
parse_nand_limits(char*  limits)
{
    int      pid = -1, signal = -1;
    int64_t  reads = 0, writes = 0;
    char*    item = limits;

    /* parse over comma-separated items */
    while (item && *item) {
        char*  next = strchr(item, ',');
        char*  end;

        if (next == NULL) {
            next = item + strlen(item);
        } else {
            *next++ = 0;
        }

        if ( !memcmp(item, "pid=", 4) ) {
            pid = strtol(item+4, &end, 10);
            if (end == NULL || *end) {
                derror( "bad parameter, expecting pid=<number>, got '%s'",
                        item );
                exit(1);
            }
            if (pid <= 0) {
                derror( "bad parameter: process identifier must be > 0" );
                exit(1);
            }
        }
        else if ( !memcmp(item, "signal=", 7) ) {
            signal = strtol(item+7,&end, 10);
            if (end == NULL || *end) {
                derror( "bad parameter: expecting signal=<number>, got '%s'",
                        item );
                exit(1);
            }
            if (signal <= 0) {
                derror( "bad parameter: signal number must be > 0" );
                exit(1);
            }
        }
        else if ( !memcmp(item, "reads=", 6) ) {
            reads = parse_nand_rw_limit(item+6);
        }
        else if ( !memcmp(item, "writes=", 7) ) {
            writes = parse_nand_rw_limit(item+7);
        }
        else {
            derror( "bad parameter '%s' (see -help-nand-limits)", item );
            exit(1);
        }
        item = next;
    }
    if (pid < 0) {
        derror( "bad paramater: missing pid=<number>" );
        exit(1);
    }
    else if (signal < 0) {
        derror( "bad parameter: missing signal=<number>" );
        exit(1);
    }
    else if (reads == 0 && writes == 0) {
        dwarning( "no read or write limit specified. ignoring -nand-limits" );
    } else {
        nand_threshold*  t;

        t  = &android_nand_read_threshold;
        t->pid     = pid;
        t->signal  = signal;
        t->counter = 0;
        t->limit   = reads;

        t  = &android_nand_write_threshold;
        t->pid     = pid;
        t->signal  = signal;
        t->counter = 0;
        t->limit   = writes;
    }
}
#endif /* CONFIG_NAND_LIMITS */

void emulator_help( void )
{
    STRALLOC_DEFINE(out);
    android_help_main(out);
    printf( "%.*s", out->n, out->s );
    stralloc_reset(out);
    exit(1);
}

static int
add_dns_server( const char*  server_name )
{
    struct in_addr   dns1;
    struct hostent*  host = gethostbyname(server_name);

    if (host == NULL) {
        fprintf(stderr,
                "### WARNING: can't resolve DNS server name '%s'\n",
                server_name );
        return -1;
    }

    dns1 = *(struct in_addr*)host->h_addr;
    D( "DNS server name '%s' resolved to %s", server_name, inet_ntoa(dns1) );

    if ( slirp_add_dns_server( dns1 ) < 0 ) {
        fprintf(stderr,
                "### WARNING: could not add DNS server '%s' to the network stack\n", server_name);
        return -1;
    }
    return 0;
}


enum {
    REPORT_CONSOLE_SERVER = (1 << 0),
    REPORT_CONSOLE_MAX    = (1 << 1)
};

static int
get_report_console_options( char*  end, int  *maxtries )
{
    int    flags = 0;

    if (end == NULL || *end == 0)
        return 0;

    if (end[0] != ',') {
        derror( "socket port/path can be followed by [,<option>]+ only\n");
        exit(3);
    }
    end += 1;
    while (*end) {
        char*  p = strchr(end, ',');
        if (p == NULL)
            p = end + strlen(end);

        if (memcmp( end, "server", p-end ) == 0)
            flags |= REPORT_CONSOLE_SERVER;
        else if (memcmp( end, "max=", 4) == 0) {
            end  += 4;
            *maxtries = strtol( end, NULL, 10 );
            flags |= REPORT_CONSOLE_MAX;
        } else {
            derror( "socket port/path can be followed by [,server][,max=<count>] only\n");
            exit(3);
        }

        end = p;
        if (*end)
            end += 1;
    }
    return flags;
}

static void
report_console( const char*  proto_port, int  console_port )
{
    int   s = -1, s2, ret;
    int   maxtries = 10;
    int   flags = 0;
    signal_state_t  sigstate;

    disable_sigalrm( &sigstate );

    if ( !strncmp( proto_port, "tcp:", 4) ) {
        char*  end;
        long   port = strtol(proto_port + 4, &end, 10);

        flags = get_report_console_options( end, &maxtries );

        if (flags & REPORT_CONSOLE_SERVER) {
            s = socket_loopback_server( port, SOCK_STREAM );
            if (s < 0) {
                fprintf(stderr, "could not create server socket on TCP:%ld: %s\n",
                        port, socket_errstr());
                exit(3);
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                D("trying to find console-report client on tcp:%d", port);
                s = socket_loopback_client( port, SOCK_STREAM );
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                fprintf(stderr, "could not connect to server on TCP:%ld: %s\n",
                        port, socket_errstr());
                exit(3);
            }
        }
    } else if ( !strncmp( proto_port, "unix:", 5) ) {
#ifdef _WIN32
        fprintf(stderr, "sorry, the unix: protocol is not supported on Win32\n");
        exit(3);
#else
        char*  path = strdup(proto_port+5);
        char*  end  = strchr(path, ',');
        if (end != NULL) {
            flags = get_report_console_options( end, &maxtries );
            *end  = 0;
        }
        if (flags & REPORT_CONSOLE_SERVER) {
            s = socket_unix_server( path, SOCK_STREAM );
            if (s < 0) {
                fprintf(stderr, "could not bind unix socket on '%s': %s\n",
                        proto_port+5, socket_errstr());
                exit(3);
            }
        } else {
            for ( ; maxtries > 0; maxtries-- ) {
                s = socket_unix_client( path, SOCK_STREAM );
                if (s >= 0)
                    break;

                sleep_ms(1000);
            }
            if (s < 0) {
                fprintf(stderr, "could not connect to unix socket on '%s': %s\n",
                        path, socket_errstr());
                exit(3);
            }
        }
        free(path);
#endif
    } else {
        fprintf(stderr, "-report-console must be followed by a 'tcp:<port>' or 'unix:<path>'\n");
        exit(3);
    }

    if (flags & REPORT_CONSOLE_SERVER) {
        int  tries = 3;
        D( "waiting for console-reporting client" );
        do {
            s2 = accept( s, NULL, NULL );
        } while (s2 < 0 && socket_errno == EINTR && --tries > 0);

        if (s2 < 0) {
            fprintf(stderr, "could not accept console-reporting client connection: %s\n",
                   socket_errstr());
            exit(3);
        }

        socket_close(s);
        s = s2;
    }

    /* simply send the console port in text */
    {
        char  temp[12];
        snprintf( temp, sizeof(temp), "%d", console_port );
        do {
            ret = send( s, temp, strlen(temp), 0 );
        } while (ret < 0 && socket_errno == EINTR);

        if (ret < 0) {
            fprintf(stderr, "could not send console number report: %d: %s\n",
                    socket_errno, socket_errstr() );
            exit(3);
        }
        socket_close(s);
    }
    D( "console port number sent to remote. resuming boot" );

    restore_sigalrm (&sigstate);
}

/* this function is used to perform auto-detection of the
 * system directory in the case of a SDK installation.
 *
 * we want to deal with several historical usages, hence
 * the slightly complicated logic.
 *
 * NOTE: the function returns the path to the directory
 *       containing 'fileName'. this is *not* the full
 *       path to 'fileName'.
 */
static char*
_getSdkImagePath( const char*  fileName )
{
    char   temp[MAX_PATH];
    char*  p   = temp;
    char*  end = p + sizeof(temp);
    char*  q;
    char*  app;

    static const char* const  searchPaths[] = {
        "",                                  /* program's directory */
        "/lib/images",                       /* this is for SDK 1.0 */
        "/../platforms/android-1.1/images",  /* this is for SDK 1.1 */
        NULL
    };

    app = bufprint_app_dir(temp, end);
    if (app >= end)
        return NULL;

    do {
        int  nn;

        /* first search a few well-known paths */
        for (nn = 0; searchPaths[nn] != NULL; nn++) {
            p = bufprint(app, end, "%s", searchPaths[nn]);
            q = bufprint(p, end, "/%s", fileName);
            if (q < end && path_exists(temp)) {
                *p = 0;
                goto FOUND_IT;
            }
        }

        /* hmmm. let's assume that we are in a post-1.1 SDK
         * scan ../platforms if it exists
         */
        p = bufprint(app, end, "/../platforms");
        if (p < end) {
            DirScanner*  scanner = dirScanner_new(temp);
            if (scanner != NULL) {
                int          found = 0;
                const char*  subdir;

                for (;;) {
                    subdir = dirScanner_next(scanner);
                    if (!subdir) break;

                    q = bufprint(p, end, "/%s/images/%s", subdir, fileName);
                    if (q >= end || !path_exists(temp))
                        continue;

                    found = 1;
                    q = bufprint(p, end, "/%s/images", subdir);
                    break;
                }
                dirScanner_free(scanner);
                if (found)
                    break;
            }
        }

        /* I'm out of ideas */
        return NULL;

    } while (0);

FOUND_IT:
    //D("image auto-detection: %s/%s", temp, fileName);
    return qemu_strdup(temp);
}

static char*
_getSdkImage( const char*  path, const char*  pathText, const char*  file )
{
    char  temp[MAX_PATH];
    char  *p = temp, *end = p + sizeof(temp);

    p = bufprint(temp, end, "%s/%s", path, file);
    if (p >= end || !path_exists(temp)) {
        derror("you %s directory is missing the '%s' image file.",
               pathText, file);
        exit(2);
    }

    return qemu_strdup(temp);
}


static void
_forceVmImagePath( AvmImageType  imageType, 
                   const char*   path, 
                   const char*   description,
                   int           required )
{
    if (path == NULL)
        return;

    if (required && !path_exists(path)) {
        derror("cannot find %s image file: %s", description, path);
        exit(1);
    }
    android_vmParams->forcePaths[imageType] = path;
}

#ifdef _WIN32
#undef main  /* we don't want SDL to define main */
#endif

int main(int argc, char **argv)
{
    char   tmp[MAX_PATH];
    char*  tmpend = tmp + sizeof(tmp);
    char*  args[128];
    int    n;
    char*  opt;
    int    use_sdcard_img = 0;
    int    serial = 0;
    int    gps_serial = 0;
    int    radio_serial = 0;
    int    qemud_serial = 0;
    int    shell_serial = 0;
    int    dns_count = 0;
    unsigned  cachePartitionSize = 0;

    AndroidHwConfig*  hw;

    //const char *appdir = get_app_dir();
    char*       android_build_root = NULL;
    char*       android_build_out  = NULL;

    AndroidOptions  opts[1];

    args[0] = argv[0];

    if ( android_parse_options( &argc, &argv, opts ) < 0 ) {
        exit(1);
    }

    while (argc-- > 1) {
        opt = (++argv)[0];

        if(!strcmp(opt, "-qemu")) {
            ++argv;
            --argc;
            break;
        }

        if (!strcmp(opt, "-help")) {
            emulator_help();
        }

        if (!strncmp(opt, "-help-",6)) {
            STRALLOC_DEFINE(out);
            opt += 6;

            if (!strcmp(opt, "all")) {
                android_help_all(out);
            }
            else if (android_help_for_option(opt, out) == 0) {
                /* ok */
            }
            else if (android_help_for_topic(opt, out) == 0) {
                /* ok */
            }
            if (out->n > 0) {
                printf("\n%.*s", out->n, out->s);
                exit(0);
            }

            fprintf(stderr, "unknown option: -help-%s\n", opt);
            fprintf(stderr, "please use -help for a list of valid topics\n");
            exit(1);
        }

        if (opt[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", opt);
            fprintf(stderr, "please use -help for a list of valid options\n");
            exit(1);
        }

        fprintf(stderr, "invalid command-line parameter: %s.\n", opt);
        fprintf(stderr, "Hint: use '@foo' to launch a virtual machine named 'foo'.\n");
        fprintf(stderr, "please use -help for more information\n");
        exit(1);
    }

    android_charmap = android_charmaps[0];

    if (opts->version) {
        printf("Android emulator version %s\n"
               "Copyright (C) 2006-2008 The Android Open Source Project and many others.\n"
               "This program is a derivative of the QEMU CPU emulator (www.qemu.org).\n\n",
#if defined ANDROID_BUILD_ID
               VERSION_STRING " (build_id " STRINGIFY(ANDROID_BUILD_ID) ")" );
#else
               VERSION_STRING);
#endif
        printf("  This software is licensed under the terms of the GNU General Public\n"
               "  License version 2, as published by the Free Software Foundation, and\n"
               "  may be copied, distributed, and modified under those terms.\n\n"
               "  This program is distributed in the hope that it will be useful,\n"
               "  but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
               "  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
               "  GNU General Public License for more details.\n\n");

        exit(0);
    }

    if (opts->timezone) {
        if ( timezone_set(opts->timezone) < 0 ) {
            fprintf(stderr, "emulator: it seems the timezone '%s' is not in zoneinfo format\n", opts->timezone);
        }
    }

    /* try to find the top of the Android build tree
     * unless we have been given a virtual machine name
     */
    do {
        char*  out = getenv("ANDROID_PRODUCT_OUT");

        if (out == NULL || out[0] == 0)
            break;

        if (!path_exists(out)) {
            dprint("### Weird, can't access ANDROID_PRODUCT_OUT as '%s'", out);
            break;
        }

        android_build_root = path_parent( out, 4 );
        if (android_build_root == NULL) {
            break;
        }
        if (!path_exists(android_build_root)) {
            dprint("### Weird, can't access build root as '%s'", android_build_root);
            free(android_build_root);
            android_build_root = NULL;
            break;
        }
        android_build_out = out;
        D( "found Android build root: %s", android_build_root );
        D( "found Android build out:  %s", android_build_out );
    } while (0);

    /* if no virtual machine name is given, and we're not in the
     * Android build system, we'll need to perform some auto-detection
     * magic :-)
     */
    if (opts->vm == NULL && !android_build_out) {
        if (!opts->system) {
            opts->system = _getSdkImagePath("system.img");
            if (!opts->system) {
            NO_VM_NAME:
                derror( "you must provide the name of a virtual machine to start the emulator.\n"
                        "please see -help-vm for details." );
                exit(2);
            }
            D("autoconfig: -system %s", opts->system);
        }

        if (!opts->image) {
            opts->image = _getSdkImage(opts->system, "-system", "system.img");
            D("autoconfig: -image %s", opts->image);
        }

        if (!opts->kernel) {
            opts->kernel = _getSdkImage(opts->system, "-system", "kernel-qemu");
            D("autoconfig: -kernel %s", opts->kernel);
        }

        if (!opts->ramdisk) {
            opts->ramdisk = _getSdkImage(opts->system, "-ramdisk", "ramdisk.img");
            D("autoconfig: -ramdisk %s", opts->ramdisk);
        }

        if (!opts->data) {
            /* we don't want new SDK users to keep using their
             * obsolete data images. unless they specifically
             * use -data or -datadir with an existing file,
             * we're going to complain.
             */
            if (!opts->datadir) {
                goto NO_VM_NAME;
            }

            /* here the user used -datadir, so check that there is a
             * valid data partition file here, if not abort.
             */
            bufprint(tmp, tmpend, opts->datadir, "/userdata-qemu.img");
            if (!path_exists(tmp))
                goto NO_VM_NAME;

            opts->data = qemu_strdup(tmp);
            D("autoconfig: -data %s", opts->data);
        }

        if (!opts->sdcard && opts->datadir) {
            bufprint(tmp, tmpend, opts->datadir, "/sdcard.img");
            if (path_exists(tmp)) {
                opts->sdcard = qemu_strdup(tmp);
                D("autoconfig: -sdcard %s", opts->sdcard);
            }
        }
    }

    /* setup the virtual machine parameters from our options
     */
    if (opts->nocache) {
        android_vmParams->flags |= AVMINFO_NO_CACHE;
    }
    if (opts->wipe_data) {
        android_vmParams->flags |= AVMINFO_WIPE_DATA | AVMINFO_WIPE_CACHE;
    }

    /* if certain options are set, we can force the path of
        * certain kernel/disk image files
        */
    _forceVmImagePath(AVM_IMAGE_KERNEL,  opts->kernel, "kernel", 1);
    _forceVmImagePath(AVM_IMAGE_SYSTEM,  opts->image,  "system", 1);
    _forceVmImagePath(AVM_IMAGE_RAMDISK, opts->ramdisk,"ramdisk", 1);
    _forceVmImagePath(AVM_IMAGE_USERDATA,opts->data,   "user data", 0);
    _forceVmImagePath(AVM_IMAGE_CACHE,   opts->cache,  "cache", 0);
    _forceVmImagePath(AVM_IMAGE_SDCARD,  opts->sdcard, "SD Card", 0);

    /* we don't accept -skindir without -skin now
     * to simplify the autoconfig stuff with virtual devices
     */
    if (opts->noskin) {
        opts->skin    = "320x480";
        opts->skindir = NULL;
    }

    if (opts->skindir) {
        if (!opts->skin) {
            derror( "the -skindir <path> option requires a -skin <name> option");
            exit(1);
        }
    }
    else {
        if (!opts->skin && android_build_out) {
            /* select default skin based on product type */
            const char*  p = strrchr(android_build_out,'/');
            if (p) {
                if (p[1] == 's') {
                    opts->skin = "QVGA-L";
                } else if (p[1] == 'd') {
                    opts->skin = "HVGA";
                }
            }
            D("autoconfig: -skin %s", opts->skin);
        }
        android_vmParams->skinName = opts->skin;
    }
    /* setup the virtual machine differently depending on whether
     * we are in the Android build system or not
     */
    if (opts->vm != NULL)
    {
        android_vmInfo = avmInfo_new( opts->vm, android_vmParams );
        if (android_vmInfo == NULL) {
            /* an error message has already been printed */
            D("could not find virtual machine named '%s'", opts->vm);
            exit(1);
        }
    }
    else
    {
        if (!android_build_out) {
            android_build_root = android_build_out = opts->system;
        }

        android_vmInfo = avmInfo_newForAndroidBuild(
                            android_build_root,
                            android_build_out,
                            android_vmParams );

        if(android_vmInfo == NULL) {
            D("could not start virtual machine\n");
            exit(1);
        }
    }

    if (!opts->skindir) {
        /* get the skin from the virtual machine configuration */
        opts->skin    = (char*) avmInfo_getSkinName( android_vmInfo );
        opts->skindir = (char*) avmInfo_getSkinDir( android_vmInfo );

        if (opts->skin) {
            D("autoconfig: -skin %s", opts->skin);
        }
        if (opts->skindir) {
            D("autoconfig: -skindir %s", opts->skindir);
        }
    }

    /* Read hardware configuration */
    hw = android_hw;
    if (avmInfo_getHwConfig(android_vmInfo, hw) < 0) {
        derror("could not read hardware configuration ?");
        exit(1);
    }

#ifdef CONFIG_NAND_LIMITS
    if (opts->nand_limits)
        parse_nand_limits(opts->nand_limits);
#endif

    if (opts->keyset) {
        parse_keyset(opts->keyset, opts);
        if (!android_keyset) {
            fprintf(stderr,
                    "emulator: WARNING: could not find keyset file named '%s',"
                    " using defaults instead\n",
                    opts->keyset);
        }
    }
    if (!android_keyset) {
        android_keyset = skin_keyset_new_from_text( skin_keyset_get_default() );
        if (!android_keyset) {
            fprintf(stderr, "PANIC: default keyset file is corrupted !!\n" );
            fprintf(stderr, "PANIC: please update the code in skins/skin_keyset.c\n" );
            exit(1);
        }
        if (!opts->keyset)
            write_default_keyset();
    }

    if (opts->audio) {
        if (opts->audio_in || opts->audio_out) {
            derror( "you can't use -audio with -audio-in or -audio-out\n" );
            exit(1);
        }
        if ( !audio_check_backend_name( 0, opts->audio ) ) {
            derror( "'%s' is not a valid audio output backend. see -help-audio-out\n",
                    opts->audio);
            exit(1);
        }
        opts->audio_out = opts->audio;
        opts->audio_in  = opts->audio;

        if ( !audio_check_backend_name( 1, opts->audio ) ) {
            fprintf(stderr,
                    "emulator: warning: '%s' is not a valid audio input backend. audio record disabled\n",
                    opts->audio);
            opts->audio_in = "none";
        }
    }

    if (opts->audio_in) {
        static char  env[64]; /* note: putenv needs a static unique string buffer */
        if ( !audio_check_backend_name( 1, opts->audio_in ) ) {
            derror( "'%s' is not a valid audio input backend. see -help-audio-in\n",
                    opts->audio_in);
            exit(1);
        }
        bufprint( env, env+sizeof(env), "QEMU_AUDIO_IN_DRV=%s", opts->audio_in );
        putenv( env );

        if (!hw->hw_audioInput) {
            dwarning( "Emulated hardware doesn't have audio input.");
        }
    }
    if (opts->audio_out) {
        static char  env[64]; /* note: putenv needs a static unique string buffer */
        if ( !audio_check_backend_name( 0, opts->audio_out ) ) {
            derror( "'%s' is not a valid audio output backend. see -help-audio-out\n",
                    opts->audio_out);
            exit(1);
        }
        bufprint( env, env+sizeof(env), "QEMU_AUDIO_OUT_DRV=%s", opts->audio_out );
        putenv( env );
        if (!hw->hw_audioOutput) {
            dwarning( "Emulated hardware doesn't have audio output");
        }
    }

    if (opts->cpu_delay) {
        char*   end;
        long    delay = strtol(opts->cpu_delay, &end, 0);
        if (end == NULL || *end || delay < 0 || delay > 1000 ) {
            fprintf(stderr, "option -cpu-delay must be an integer between 0 and 1000\n" );
            exit(1);
        }
        if (delay > 0)
            delay = (1000-delay);

        qemu_cpu_delay = (int) delay;
    }

    emulator_config_init();
    init_skinned_ui(opts->skindir, opts->skin, opts);

    if (!opts->netspeed) {
        if (skin_network_speed)
            D("skin network speed: '%s'", skin_network_speed);
        opts->netspeed = (char*)skin_network_speed;
    }
    if (!opts->netdelay) {
        if (skin_network_delay)
            D("skin network delay: '%s'", skin_network_delay);
        opts->netdelay = (char*)skin_network_delay;
    }

    if ( android_parse_network_speed(opts->netspeed) < 0 ) {
        fprintf(stderr, "invalid -netspeed parameter '%s', see emulator -usage\n", opts->netspeed);
        emulator_help();
    }

    if ( android_parse_network_latency(opts->netdelay) < 0 ) {
        fprintf(stderr, "invalid -netdelay parameter '%s', see emulator -usage\n", opts->netdelay);
        emulator_help();
    }

    if (opts->netfast) {
        qemu_net_download_speed = 0;
        qemu_net_upload_speed = 0;
        qemu_net_min_latency = 0;
        qemu_net_max_latency = 0;
    }

    if (opts->trace) {
        char*   tracePath = avmInfo_getTracePath(android_vmInfo, opts->trace);
        int     ret;

        if (tracePath == NULL) {
            derror( "bad -trace parameter" );
            exit(1);
        }
        ret = path_mkdir_if_needed( tracePath, 0755 );
        if (ret < 0) {
            fprintf(stderr, "could not create directory '%s'\n", tmp);
            exit(2);
        }
        opts->trace = tracePath;
    }

    if (opts->nocache)
        opts->cache = 0;

    if (opts->dns_server) {
        char*  x = strchr(opts->dns_server, ',');
        dns_count = 0;
        if (x == NULL)
        {
            if ( add_dns_server( opts->dns_server ) == 0 )
                dns_count = 1;
        }
        else
        {
            x = strdup(opts->dns_server);
            while (*x) {
                char*  y = strchr(x, ',');

                if (y != NULL)
                    *y = 0;

                if (y == NULL || y > x) {
                    if ( add_dns_server( x ) == 0 )
                        dns_count += 1;
                }

                if (y == NULL)
                    break;

                x = y+1;
            }
        }
        if (dns_count == 0)
            fprintf( stderr, "### WARNING: will use system default DNS server\n" );
    }

    if (dns_count == 0)
        dns_count = slirp_get_system_dns_servers();

    n = 1;
    /* generate arguments for the underlying qemu main() */
    args[n++] = "-kernel";
    args[n++] = (char*) avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_KERNEL);

    args[n++] = "-initrd";
    args[n++] = (char*) avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_RAMDISK);

    {
        const char*  filetype = "file";

        if (avmInfo_isImageReadOnly(android_vmInfo, AVM_IMAGE_SYSTEM))
            filetype = "initfile";

        bufprint(tmp, tmpend,
             "system,size=0x4200000,%s=%s", filetype,
             avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_SYSTEM));

        args[n++] = "-nand";
        args[n++] = strdup(tmp);
    }

    bufprint(tmp, tmpend,
             "userdata,size=0x4200000,file=%s",
             avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_USERDATA));

    args[n++] = "-nand";
    args[n++] = strdup(tmp);

    if (hw->disk_cachePartition) {
        opts->cache = (char*) avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_CACHE);
        cachePartitionSize = hw->disk_cachePartition_size;
    }
    else if (opts->cache) {
        dwarning( "Emulated hardware doesn't support a cache partition" );
        opts->cache   = NULL;
        opts->nocache = 1;
    }

    if (opts->cache) {
        /* use a specific cache file */
        sprintf(tmp, "cache,size=0x%0x,file=%s", cachePartitionSize, opts->cache);
        args[n++] = "-nand";
        args[n++] = strdup(tmp);
    }
    else if (!opts->nocache) {
        /* create a temporary cache partition file */
        sprintf(tmp, "cache,size=0x%0x", cachePartitionSize);
        args[n++] = "-nand";
        args[n++] = strdup(tmp);
    }

    if (hw->hw_sdCard != 0)
        opts->sdcard = (char*) avmInfo_getImageFile(android_vmInfo, AVM_IMAGE_SDCARD);
    else if (opts->sdcard) {
        dwarning( "Emulated hardware doesn't support SD Cards" );
        opts->sdcard = NULL;
    }

    if(opts->sdcard) {
        uint64_t  size;
        if (path_get_size(opts->sdcard, &size) == 0) {
            /* see if we have an sdcard image.  get its size if it exists */
            if (size < 8*1024*1024ULL) {
                fprintf(stderr, "### WARNING: SD Card files must be at least 8 MB, ignoring '%s'\n", opts->sdcard);
            } else {
                args[n++] = "-hda";
                args[n++] = opts->sdcard;
                use_sdcard_img = 1;
            }
        } else {
            D("no SD Card image at '%s'", opts->sdcard);
        }
    }

    if (!opts->logcat || opts->logcat[0] == 0) {
        opts->logcat = getenv("ANDROID_LOG_TAGS");
        if (opts->logcat && opts->logcat[0] == 0)
            opts->logcat = NULL;
    }

#if 0
    if (opts->console) {
        derror( "option -console is obsolete. please use -shell instead" );
        exit(1);
    }
#endif

    /* we always send the kernel messages from ttyS0 to android_kmsg */
    {
        AndroidKmsgFlags  flags = 0;

        if (opts->show_kernel)
            flags |= ANDROID_KMSG_PRINT_MESSAGES;

        android_kmsg_init( flags );
        args[n++] = "-serial";
        args[n++] = "android-kmsg";
        serial++;
    }

    /* XXXX: TODO: implement -shell and -logcat through qemud instead */
    if (!opts->shell_serial)
        opts->shell_serial = "stdio";
    else
        opts->shell = 1;

    if (opts->shell || opts->logcat) {
        args[n++] = "-serial";
        args[n++] = opts->shell_serial;
        shell_serial = serial++;
    }

    if (opts->old_system)
    {
        if (opts->radio) {
            args[n++] = "-serial";
            args[n++] = opts->radio;
            radio_serial = serial++;
        }
        else {
            args[n++] = "-serial";
            args[n++] = "android-modem";
            radio_serial = serial++;
        }
        if (opts->gps) {
            args[n++] = "-serial";
            args[n++] = opts->gps;
            gps_serial = serial++;
        }
    }
    else /* !opts->old_system */
    {
        args[n++] = "-serial";
        args[n++] = "android-qemud";
        qemud_serial = serial++;

        if (opts->radio) {
            CharDriverState*  cs = qemu_chr_open(opts->radio);
            if (cs == NULL) {
                derror( "unsupported character device specification: %s\n"
                        "used -help-char-devices for list of available formats\n", opts->radio );
                exit(1);
            }
            android_qemud_set_channel( ANDROID_QEMUD_GSM, cs);
        }
        else if ( hw->hw_gsmModem != 0 ) {
            if ( android_qemud_get_channel( ANDROID_QEMUD_GSM, &android_modem_cs ) < 0 ) {
                derror( "could not initialize qemud 'gsm' channel" );
                exit(1);
            }
        }

        if (opts->gps) {
            CharDriverState*  cs = qemu_chr_open(opts->gps);
            if (cs == NULL) {
                derror( "unsupported character device specification: %s\n"
                        "used -help-char-devices for list of available formats\n", opts->gps );
                exit(1);
            }
            android_qemud_set_channel( ANDROID_QEMUD_GPS, cs);
        }
        else if ( hw->hw_gps != 0 ) {
            if ( android_qemud_get_channel( "gps", &android_gps_cs ) < 0 ) {
                derror( "could not initialize qemud 'gps' channel" );
                exit(1);
            }
        }
    }

    if (opts->memory) {
        char*  end;
        long   ramSize = strtol(opts->memory, &end, 0);
        if (ramSize < 0 || *end != 0) {
            derror( "-memory must be followed by a positive integer" );
            exit(1);
        }
        if (ramSize < 32 || ramSize > 4096) {
            derror( "physical memory size must be between 32 and 4096 MB" );
            exit(1);
        }
    }
    if (!opts->memory) {
        bufprint(tmp, tmpend, "%d", hw->hw_ramSize);
        opts->memory = qemu_strdup(tmp);
    }

    if (opts->noaudio) {
        args[n++] = "-noaudio";
    }

#if 0
    if (opts->mic) {
        if (path_can_read(opts->mic)) {
            args[n++] = "-mic";
            args[n++] = opts->mic;
        } else {
            dprint("could not find or access audio input at '%s'", opts->mic);
        }
    }
#endif

    if (opts->trace) {
        args[n++] = "-trace";
        args[n++] = opts->trace;
        args[n++] = "-tracing";
        args[n++] = "off";
    }

    args[n++] = "-append";

    if (opts->bootchart) {
        char*  end;
        int    timeout = strtol(opts->bootchart, &end, 10);
        if (timeout == 0)
            opts->bootchart = NULL;
        else if (timeout < 0 || timeout > 15*60) {
            derror( "timeout specified for -bootchart option is invalid.\n"
                    "please use integers between 1 and 900\n");
            exit(1);
        }
    }

    {
        static char  params[1024];
        char        *p = params, *end = p + sizeof(params);

        p = bufprint(p, end, "qemu=1 console=ttyS0" );

        if (opts->shell || opts->logcat) {
            p = bufprint(p, end, " androidboot.console=ttyS%d", shell_serial );
        }

        if (opts->trace) {
            p = bufprint(p, end, " android.tracing=1");
        }

        if (!opts->nojni) {
            p = bufprint(p, end, " android.checkjni=1");
        }

        if (opts->no_boot_anim) {
            p = bufprint( p, end, " android.bootanim=0" );
        }

        if (opts->logcat) {
            char*  q = bufprint(p, end, " androidboot.logcat=%s", opts->logcat);

            if (q < end) {
                /* replace any space by a comma ! */
                {
                    int  nn;
                    for (nn = 1; p[nn] != 0; nn++)
                        if (p[nn] == ' ' || p[nn] == '\t')
                            p[nn] = ',';
                    p += nn;
                }
            }
            p = q;
        }

        if (opts->old_system)
        {
            p = bufprint(p, end, " android.ril=ttyS%d", radio_serial);

            if (opts->gps) {
                p = bufprint(p, end, " android.gps=ttyS%d", gps_serial);
            }
        }
        else
        {
            p = bufprint(p, end, " android.qemud=ttyS%d", qemud_serial);
        }

        if (dns_count > 0) {
            p = bufprint(p, end, " android.ndns=%d", dns_count);
        }

        if (opts->bootchart) {
            p = bufprint(p, end, " androidboot.bootchart=%s", opts->bootchart);
        }

        if (p >= end) {
            fprintf(stderr, "### ERROR: kernel parameters too long\n");
            exit(1);
        }

        args[n++] = strdup(params);
    }

    /* physical memory */
    args[n++] = "-m";
    args[n++] = opts->memory;

    while(argc-- > 0) {
        args[n++] = *argv++;
    }
    args[n] = 0;

    if(VERBOSE_CHECK(init)) {
        int i;
        for(i = 0; i < n; i++) {
            fprintf(stdout, "emulator: argv[%02d] = \"%s\"\n", i, args[i]);
        }
    }
    return qemu_main(n, args);
}

/* this function is called from qemu_main() once all arguments have been parsed
 * it should be used to setup any Android-specific items in the emulation before the
 * main loop runs
 */
void  android_emulation_setup( void )
{
    int   tries     = 16;
    int   base_port = 5554;
    int   success   = 0;
    int   s;
    struct in_addr guest_addr;
    AndroidOptions*  opts = qemulator->opts;

    inet_aton("10.0.2.15", &guest_addr);

#if 0
    if (opts->adb_port) {
        fprintf( stderr, "option -adb-port is obsolete, use -port instead\n" );
        exit(1);
    }
#endif

    if (opts->port && opts->ports) {
        fprintf( stderr, "options -port and -ports cannot be used together.\n");
        exit(1);
    }

    if (opts->ports) {
        char* comma_location;
        char* end;
        int console_port = strtol( opts->ports, &comma_location, 0 );

        if ( comma_location == NULL || *comma_location != ',' ) {
            derror( "option -ports must be followed by two comma separated positive integer numbers" );
            exit(1);
        }

        int adb_port = strtol( comma_location+1, &end, 0 );

        if ( end == NULL || *end ) {
            derror( "option -ports must be followed by two comma separated positive integer numbers" );
            exit(1);
        }

        if ( console_port == adb_port ) {
            derror( "option -ports must be followed by two different integer numbers" );
            exit(1);
        }

        slirp_redir( 0, adb_port, guest_addr, 5555 );
        if ( control_console_start( console_port ) < 0 ) {
            slirp_unredir( 0, adb_port );
        }

        base_port = console_port;
    } else {
        if (opts->port) {
            char*  end;
            int    port = strtol( opts->port, &end, 0 );
            if ( end == NULL || *end ||
                (unsigned)((port - base_port) >> 1) >= (unsigned)tries ) {
                derror( "option -port must be followed by an even integer number between %d and %d\n",
                        base_port, base_port + (tries-1)*2 );
                exit(1);
            }
            if ( (port & 1) != 0 ) {
                port &= ~1;
                dwarning( "option -port must be followed by an even integer, using  port number %d\n",
                          port );
            }
            base_port = port;
            tries     = 1;
        }

        for ( ; tries > 0; tries--, base_port += 2 ) {

            /* setup first redirection for ADB, the Android Debug Bridge */
            if ( slirp_redir( 0, base_port+1, guest_addr, 5555 ) < 0 )
                continue;

            /* setup second redirection for the emulator console */
            if ( control_console_start( base_port ) < 0 ) {
                slirp_unredir( 0, base_port+1 );
                continue;
            }

            D( "control console listening on port %d, ADB on port %d", base_port, base_port+1 );
            success = 1;
            break;
        }

        if (!success) {
            fprintf(stderr, "it seems too many emulator instances are running on this machine. Aborting\n" );
            exit(1);
        }
    }

    if (opts->report_console) {
        report_console(opts->report_console, base_port);
    }

    android_modem_init( base_port );

    android_base_port = base_port;
   /* send a simple message to the ADB host server to tell it we just started.
    * it should be listening on port 5037. if we can't reach it, don't bother
    */
    do
    {
        struct sockaddr_in  addr;
        char                tmp[32];
        int                 ret;

        s = socket(PF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            D("can't create socket to talk to the ADB server");
            break;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(5037);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        do {
            ret = connect(s, (struct sockaddr*)&addr, sizeof(addr));
        } while (ret < 0 && socket_errno == EINTR);

        if (ret < 0) {
            D("can't connect to ADB server: errno %d: %s", socket_errno, socket_errstr() );
            break;
        }

        sprintf(tmp,"0012host:emulator:%d",base_port+1);

        while ( send(s, tmp, 18+4, 0) < 0 && errno == EINTR ) {
        };
        D("sent '%s' to ADB server", tmp);
    }
    while (0);
    if (s >= 0)
        close(s);

    /* setup the http proxy, if any */
    if (VERBOSE_CHECK(proxy))
        proxy_set_verbose(1);

    if (!opts->http_proxy) {
        opts->http_proxy = getenv("http_proxy");
    }

    do
    {
        const char*  env = opts->http_proxy;
        int          envlen;
        ProxyOption  option_tab[4];
        ProxyOption* option = option_tab;
        char*        p;
        char*        q;
        const char*  proxy_name;
        int          proxy_name_len;
        int          proxy_port;

        if (!env)
            break;

        envlen = strlen(env);

        /* skip the 'http://' header, if present */
        if (envlen >= 7 && !memcmp(env, "http://", 7)) {
            env    += 7;
            envlen -= 7;
        }

        /* do we have a username:password pair ? */
        p = strchr(env, '@');
        if (p != 0) {
            q = strchr(env, ':');
            if (q == NULL) {
            BadHttpProxyFormat:
                dprint("http_proxy format unsupported, try 'proxy:port' or 'username:password@proxy:port'");
                break;
            }

            option->type       = PROXY_OPTION_AUTH_USERNAME;
            option->string     = env;
            option->string_len = q - env;
            option++;

            option->type       = PROXY_OPTION_AUTH_PASSWORD;
            option->string     = q+1;
            option->string_len = p - (q+1);
            option++;

            env = p+1;
        }

        p = strchr(env,':');
        if (p == NULL)
            goto BadHttpProxyFormat;

        proxy_name     = env;
        proxy_name_len = p - env;
        proxy_port     = atoi(p+1);

        D( "setting up http proxy:  server=%.*s port=%d",
                proxy_name_len, proxy_name, proxy_port );

        if ( proxy_http_setup( proxy_name, proxy_name_len, proxy_port,
                               option - option_tab, option_tab ) < 0 )
        {
            dprint( "http proxy setup failed, check your $http_proxy variable");
        }
    }
    while (0);

   /* cool, now try to run the "ddms ping" command, which will take care of pinging usage
    * if the user agreed for it. the emulator itself never sends anything to any outside
    * machine
    */
    {
#ifdef _WIN32
#  define  _ANDROID_PING_PROGRAM   "ddms.bat"
#else
#  define  _ANDROID_PING_PROGRAM   "ddms"
#endif

        char         tmp[PATH_MAX];
        const char*  appdir = get_app_dir();

        if (snprintf( tmp, PATH_MAX, "%s%s%s", appdir, PATH_SEP,
                      _ANDROID_PING_PROGRAM ) >= PATH_MAX) {
            dprint( "Application directory too long: %s", appdir);
            return;
        }

        /* if the program isn't there, don't bother */
        D( "ping program: %s", tmp);
        if (path_exists(tmp)) {
#ifdef _WIN32
            STARTUPINFO           startup;
            PROCESS_INFORMATION   pinfo;

            ZeroMemory( &startup, sizeof(startup) );
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_SHOWMINIMIZED;

            ZeroMemory( &pinfo, sizeof(pinfo) );

            char* comspec = getenv("COMSPEC");
            if (!comspec) comspec = "cmd.exe";

            // Run
            char args[PATH_MAX + 30];
            if (snprintf( args, PATH_MAX, "/C \"%s\" ping emulator " VERSION_STRING,
                          tmp) >= PATH_MAX ) {
                D( "DDMS path too long: %s", tmp);
                return;
            }

            CreateProcess(
                comspec,                                      /* program path */
                args,                                    /* command line args */
                NULL,                    /* process handle is not inheritable */
                NULL,                     /* thread handle is not inheritable */
                FALSE,                       /* no, don't inherit any handles */
                DETACHED_PROCESS,   /* the new process doesn't have a console */
                NULL,                       /* use parent's environment block */
                NULL,                      /* use parent's starting directory */
                &startup,                   /* startup info, i.e. std handles */
                &pinfo );

            D( "ping command: %s %s", comspec, args );
#else
            int  pid = fork();
            if (pid == 0) {
                int  fd = open("/dev/null", O_WRONLY);
                dup2(fd, 1);
                dup2(fd, 2);
                execl( tmp, _ANDROID_PING_PROGRAM, "ping", "emulator", VERSION_STRING, NULL );
            }
            /* don't do anything in the parent or in case of error */
            strncat( tmp, " ping emulator " VERSION_STRING, PATH_MAX - strlen(tmp) );
            D( "ping command: %s", tmp );
#endif
        }
    }
}


void  android_emulation_teardown( void )
{
}
