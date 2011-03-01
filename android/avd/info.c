/* Copyright (C) 2008 The Android Open Source Project
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
#include "android/avd/info.h"
#include "android/config/config.h"
#include "android/utils/path.h"
#include "android/utils/bufprint.h"
#include "android/utils/filelock.h"
#include "android/utils/tempfile.h"
#include "android/utils/debug.h"
#include "android/utils/dirscanner.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

/* global variables - see android/globals.h */
AvdInfoParams   android_avdParams[1];
AvdInfo*        android_avdInfo;

/* for debugging */
#define  D(...)   VERBOSE_PRINT(init,__VA_ARGS__)
#define  DD(...)  VERBOSE_PRINT(avd_config,__VA_ARGS__)

/* technical note on how all of this is supposed to work:
 *
 * Each AVD corresponds to a "content directory" that is used to
 * store persistent disk images and configuration files. Most remarkable
 * are:
 *
 * - a "config.ini" file used to hold configuration information for the
 *   AVD
 *
 * - mandatory user data image ("userdata-qemu.img") and cache image
 *   ("cache.img")
 *
 * - optional mutable system image ("system-qemu.img"), kernel image
 *   ("kernel-qemu") and read-only ramdisk ("ramdisk.img")
 *
 * When starting up an AVD, the emulator looks for relevant disk images
 * in the content directory. If it doesn't find a given image there, it
 * will try to search in the list of system directories listed in the
 * 'config.ini' file through one of the following (key,value) pairs:
 *
 *    images.sysdir.1 = <first search path>
 *    images.sysdir.2 = <second search path>
 *
 * The search paths can be absolute, or relative to the root SDK installation
 * path (which is determined from the emulator program's location, or from the
 * ANDROID_SDK_ROOT environment variable).
 *
 * Individual image disk search patch can be over-riden on the command-line
 * with one of the usual options.
 */

/* this is the subdirectory of $HOME/.android where all
 * root configuration files (and default content directories)
 * are located.
 */
#define  ANDROID_AVD_DIR    "avd"

/* the prefix of config.ini keys that will be used for search directories
 * of system images.
 */
#define  SEARCH_PREFIX   "image.sysdir."

/* the maximum number of search path keys we're going to read from the
 * config.ini file
 */
#define  MAX_SEARCH_PATHS  2

/* the config.ini key that will be used to indicate the full relative
 * path to the skin directory (including the skin name).
 */
#define  SKIN_PATH       "skin.path"

/* the config.ini key that will be used to indicate the default skin's name.
 * this is ignored if there is a valid SKIN_PATH entry in the file.
 */
#define  SKIN_NAME       "skin.name"

/* default skin name */
#define  SKIN_DEFAULT    "HVGA"

/* the config.ini key that is used to indicate the absolute path
 * to the SD Card image file, if you don't want to place it in
 * the content directory.
 */
#define  SDCARD_PATH     "sdcard.path"

/* the name of the .ini file that will contain the complete hardware
 * properties for the AVD. This will be used to launch the corresponding
 * core from the UI.
 */
#define  CORE_HARDWARE_INI   "hardware-qemu.ini"

/* certain disk image files are mounted read/write by the emulator
 * to ensure that several emulators referencing the same files
 * do not corrupt these files, we need to lock them and respond
 * to collision depending on the image type.
 *
 * the enumeration below is used to record information about
 * each image file path.
 *
 * READONLY means that the file will be mounted read-only
 * and this doesn't need to be locked. must be first in list
 *
 * MUSTLOCK means that the file should be locked before
 * being mounted by the emulator
 *
 * TEMPORARY means that the file has been copied to a
 * temporary image, which can be mounted read/write
 * but doesn't require locking.
 */
typedef enum {
    IMAGE_STATE_READONLY,     /* unlocked */
    IMAGE_STATE_MUSTLOCK,     /* must be locked */
    IMAGE_STATE_LOCKED,       /* locked */
    IMAGE_STATE_LOCKED_EMPTY, /* locked and empty */
    IMAGE_STATE_TEMPORARY,    /* copied to temp file (no lock needed) */
} AvdImageState;

struct AvdInfo {
    /* for the Android build system case */
    char      inAndroidBuild;
    char*     androidOut;
    char*     androidBuildRoot;
    char*     targetArch;

    /* for the normal virtual device case */
    char*     deviceName;
    char*     sdkRootPath;
    char      sdkRootPathFromEnv;
    char*     searchPaths[ MAX_SEARCH_PATHS ];
    int       numSearchPaths;
    char*     contentPath;
    IniFile*  rootIni;      /* root <foo>.ini file, empty if missing */
    IniFile*  configIni;    /* virtual device's config.ini, NULL if missing */
    IniFile*  skinHardwareIni;  /* skin-specific hardware.ini */

    /* for both */
    char*     skinName;     /* skin name */
    char*     skinDirPath;  /* skin directory */
    char*     coreHardwareIniPath;  /* core hardware.ini path */

    /* image files */
    char*     imagePath [ AVD_IMAGE_MAX ];
    char      imageState[ AVD_IMAGE_MAX ];
};


void
avdInfo_free( AvdInfo*  i )
{
    if (i) {
        int  nn;

        for (nn = 0; nn < AVD_IMAGE_MAX; nn++)
            AFREE(i->imagePath[nn]);

        AFREE(i->skinName);
        AFREE(i->skinDirPath);
        AFREE(i->coreHardwareIniPath);

        for (nn = 0; nn < i->numSearchPaths; nn++)
            AFREE(i->searchPaths[nn]);

        i->numSearchPaths = 0;

        if (i->configIni) {
            iniFile_free(i->configIni);
            i->configIni = NULL;
        }

        if (i->skinHardwareIni) {
            iniFile_free(i->skinHardwareIni);
            i->skinHardwareIni = NULL;
        }

        if (i->rootIni) {
            iniFile_free(i->rootIni);
            i->rootIni = NULL;
        }

        AFREE(i->contentPath);
        AFREE(i->sdkRootPath);

        if (i->inAndroidBuild) {
            AFREE(i->androidOut);
            AFREE(i->androidBuildRoot);
        }

        AFREE(i->deviceName);
        AFREE(i);
    }
}

/* list of default file names for each supported image file type */
static const char*  const  _imageFileNames[ AVD_IMAGE_MAX ] = {
#define  _AVD_IMG(x,y,z)  y,
    AVD_IMAGE_LIST
#undef _AVD_IMG
};

/* list of short text description for each supported image file type */
static const char*  const _imageFileText[ AVD_IMAGE_MAX ] = {
#define  _AVD_IMG(x,y,z)  z,
    AVD_IMAGE_LIST
#undef _AVD_IMG
};

/***************************************************************
 ***************************************************************
 *****
 *****    UTILITY FUNCTIONS
 *****
 *****  The following functions do not depend on the AvdInfo
 *****  structure and could easily be moved elsewhere.
 *****
 *****/

/* Return the path to the Android SDK root installation.
 *
 * (*pFromEnv) will be set to 1 if it comes from the $ANDROID_SDK_ROOT
 * environment variable, or 0 otherwise.
 *
 * Caller must free() returned string.
 */
static char*
_getSdkRoot( char *pFromEnv )
{
    const char*  env;
    char*        sdkPath;
    char         temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

    /* If ANDROID_SDK_ROOT is defined is must point to a directory
     * containing a valid SDK installation.
     */
#define  SDK_ROOT_ENV  "ANDROID_SDK_ROOT"

    env = getenv(SDK_ROOT_ENV);
    if (env != NULL && env[0] != 0) {
        if (path_exists(env)) {
            D("found " SDK_ROOT_ENV ": %s", env);
            *pFromEnv = 1;
            return ASTRDUP(env);
        }
        D(SDK_ROOT_ENV " points to unknown directory: %s", env);
    }

    *pFromEnv = 0;

    /* We assume the emulator binary is under tools/ so use its
     * parent as the Android SDK root.
     */
    (void) bufprint_app_dir(temp, end);
    sdkPath = path_parent(temp, 1);
    if (sdkPath == NULL) {
        derror("can't find root of SDK directory");
        return NULL;
    }
    D("found SDK root at %s", sdkPath);
    return sdkPath;
}

/* Parse a given config.ini file and extract the list of SDK search paths
 * from it. Returns the number of valid paths stored in 'searchPaths', or -1
 * in case of problem.
 *
 * Relative search paths in the config.ini will be stored as full pathnames
 * relative to 'sdkRootPath'.
 *
 * 'searchPaths' must be an array of char* pointers of at most 'maxSearchPaths'
 * entries.
 */
static int
_getSearchPaths( IniFile*    configIni,
                 const char* sdkRootPath,
                 int         maxSearchPaths,
                 char**      searchPaths )
{
    char  temp[PATH_MAX], *p = temp, *end= p+sizeof temp;
    int   nn, count = 0;

    for (nn = 0; nn < maxSearchPaths; nn++) {
        char*  path;

        p = bufprint(temp, end, "%s%d", SEARCH_PREFIX, nn+1 );
        if (p >= end)
            continue;

        path = iniFile_getString(configIni, temp, NULL);
        if (path != NULL) {
            DD("    found image search path: %s", path);
            if (!path_is_absolute(path)) {
                p = bufprint(temp, end, "%s/%s", sdkRootPath, path);
                AFREE(path);
                path = ASTRDUP(temp);
            }
            searchPaths[count++] = path;
        }
    }
    return count;
}

/* Check that an AVD name is valid. Returns 1 on success, 0 otherwise.
 */
static int
_checkAvdName( const char*  name )
{
    int  len  = strlen(name);
    int  len2 = strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789_.-");
    return (len == len2);
}

/* Return the path to the AVD's root configuration .ini file. it is located in
 * ~/.android/avd/<name>.ini or Windows equivalent
 *
 * This file contains the path to the AVD's content directory, which
 * includes its own config.ini.
 */
static char*
_getRootIniPath( const char*  avdName )
{
    char temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

    p = bufprint_config_path(temp, end);
    p = bufprint(p, end, "/" ANDROID_AVD_DIR "/%s.ini", avdName);
    if (p >= end) {
        return NULL;
    }
    if (!path_exists(temp)) {
        return NULL;
    }
    return ASTRDUP(temp);
}


/* Retrieves a string corresponding to the target architecture
 * when in the Android platform tree. The only way to do that
 * properly for now is to look at $OUT/system/build.prop:
 *
 *   ro.product.cpu-abi=<abi>
 *
 * Where <abi> can be 'armeabi', 'armeabi-v7a' or 'x86'.
 */
static const char*
_getBuildTargetArch( const char* androidOut )
{
    const char* arch = "arm";
    char temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);
    FILE*  file;

#define ABI_PREFIX       "ro.product.cpu.abi="
#define ABI_PREFIX_LEN   (sizeof(ABI_PREFIX)-1)

    p = bufprint(temp, end, "%s/system/build.prop", androidOut);
    if (p >= end) {
        D("%s: ANDROID_PRODUCT_OUT too long: %s", __FUNCTION__, androidOut);
        goto EXIT;
    }
    file = fopen(temp, "rb");
    if (file == NULL) {
        D("Could not open file: %s: %s", temp, strerror(errno));
        D("Default target architecture=%s", arch);
        goto EXIT;
    }

    while (fgets(temp, sizeof temp, file) != NULL) {
        /* Trim trailing newlines, if any */
        p = memchr(temp, '\0', sizeof temp);
        if (p == NULL)
            p = end;
        if (p > temp && p[-1] == '\n') {
            *--p = '\0';
        }
        if (p > temp && p[-1] == '\r') {
            *--p = '\0';
        }
        /* force zero-termination in case of full-buffer */
        if (p == end)
            *--p = '\0';

        if (memcmp(temp, ABI_PREFIX, ABI_PREFIX_LEN) != 0) {
            continue;
        }
        p = temp + ABI_PREFIX_LEN;
        if (p >= end || !*p)
            goto EXIT2;

        if (!strcmp("armeabi",p))
            arch = "arm";
        else if (!strcmp("armeabi-v7a",p))
            arch = "arm";
        else
            arch = p;

        D("Found target ABI=%s, architecture=%s", p, arch);
        goto EXIT2;
    }

    D("Could not find target architecture, defaulting to %s", arch);
EXIT2:
    fclose(file);
EXIT:
    return arch;
}

/* Returns the full path of a given file.
 *
 * If 'fileName' is an absolute path, this returns a simple copy.
 * Otherwise, this returns a new string corresponding to <rootPath>/<fileName>
 *
 * This returns NULL if the paths are too long.
 */
static char*
_getFullFilePath( const char* rootPath, const char* fileName )
{
    if (path_is_absolute(fileName)) {
        return ASTRDUP(fileName);
    } else {
        char temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

        p = bufprint(temp, end, "%s/%s", rootPath, fileName);
        if (p >= end) {
            return NULL;
        }
        return ASTRDUP(temp);
    }
}

/* check that a given directory contains a valid skin.
 * returns 1 on success, 0 on failure.
 */
static int
_checkSkinPath( const char*  skinPath )
{
    char  temp[MAX_PATH], *p=temp, *end=p+sizeof(temp);

    /* for now, if it has a 'layout' file, it is a valid skin path */
    p = bufprint(temp, end, "%s/layout", skinPath);
    if (p >= end || !path_exists(temp))
        return 0;

    return 1;
}

/* Check that there is a skin named 'skinName' listed from 'skinDirRoot'
 * this returns the full path of the skin directory (after alias expansions),
 * including the skin name, or NULL on failure.
 */
static char*
_checkSkinSkinsDir( const char*  skinDirRoot,
               const char*  skinName )
{
    DirScanner*  scanner;
    char*        result;
    char         temp[MAX_PATH], *p = temp, *end = p + sizeof(temp);

    p = bufprint(temp, end, "%s/skins/%s", skinDirRoot, skinName);
    if (p >= end || !path_exists(temp)) {
        DD("    ignore bad skin directory %s", temp);
        return NULL;
    }

    /* first, is this a normal skin directory ? */
    if (_checkSkinPath(temp)) {
        /* yes */
        DD("    found skin directory: %s", temp);
        return ASTRDUP(temp);
    }

    /* second, is it an alias to another skin ? */
    *p      = 0;
    result  = NULL;
    scanner = dirScanner_new(temp);
    if (scanner != NULL) {
        for (;;) {
            const char*  file = dirScanner_next(scanner);

            if (file == NULL)
                break;

            if (strncmp(file, "alias-", 6) || file[6] == 0)
                continue;

            p = bufprint(temp, end, "%s/skins/%s", skinDirRoot, file+6);
            if (p < end && _checkSkinPath(temp)) {
                /* yes, it's an alias */
                DD("    skin alias '%s' points to skin directory: %s",
                   file+6, temp);
                result = ASTRDUP(temp);
                break;
            }
        }
        dirScanner_free(scanner);
    }
    return result;
}

/* try to see if the skin name leads to a magic skin or skin path directly
 * returns 1 on success, 0 on error.
 *
 * on success, this sets up '*pSkinName' and '*pSkinDir'
 */
static int
_getSkinPathFromName( const char*  skinName,
                      const char*  sdkRootPath,
                      char**       pSkinName,
                      char**       pSkinDir )
{
    char  temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

    /* if the skin name has the format 'NNNNxNNN' where
    * NNN is a decimal value, then this is a 'magic' skin
    * name that doesn't require a skin directory
    */
    if (isdigit(skinName[0])) {
        int  width, height;
        if (sscanf(skinName, "%dx%d", &width, &height) == 2) {
            D("'magic' skin format detected: %s", skinName);
            *pSkinName = ASTRDUP(skinName);
            *pSkinDir  = NULL;
            return 1;
        }
    }

    /* is the skin name a direct path to the skin directory ? */
    if (path_is_absolute(skinName) && _checkSkinPath(skinName)) {
        goto FOUND_IT;
    }

    /* is the skin name a relative path from the SDK root ? */
    p = bufprint(temp, end, "%s/%s", sdkRootPath, skinName);
    if (p < end && _checkSkinPath(temp)) {
        skinName = temp;
        goto FOUND_IT;
    }

    /* nope */
    return 0;

FOUND_IT:
    if (path_split(skinName, pSkinDir, pSkinName) < 0) {
        derror("malformed skin name: %s", skinName);
        exit(2);
    }
    D("found skin '%s' in directory: %s", *pSkinName, *pSkinDir);
    return 1;
}

/***************************************************************
 ***************************************************************
 *****
 *****    NORMAL VIRTUAL DEVICE SUPPORT
 *****
 *****/

/* compute path to the root SDK directory
 * assume we are in $SDKROOT/tools/emulator[.exe]
 */
static int
_avdInfo_getSdkRoot( AvdInfo*  i )
{

    i->sdkRootPath = _getSdkRoot(&i->sdkRootPathFromEnv);
    if (i->sdkRootPath == NULL)
        return -1;

    return 0;
}

/* parse the root config .ini file. it is located in
 * ~/.android/avd/<name>.ini or Windows equivalent
 */
static int
_avdInfo_getRootIni( AvdInfo*  i )
{
    char*  iniPath = _getRootIniPath( i->deviceName );

    if (iniPath == NULL) {
        derror("unknown virtual device name: '%s'", i->deviceName);
        return -1;
    }

    D("Android virtual device file at: %s", iniPath);

    i->rootIni = iniFile_newFromFile(iniPath);
    AFREE(iniPath);

    if (i->rootIni == NULL) {
        derror("Corrupt virtual device config file!");
        return -1;
    }
    return 0;
}

/* Returns the AVD's content path, i.e. the directory that contains
 * the AVD's content files (e.g. data partition, cache, sd card, etc...).
 *
 * We extract this by parsing the root config .ini file, looking for
 * a "path" elements.
 */
static int
_avdInfo_getContentPath( AvdInfo*  i )
{
#   define  ROOT_PATH_KEY    "path"

    i->contentPath = iniFile_getString(i->rootIni, ROOT_PATH_KEY, NULL);

    if (i->contentPath == NULL) {
        derror("bad config: %s",
               "virtual device file lacks a "ROOT_PATH_KEY" entry");
        return -1;
    }
    D("virtual device content at %s", i->contentPath);
    return 0;
}

/* Look for a named file inside the AVD's content directory.
 * Returns NULL if it doesn't exist, or a strdup() copy otherwise.
 */
static char*
_avdInfo_getContentFilePath(AvdInfo*  i, const char* fileName)
{
    char temp[MAX_PATH], *p = temp, *end = p + sizeof(temp);

    p = bufprint(p, end, "%s/%s", i->contentPath, fileName);
    if (p >= end) {
        derror("can't access virtual device content directory");
        return NULL;
    }
    if (!path_exists(temp)) {
        return NULL;
    }
    return ASTRDUP(temp);
}

/* find and parse the config.ini file from the content directory */
static int
_avdInfo_getConfigIni(AvdInfo*  i)
{
    char*  iniPath = _avdInfo_getContentFilePath(i, "config.ini");

    /* Allow non-existing config.ini */
    if (iniPath == NULL) {
        D("virtual device has no config file - no problem");
        return 0;
    }

    D("virtual device config file: %s", iniPath);
    i->configIni = iniFile_newFromFile(iniPath);
    AFREE(iniPath);

    if (i->configIni == NULL) {
        derror("bad config: %s",
               "virtual device has corrupted config.ini");
        return -1;
    }
    return 0;
}

/* The AVD's config.ini contains a list of search paths (all beginning
 * with SEARCH_PREFIX) which are directory locations searched for
 * AVD platform files.
 */
static void
_avdInfo_getSearchPaths( AvdInfo*  i )
{
    if (i->configIni == NULL)
        return;

    i->numSearchPaths = _getSearchPaths( i->configIni,
                                         i->sdkRootPath,
                                         MAX_SEARCH_PATHS,
                                         i->searchPaths );
    if (i->numSearchPaths == 0) {
        derror("no search paths found in this AVD's configuration.\n"
               "Weird, the AVD's config.ini file is malformed. Try re-creating it.\n");
        exit(2);
    }
    else
        DD("found a total of %d search paths for this AVD", i->numSearchPaths);
}

/* Search a file in the SDK search directories. Return NULL if not found,
 * or a strdup() otherwise.
 */
static char*
_avdInfo_getSdkFilePath(AvdInfo*  i, const char*  fileName)
{
    char temp[MAX_PATH], *p = temp, *end = p + sizeof(temp);

    do {
        /* try the search paths */
        int  nn;

        for (nn = 0; nn < i->numSearchPaths; nn++) {
            const char* searchDir = i->searchPaths[nn];

            p = bufprint(temp, end, "%s/%s", searchDir, fileName);
            if (p < end && path_exists(temp)) {
                DD("found %s in search dir: %s", fileName, searchDir);
                goto FOUND;
            }
            DD("    no %s in search dir: %s", fileName, searchDir);
        }

        return NULL;

    } while (0);

FOUND:
    return ASTRDUP(temp);
}

/* Search for a file in the content directory, and if not found, in the
 * SDK search directory. Returns NULL if not found.
 */
static char*
_avdInfo_getContentOrSdkFilePath(AvdInfo*  i, const char*  fileName)
{
    char*  path;

    path = _avdInfo_getContentFilePath(i, fileName);
    if (path)
        return path;

    path = _avdInfo_getSdkFilePath(i, fileName);
    if (path)
        return path;

    return NULL;
}

#if 0
static int
_avdInfo_findContentOrSdkImage(AvdInfo* i, AvdImageType id)
{
    const char* fileName = _imageFileNames[id];
    char*       path     = _avdInfo_getContentOrSdkFilePath(i, fileName);

    i->imagePath[id]  = path;
    i->imageState[id] = IMAGE_STATE_READONLY;

    if (path == NULL)
        return -1;
    else
        return 0;
}
#endif

/* Returns path to the core hardware .ini file. This contains the
 * hardware configuration that is read by the core. The content of this
 * file is auto-generated before launching a core, but we need to know
 * its path before that.
 */
static int
_avdInfo_getCoreHwIniPath( AvdInfo* i, const char* basePath )
{
    i->coreHardwareIniPath = _getFullFilePath(basePath, CORE_HARDWARE_INI);
    if (i->coreHardwareIniPath == NULL) {
        DD("Path too long for %s: %s", CORE_HARDWARE_INI, basePath);
        return -1;
    }
    D("using core hw config path: %s", i->coreHardwareIniPath);
    return 0;
}


/***************************************************************
 ***************************************************************
 *****
 *****    KERNEL/DISK IMAGE LOADER
 *****
 *****/

/* a structure used to handle the loading of
 * kernel/disk images.
 */
typedef struct {
    AvdInfo*        info;
    AvdInfoParams*  params;
    AvdImageType    id;
    const char*     imageFile;
    const char*     imageText;
    char**          pPath;
    char*           pState;
    char            temp[PATH_MAX];
} ImageLoader;

static void
imageLoader_init( ImageLoader*  l, AvdInfo*  info, AvdInfoParams*  params )
{
    memset(l, 0, sizeof(*l));
    l->info    = info;
    l->params  = params;
}

/* set the type of the image to load */
static void
imageLoader_set( ImageLoader*  l, AvdImageType  id )
{
    l->id        = id;
    l->imageFile = _imageFileNames[id];
    l->imageText = _imageFileText[id];
    l->pPath     = &l->info->imagePath[id];
    l->pState    = &l->info->imageState[id];

    l->pState[0] = IMAGE_STATE_READONLY;
}

/* change the image path */
static char*
imageLoader_setPath( ImageLoader*  l, const char*  path )
{
    path = path ? ASTRDUP(path) : NULL;

    AFREE(l->pPath[0]);
    l->pPath[0] = (char*) path;

    return (char*) path;
}

static char*
imageLoader_extractPath( ImageLoader*  l )
{
    char*  result = l->pPath[0];
    l->pPath[0] = NULL;
    return result;
}

/* flags used when loading images */
enum {
    IMAGE_REQUIRED          = (1<<0),  /* image is required */
    IMAGE_SEARCH_SDK        = (1<<1),  /* search image in SDK */
    IMAGE_EMPTY_IF_MISSING  = (1<<2),  /* create empty file if missing */
    IMAGE_DONT_LOCK         = (1<<4),  /* don't try to lock image */
    IMAGE_IGNORE_IF_LOCKED  = (1<<5),  /* ignore file if it's locked */
};

#define  IMAGE_OPTIONAL  0

/* find an image from the SDK search directories.
 * returns the full path or NULL if the file could not be found.
 *
 * note: this stores the result in the image's path as well
 */
static char*
imageLoader_lookupSdk( ImageLoader*  l  )
{
    AvdInfo*     i     = l->info;
    const char*  image = l->imageFile;
    char*        temp  = l->temp, *p = temp, *end = p + sizeof(l->temp);

    do {
        /* try the search paths */
        int  nn;

        for (nn = 0; nn < i->numSearchPaths; nn++) {
            const char* searchDir = i->searchPaths[nn];

            p = bufprint(temp, end, "%s/%s", searchDir, image);
            if (p < end && path_exists(temp)) {
                DD("found %s in search dir: %s", image, searchDir);
                goto FOUND;
            }
            DD("    no %s in search dir: %s", image, searchDir);
        }

        return NULL;

    } while (0);

FOUND:
    l->pState[0] = IMAGE_STATE_READONLY;

    return imageLoader_setPath(l, temp);
}

/* search for a file in the content directory.
 * returns NULL if the file cannot be found.
 *
 * note that this formats l->temp with the file's path
 * allowing you to retrieve it if the function returns NULL
 */
static char*
imageLoader_lookupContent( ImageLoader*  l )
{
    AvdInfo*  i     = l->info;
    char*     temp  = l->temp, *p = temp, *end = p + sizeof(l->temp);

    p = bufprint(temp, end, "%s/%s", i->contentPath, l->imageFile);
    if (p >= end) {
        derror("content directory path too long");
        exit(2);
    }
    if (!path_exists(temp)) {
        DD("    no %s in content directory", l->imageFile);
        return NULL;
    }
    DD("found %s in content directory", l->imageFile);

    /* assume content image files must be locked */
    l->pState[0] = IMAGE_STATE_MUSTLOCK;

    return imageLoader_setPath(l, temp);
}

/* lock a file image depending on its state and user flags
 * note that this clears l->pPath[0] if the lock could not
 * be acquired and that IMAGE_IGNORE_IF_LOCKED is used.
 */
static void
imageLoader_lock( ImageLoader*  l, unsigned  flags )
{
    const char*  path = l->pPath[0];

    if (flags & IMAGE_DONT_LOCK)
        return;

    if (l->pState[0] != IMAGE_STATE_MUSTLOCK)
        return;

    D("    locking %s image at %s", l->imageText, path);

    if (filelock_create(path) != NULL) {
        /* succesful lock */
        l->pState[0] = IMAGE_STATE_LOCKED;
        return;
    }

    if (flags & IMAGE_IGNORE_IF_LOCKED) {
        dwarning("ignoring locked %s image at %s", l->imageText, path);
        imageLoader_setPath(l, NULL);
        return;
    }

    derror("the %s image is used by another emulator. aborting",
            l->imageText);
    exit(2);
}

/* make a file image empty, this may require locking */
static void
imageLoader_empty( ImageLoader*  l, unsigned  flags )
{
    const char*  path;

    imageLoader_lock(l, flags);

    path = l->pPath[0];
    if (path == NULL)  /* failed to lock, caller will handle it */
        return;

    if (path_empty_file(path) < 0) {
        derror("could not create %s image at %s: %s",
                l->imageText, path, strerror(errno));
        exit(2);
    }
    l->pState[0] = IMAGE_STATE_LOCKED_EMPTY;
}


/* copy image file from a given source
 * assumes locking is needed.
 */
static void
imageLoader_copyFrom( ImageLoader*  l, const char*  srcPath )
{
    const char*  dstPath = NULL;

    /* find destination file */
    if (l->params) {
        dstPath = l->params->forcePaths[l->id];
    }
    if (!dstPath) {
        imageLoader_lookupContent(l);
        dstPath = l->temp;
    }

    /* lock destination */
    imageLoader_setPath(l, dstPath);
    l->pState[0] = IMAGE_STATE_MUSTLOCK;
    imageLoader_lock(l, 0);

    /* make the copy */
    if (path_copy_file(dstPath, srcPath) < 0) {
        derror("can't initialize %s image from SDK: %s: %s",
               l->imageText, dstPath, strerror(errno));
        exit(2);
    }
}

/* this will load and eventually lock and image file, depending
 * on the flags being used. on exit, this function udpates
 * l->pState[0] and l->pPath[0]
 *
 * returns the path to the file. Note that it returns NULL
 * only if the file was optional and could not be found.
 *
 * if the file is required and missing, the function aborts
 * the program.
 */
static char*
imageLoader_load( ImageLoader*    l,
                  unsigned        flags )
{
    const char*  path = NULL;

    /* set image state */
    l->pState[0] = (flags & IMAGE_DONT_LOCK) == 0
                 ? IMAGE_STATE_MUSTLOCK
                 : IMAGE_STATE_READONLY;

    /* check user-provided path */
    path = l->params->forcePaths[l->id];
    if (path != NULL) {
        imageLoader_setPath(l, path);
        if (path_exists(path)) {
            DD("found user-provided %s image: %s", l->imageText, l->imageFile);
            goto EXIT;
        }
        D("user-provided %s image does not exist: %s",
          l->imageText, path);

        /* if the file is required, abort */
        if (flags & IMAGE_REQUIRED) {
            derror("user-provided %s image at %s doesn't exist",
                    l->imageText, path);
            exit(2);
        }
    }
    else {
        const char*  contentFile;

        /* second, look in the content directory */
        path = imageLoader_lookupContent(l);
        if (path) goto EXIT;

        contentFile = ASTRDUP(l->temp);

        /* it's not there */
        if (flags & IMAGE_SEARCH_SDK) {
            /* third, look in the SDK directory */
            path = imageLoader_lookupSdk(l);
            if (path) {
                AFREE((char*)contentFile);
                goto EXIT;
            }
        }
        DD("found no %s image (%s)", l->imageText, l->imageFile);

        /* if the file is required, abort */
        if (flags & IMAGE_REQUIRED) {
            AvdInfo*  i = l->info;

            derror("could not find required %s image (%s).",
                   l->imageText, l->imageFile);

            if (i->inAndroidBuild) {
                dprint( "Did you build everything ?" );
            } else if (!i->sdkRootPathFromEnv) {
                dprint( "Maybe defining %s to point to a valid SDK "
                        "installation path might help ?", SDK_ROOT_ENV );
            } else {
                dprint( "Your %s is probably wrong: %s", SDK_ROOT_ENV,
                        i->sdkRootPath );
            }
            exit(2);
        }

        path = imageLoader_setPath(l, contentFile);
        AFREE((char*)contentFile);
    }

    /* otherwise, do we need to create it ? */
    if (flags & IMAGE_EMPTY_IF_MISSING) {
        imageLoader_empty(l, flags);
        return l->pPath[0];
    }
    return NULL;

EXIT:
    imageLoader_lock(l, flags);
    return l->pPath[0];
}

/* Attempts to load an AVD image, but does not kill the process if loading
 * fails.
 */
static void
imageLoader_loadOptional( ImageLoader *l, AvdImageType img_type,
                           const char *forcedPath )
{
    imageLoader_set (l, img_type);
    imageLoader_load(l, IMAGE_OPTIONAL |
                        IMAGE_IGNORE_IF_LOCKED);

    /* if the file was not found, ignore it */
    if (l->pPath[0] && !path_exists(l->pPath[0]))
    {
        D("ignoring non-existing %s at %s: %s",
          l->imageText, l->pPath[0], strerror(errno));

        /* if the user provided the path by hand, warn him. */
        if (forcedPath != NULL)
            dwarning("ignoring non-existing %s image", l->imageText);

        imageLoader_setPath(l, NULL);
    }
}

/* find the correct path of all image files we're going to need
 * and lock the files that need it.
 */
static int
_avdInfo_getImagePaths(AvdInfo*  i, AvdInfoParams*  params )
{
    int   wipeData    = (params->flags & AVDINFO_WIPE_DATA) != 0;
    int   noSnapshots = (params->flags & AVDINFO_NO_SNAPSHOTS) != 0;

    ImageLoader  l[1];

    imageLoader_init(l, i, params);

    /* the system image
     *
     * if there is one in the content directory just lock
     * and use it.
     */
    imageLoader_set ( l, AVD_IMAGE_INITSYSTEM );
    imageLoader_load( l, IMAGE_REQUIRED | IMAGE_SEARCH_SDK | IMAGE_DONT_LOCK );

    /* the data partition - this one is special because if it
     * is missing, we need to copy the initial image file into it.
     *
     * first, try to see if it is in the content directory
     * (or the user-provided path)
     */
    imageLoader_set( l, AVD_IMAGE_USERDATA );
    if ( !imageLoader_load( l, IMAGE_OPTIONAL |
                               IMAGE_DONT_LOCK ) )
    {
        /* it's not, we're going to initialize it. simply
         * forcing a data wipe should be enough */
        D("initializing new data partition image: %s", l->pPath[0]);
        wipeData = 1;
    }

    if (wipeData) {
        /* find SDK source file */
        const char*  srcPath;

        imageLoader_set( l, AVD_IMAGE_INITDATA );
        if (imageLoader_lookupSdk(l) == NULL) {
            derror("can't locate initial %s image in SDK",
                l->imageText);
            exit(2);
        }
        srcPath = imageLoader_extractPath(l);

        imageLoader_set( l, AVD_IMAGE_USERDATA );
        imageLoader_copyFrom( l, srcPath );
        AFREE((char*) srcPath);
    }
    else
    {
        /* lock the data partition image */
        l->pState[0] = IMAGE_STATE_MUSTLOCK;
        imageLoader_lock( l, 0 );
    }

    /* the state snapshot image. Mounting behaviour identical to
     * SD card.
     */
    if (!noSnapshots) {
        imageLoader_loadOptional(l, AVD_IMAGE_SNAPSHOTS,
                                 params->forcePaths[AVD_IMAGE_SNAPSHOTS]);
    }

    return 0;
}

/* If the user didn't explicitely provide an SD Card path,
 * check the SDCARD_PATH key in config.ini and use that if
 * available.
 */
static void
_avdInfo_getSDCardPath( AvdInfo*  i, AvdInfoParams*  params )
{
    const char*  path;

    if (params->forcePaths[AVD_IMAGE_SDCARD] != NULL)
        return;

    path = iniFile_getString(i->configIni, SDCARD_PATH, NULL);
    if (path == NULL)
        return;

    params->forcePaths[AVD_IMAGE_SDCARD] = path;
}

AvdInfo*
avdInfo_new( const char*  name, AvdInfoParams*  params )
{
    AvdInfo*  i;

    if (name == NULL)
        return NULL;

    if (!_checkAvdName(name)) {
        derror("virtual device name contains invalid characters");
        exit(1);
    }

    ANEW0(i);
    i->deviceName = ASTRDUP(name);

    if ( _avdInfo_getSdkRoot(i) < 0     ||
         _avdInfo_getRootIni(i) < 0     ||
         _avdInfo_getContentPath(i) < 0 ||
         _avdInfo_getConfigIni(i)   < 0 ||
         _avdInfo_getCoreHwIniPath(i, i->contentPath) < 0 )
        goto FAIL;

    /* look for image search paths. handle post 1.1/pre cupcake
     * obsolete SDKs.
     */
    _avdInfo_getSearchPaths(i);
    _avdInfo_getSDCardPath(i, params);

    /* don't need this anymore */
    iniFile_free(i->rootIni);
    i->rootIni = NULL;

    if ( _avdInfo_getImagePaths(i, params) < 0 )
        goto FAIL;

    return i;

FAIL:
    avdInfo_free(i);
    return NULL;
}

/***************************************************************
 ***************************************************************
 *****
 *****    ANDROID BUILD SUPPORT
 *****
 *****    The code below corresponds to the case where we're
 *****    starting the emulator inside the Android build
 *****    system. The main differences are that:
 *****
 *****    - the $ANDROID_PRODUCT_OUT directory is used as the
 *****      content file.
 *****
 *****    - built images must not be modified by the emulator,
 *****      so system.img must be copied to a temporary file
 *****      and userdata.img must be copied to userdata-qemu.img
 *****      if the latter doesn't exist.
 *****
 *****    - the kernel and default skin directory are taken from
 *****      prebuilt
 *****
 *****    - there is no root .ini file, or any config.ini in
 *****      the content directory, no SDK images search path.
 *****/

static int
_avdInfo_getBuildImagePaths( AvdInfo*  i, AvdInfoParams*  params )
{
    int   wipeData    = (params->flags & AVDINFO_WIPE_DATA) != 0;
    int   noSnapshots = (params->flags & AVDINFO_NO_SNAPSHOTS) != 0;

    char         temp[PATH_MAX];
    char*        srcData;
    ImageLoader  l[1];

    imageLoader_init(l, i, params);

    /** load the data partition. note that we use userdata-qemu.img
     ** since we don't want to modify userdata.img at all
     **/
    imageLoader_set ( l, AVD_IMAGE_USERDATA );
    imageLoader_load( l, IMAGE_OPTIONAL | IMAGE_DONT_LOCK );

    /* get the path of the source file, and check that it actually exists
     * if the user didn't provide an explicit data file
     */
    srcData = imageLoader_extractPath(l);
    if (srcData == NULL && params->forcePaths[AVD_IMAGE_USERDATA] == NULL) {
        derror("There is no %s image in your build directory. Please make a full build",
                l->imageText, l->imageFile);
        exit(2);
    }

    /* get the path of the target file */
    l->imageFile = "userdata-qemu.img";
    imageLoader_load( l, IMAGE_OPTIONAL |
                         IMAGE_EMPTY_IF_MISSING |
                         IMAGE_IGNORE_IF_LOCKED );

    /* force a data wipe if we just created the image */
    if (l->pState[0] == IMAGE_STATE_LOCKED_EMPTY)
        wipeData = 1;

    /* if the image was already locked, create a temp file
     * then force a data wipe.
     */
    if (l->pPath[0] == NULL) {
        TempFile*  temp = tempfile_create();
        imageLoader_setPath(l, tempfile_path(temp));
        dwarning( "Another emulator is running. user data changes will *NOT* be saved");
        wipeData = 1;
    }

    /* in the case of a data wipe, copy userdata.img into
     * the destination */
    if (wipeData) {
        if (srcData == NULL || !path_exists(srcData)) {
            derror("There is no %s image in your build directory. Please make a full build",
                   l->imageText, _imageFileNames[l->id]);
            exit(2);
        }
        if (path_copy_file( l->pPath[0], srcData ) < 0) {
            derror("could not initialize %s image from %s: %s",
                   l->imageText, temp, strerror(errno));
            exit(2);
        }
    }

    AFREE(srcData);

    /** load the system image. read-only. the caller must
     ** take care of checking the state
     **/
    imageLoader_set ( l, AVD_IMAGE_INITSYSTEM );
    imageLoader_load( l, IMAGE_REQUIRED | IMAGE_DONT_LOCK );

    /* force the system image to read-only status */
    l->pState[0] = IMAGE_STATE_READONLY;

    /** State snapshots image
     **/
    if (!noSnapshots) {
        imageLoader_set (l, AVD_IMAGE_SNAPSHOTS);
        imageLoader_load(l, IMAGE_OPTIONAL | IMAGE_IGNORE_IF_LOCKED);
    }

    return 0;
}

/* Read a hardware.ini if it is located in the skin directory */
static int
_avdInfo_getBuildSkinHardwareIni( AvdInfo*  i )
{
    char* skinName;
    char* skinDirPath;
    char  temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

    avdInfo_getSkinInfo(i, &skinName, &skinDirPath);
    if (skinDirPath == NULL)
        return 0;

    p = bufprint(temp, end, "%s/%s/hardware.ini", skinDirPath, skinName);
    if (p >= end || !path_exists(temp)) {
        DD("no skin-specific hardware.ini in %s", skinDirPath);
        return 0;
    }

    D("found skin-specific hardware.ini: %s", temp);
    i->skinHardwareIni = iniFile_newFromFile(temp);
    if (i->skinHardwareIni == NULL)
        return -1;

    return 0;
}

AvdInfo*
avdInfo_newForAndroidBuild( const char*     androidBuildRoot,
                            const char*     androidOut,
                            AvdInfoParams*  params )
{
    AvdInfo*  i;

    ANEW0(i);

    i->inAndroidBuild   = 1;
    i->androidBuildRoot = ASTRDUP(androidBuildRoot);
    i->androidOut       = ASTRDUP(androidOut);
    i->contentPath      = ASTRDUP(androidOut);
    i->targetArch       = ASTRDUP(_getBuildTargetArch(i->androidOut));

    /* TODO: find a way to provide better information from the build files */
    i->deviceName = ASTRDUP("<build>");

    /* There is no config.ini in the build */
    i->configIni = NULL;

    if (_avdInfo_getBuildImagePaths(i, params) < 0 ||
        _avdInfo_getCoreHwIniPath(i, i->androidOut) < 0 )
        goto FAIL;

    /* Read the build skin's hardware.ini, if any */
    _avdInfo_getBuildSkinHardwareIni(i);

    return i;

FAIL:
    avdInfo_free(i);
    return NULL;
}

const char*
avdInfo_getName( AvdInfo*  i )
{
    return i ? i->deviceName : NULL;
}

const char*
avdInfo_getImageFile( AvdInfo*  i, AvdImageType  imageType )
{
    if (i == NULL || (unsigned)imageType >= AVD_IMAGE_MAX)
        return NULL;

    return i->imagePath[imageType];
}

uint64_t
avdInfo_getImageFileSize( AvdInfo*  i, AvdImageType  imageType )
{
    const char* file = avdInfo_getImageFile(i, imageType);
    uint64_t    size;

    if (file == NULL)
        return 0ULL;

    if (path_get_size(file, &size) < 0)
        return 0ULL;

    return size;
}

int
avdInfo_isImageReadOnly( AvdInfo*  i, AvdImageType  imageType )
{
    if (i == NULL || (unsigned)imageType >= AVD_IMAGE_MAX)
        return 1;

    return (i->imageState[imageType] == IMAGE_STATE_READONLY);
}

char*
avdInfo_getKernelPath( AvdInfo*  i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_KERNEL ];

    char*  kernelPath = _avdInfo_getContentOrSdkFilePath(i, imageName);

    if (kernelPath == NULL && i->inAndroidBuild) {
        /* When in the Android build, look into the prebuilt directory
         * for our target architecture.
         */
        char temp[PATH_MAX], *p = temp, *end = p + sizeof(temp);

        p = bufprint(temp, end, "%s/prebuilt/android-%s/kernel/kernel-qemu",
                     i->androidBuildRoot, i->targetArch);
        if (p >= end || !path_exists(temp)) {
            derror("bad workspace: cannot find prebuilt kernel in: %s", temp);
            exit(1);
        }
        kernelPath = ASTRDUP(temp);
    }
    return kernelPath;
}


char*
avdInfo_getRamdiskPath( AvdInfo* i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_RAMDISK ];
    return _avdInfo_getContentOrSdkFilePath(i, imageName);
}

char*  avdInfo_getCachePath( AvdInfo*  i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_CACHE ];
    return _avdInfo_getContentFilePath(i, imageName);
}

char*  avdInfo_getDefaultCachePath( AvdInfo*  i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_CACHE ];
    return _getFullFilePath(i->contentPath, imageName);
}

char*  avdInfo_getSdCardPath( AvdInfo* i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_SDCARD ];
    char*       path;

    /* Special case, the config.ini can have a SDCARD_PATH entry
     * that gives the full path to the SD Card.
     */
    if (i->configIni != NULL) {
        path = iniFile_getString(i->configIni, SDCARD_PATH, NULL);
        if (path != NULL) {
            if (path_exists(path))
                return path;

            dwarning("Ignoring invalid SDCard path: %s", path);
            AFREE(path);
        }
    }

    /* Otherwise, simply look into the content directory */
    return _avdInfo_getContentFilePath(i, imageName);
}

char*
avdInfo_getSystemInitImagePath( AvdInfo*  i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_INITSYSTEM ];
    return _avdInfo_getContentOrSdkFilePath(i, imageName);
}

char*
avdInfo_getDataInitImagePath( AvdInfo* i )
{
    const char* imageName = _imageFileNames[ AVD_IMAGE_INITDATA ];
    return _avdInfo_getContentOrSdkFilePath(i, imageName);
}

int
avdInfo_getHwConfig( AvdInfo*  i, AndroidHwConfig*  hw )
{
    IniFile*   ini = i->configIni;
    int        ret;

    if (ini == NULL)
        ini = iniFile_newFromMemory("", 0);

    ret = androidHwConfig_read(hw, ini);

    if (ini != i->configIni)
        iniFile_free(ini);

    if (ret == 0 && i->skinHardwareIni != NULL) {
        ret = androidHwConfig_read(hw, i->skinHardwareIni);
    }

    /* special product-specific hardware configuration */
    if (i->androidOut != NULL)
    {
        char*  p = strrchr(i->androidOut, '/');
        if (p != NULL && p[0] != 0) {
            if (p[1] == 's') {
                hw->hw_keyboard = 0;
            }
        }
    }

    return ret;
}

const char*
avdInfo_getContentPath( AvdInfo*  i )
{
    return i->contentPath;
}

int
avdInfo_inAndroidBuild( AvdInfo*  i )
{
    return i->inAndroidBuild;
}

char*
avdInfo_getTracePath( AvdInfo*  i, const char*  traceName )
{
    char   tmp[MAX_PATH], *p=tmp, *end=p + sizeof(tmp);

    if (i == NULL || traceName == NULL || traceName[0] == 0)
        return NULL;

    if (i->inAndroidBuild) {
        p = bufprint( p, end, "%s" PATH_SEP "traces" PATH_SEP "%s",
                      i->androidOut, traceName );
    } else {
        p = bufprint( p, end, "%s" PATH_SEP "traces" PATH_SEP "%s",
                      i->contentPath, traceName );
    }
    return ASTRDUP(tmp);
}

const char*
avdInfo_getCoreHwIniPath( AvdInfo* i )
{
    return i->coreHardwareIniPath;
}


void
avdInfo_getSkinInfo( AvdInfo*  i, char** pSkinName, char** pSkinDir )
{
    char*  skinName = NULL;
    char*  skinPath;
    char   temp[PATH_MAX], *p=temp, *end=p+sizeof(temp);

    *pSkinName = NULL;
    *pSkinDir  = NULL;

    /* First, see if the config.ini contains a SKIN_PATH entry that
     * names the full directory path for the skin.
     */
    if ( i->configIni != NULL ) {
        skinPath = iniFile_getString( i->configIni, SKIN_PATH, NULL );
        if (skinPath != NULL) {
            /* If this skin name is magic or a direct directory path
            * we have our result right here.
            */
            if (_getSkinPathFromName(skinPath, i->sdkRootPath,
                                     pSkinName, pSkinDir )) {
                AFREE(skinPath);
                return;
            }
        }

        /* The SKIN_PATH entry was not valid, so look at SKIN_NAME */
        D("Warning: config.ini contains invalid %s entry: %s", SKIN_PATH, skinPath);
        AFREE(skinPath);

        skinName = iniFile_getString( i->configIni, SKIN_NAME, NULL );
    }

    if (skinName == NULL) {
        /* If there is no skin listed in the config.ini, try to see if
         * there is one single 'skin' directory in the content directory.
         */
        p = bufprint(temp, end, "%s/skin", i->contentPath);
        if (p < end && _checkSkinPath(temp)) {
            D("using skin content from %s", temp);
            AFREE(i->skinName);
            *pSkinName = ASTRDUP("skin");
            *pSkinDir  = ASTRDUP(i->contentPath);
            return;
        }

        /* otherwise, use the default name */
        skinName = ASTRDUP(SKIN_DEFAULT);
    }

    /* now try to find the skin directory for that name -
     */
    do {
        /* first try the content directory, i.e. $CONTENT/skins/<name> */
        skinPath = _checkSkinSkinsDir(i->contentPath, skinName);
        if (skinPath != NULL)
            break;

#define  PREBUILT_SKINS_ROOT "development/tools/emulator"

        /* if we are in the Android build, try the prebuilt directory */
        if (i->inAndroidBuild) {
            p = bufprint( temp, end, "%s/%s",
                        i->androidBuildRoot, PREBUILT_SKINS_ROOT );
            if (p < end) {
                skinPath = _checkSkinSkinsDir(temp, skinName);
                if (skinPath != NULL)
                    break;
            }
        }

        /* look in the search paths. For each <dir> in the list,
         * look into <dir>/../skins/<name>/ */
        {
            int  nn;
            for (nn = 0; nn < i->numSearchPaths; nn++) {
                char*  parentDir = path_parent(i->searchPaths[nn], 1);
                if (parentDir == NULL)
                    continue;
                skinPath = _checkSkinSkinsDir(parentDir, skinName);
                AFREE(parentDir);
                if (skinPath != NULL)
                  break;
            }
            if (nn < i->numSearchPaths)
                break;
        }

        /* We didn't find anything ! */
        return;

    } while (0);

    if (path_split(skinPath, pSkinDir, pSkinName) < 0) {
        derror("weird skin path: %s", skinPath);
        AFREE(skinPath);
        return;
    }
    DD("found skin '%s' in directory: %s", *pSkinName, *pSkinDir);
    AFREE(skinPath);
    return;
}
