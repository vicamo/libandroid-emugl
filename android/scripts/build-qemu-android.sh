#!/bin/sh

# Copyright 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

. $(dirname "$0")/utils/common.shi

shell_import utils/aosp_dir.shi
shell_import utils/emulator_prebuilts.shi
shell_import utils/install_dir.shi
shell_import utils/option_parser.shi
shell_import utils/package_builder.shi
shell_import utils/package_list_parser.shi

PROGRAM_PARAMETERS="<qemu-android>"

PROGRAM_DESCRIPTION=\
"A script used to rebuild the 'qemu-android' binaries from sources.
This requires one parameter, which must be the location of the qemu-android
source tree to compile.

It will look for <prebuilts>/install/<host>/ for prebuilt libraries of the
qemu-android dependencies. These can be generated by using the
build-qemu-android-deps.sh script. Note that the default value
for <prebuilts> is /tmp/$USER-emulator-prebuilts/, but this can be changed
with the --prebuits-dir=<dir> option, or by defining the environment
variable ANDROID_EMULATOR_PREBUILTS_DIR.

On some platforms, the top-level checkout of the AOSP source tree is also
required to use SDK-specific toolchains. The script will try to auto-detect
one, otherwise use --aosp-dir=<path> or define ANDROID_EMULATOR_AOSP_DIR
in your environment.

By default, binaries are installed under <prebuilts>/install/<host>/
but this can be changed with --install-dir=<path>."

package_builder_register_options

VALID_TARGETS="arm,arm64,x86,x86_64,mips,mips64"
DEFAULT_TARGETS="arm64"

OPT_TARGET=
option_register_var "--target=<list>" OPT_TARGET \
        "Build binaries for targets [$DEFAULT_TARGETS]"

OPT_USE_SDL1=
option_register_var "--use-sdl1" "Use SDL 1.x instead of SDL 2.x"

OPT_SRC_DIR=
option_register_var "--src-dir=<dir>" OPT_SRC_DIR \
        "Set qemu-android source directory [autodetect]"

prebuilts_dir_register_option
aosp_dir_register_option
install_dir_register_option qemu-android

option_parse "$@"

if [ "$PARAMETER_COUNT" != 0 ]; then
    panic "This script takes no arguments. See --help for details."
fi

if [ "$OPT_SRC_DIR" ]; then
    QEMU_ANDROID=$OPT_SRC_DIR
else
    DEFAULT_SRC_DIR=$(cd $(program_directory)/../../../qemu-android && pwd -P 2>/dev/null || true)
    if [ "$DEFAULT_SRC_DIR" ]; then
        QEMU_ANDROID=$DEFAULT_SRC_DIR
        log "Auto-config: --src-dir=$QEMU_ANDROID"
    else
        panic "Could not find qemu-android sources. Please use --src-dir=<dir>."
    fi
fi
if [ ! -f "$QEMU_ANDROID/include/qemu-common.h" ]; then
    panic "Not a valid qemu-android source directory: $QEMU_ANDROID"
fi
QEMU_ANDROID=$(cd "$QEMU_ANDROID" && pwd -P)

prebuilts_dir_parse_option
aosp_dir_parse_option
install_dir_parse_option

package_builder_process_options qemu-android

##
## Handle target system list
##
if [ "$OPT_TARGET" ]; then
    TARGETS=$(commas_to_spaces "$OPT_TARGET")
else
    TARGETS=$(commas_to_spaces "$DEFAULT_TARGETS")
    log "Auto-config: --target='$TARGETS'"
fi

BAD_TARGETS=
for TARGET in $TARGETS; do
    if ! list_contains "$VALID_TARGETS" "$TARGET"; then
        BAD_TARGETS="$BAD_TARGETS $TARGET"
    fi
done
if [ "$BAD_TARGETS" ]; then
    panic "Invalid target name(s): [$BAD_TARGETS], use one of: $VALID_TARGETS"
fi

export PKG_CONFIG=$(which pkg-config 2>/dev/null)
if [ "$PKG_CONFIG" ]; then
    log "Found pkg-config at: $PKG_CONFIG"
else
    log "pkg-config is not installed on this system."
fi

# $1: host os name.
# $2: AOSP source directory
build_qemu_android () {
    builder_prepare_for_host $1 "$2"

    QEMU_TARGETS=
    QEMU_TARGET_LIST=
    QEMU_TARGET_BUILDS=
    for TARGET in $TARGETS; do
        case $TARGET in
            arm64)
                QEMU_TARGET=aarch64
                ;;
            x86)
                QEMU_TARGET=i386
                ;;
            mips)
                QEMU_TARGET=mipsel
                ;;
            mips64)
                QEMU_TARGET=mips64el
                ;;
            *)
                QEMU_TARGET=$TARGET
                ;;
        esac
        var_append QEMU_TARGETS "$QEMU_TARGET"
        var_append QEMU_TARGET_LIST "${QEMU_TARGET}-softmmu"
        var_append QEMU_TARGET_BUILDS "$QEMU_TARGET-softmmu/qemu-system-$QEMU_TARGET$(builder_host_exe_extension)"
    done

    dump "$(builder_text) Building qemu-android"
    log "Qemu targets: $QEMU_TARGETS"

    BUILD_DIR=$TEMP_DIR/build-$1/qemu-android
    (
        PREFIX=$INSTALL_DIR/$1

        BUILD_FLAGS=
        if [ "$(get_verbosity)" -gt 3 ]; then
            var_append BUILD_FLAGS "V=1"
        fi

        run mkdir -p "$BUILD_DIR"
        run rm -rf "$BUILD_DIR"/*
        run cd "$BUILD_DIR"
        EXTRA_LDFLAGS="-L$PREFIX/lib"
        case $1 in
           darwin-*)
               EXTRA_LDFLAGS="$EXTRA_LDFLAGS -Wl,-framework,Carbon"
               ;;
           *)
               EXTRA_LDFLAGS="$EXTRA_LDFLAGS -static-libgcc -static-libstdc++"
               ;;
        esac
        case $1 in
            windows-*)
                ;;
            *)
                EXTRA_LDFLAGS="$EXTRA_LDFLAGS -ldl -lm"
                ;;
        esac
        CROSS_PREFIX_FLAG=
        if [ "$(builder_gnu_config_host_prefix)" ]; then
            CROSS_PREFIX_FLAG="--cross-prefix=$(builder_gnu_config_host_prefix)"
        fi
        EXTRA_CFLAGS="-I$PREFIX/include"
        case $1 in
            windows-*)
                # Necessary to build version.rc properly.
                # VS_VERSION_INFO is a relatively new features that is
                # not supported by the cross-toolchain's windres tool.
                EXTRA_CFLAGS="$EXTRA_CFLAGS -DVS_VERSION_INFO=1"
                ;;
            darwin-*)
                EXTRA_CFLAGS="$EXTRA_CFLAGS -mmacosx-version-min=10.8"
                ;;
        esac

        PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
        PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
        case $1 in
            windows-*)
                # Use the host version, or the build will freeze.
                PKG_CONFIG=pkg-config
                ;;
            *)
                PKG_CONFIG=$PREFIX/bin/pkg-config
                ;;
        esac

        # Create a pkg-config wrapper that redefines the |prefix|
        # variable to the right location, then force its usage.
        cat > pkg-config <<EOF
#!/bin/sh
$PKG_CONFIG --define-variable=prefix=$PREFIX "\$@"
EOF
        chmod a+x pkg-config
        PKG_CONFIG=$(pwd -P)/pkg-config

        export PKG_CONFIG PKG_CONFIG_PATH PKG_CONFIG_LIBDIR

        # Export these to ensure that pkg-config picks them up properly.
        export GLIB_CFLAGS="-I$PREFIX/include/glib-2.0 -I$PREFIX/lib/glib-2.0/include"
        export GLIB_LIBS="$PREFIX/lib/libglib-2.0.la"
        case $BUILD_OS in
            darwin)
                GLIB_LIBS="$GLIB_LIBS -Wl,-framework,Carbon -Wl,-framework,Foundation"
                ;;
        esac

        if [ "$OPT_USE_SDL1" ]; then
            SDL_CONFIG=$PREFIX/bin/sdl-config
        else
            SDL_CONFIG=$PREFIX/bin/sdl2-config
        fi
        export SDL_CONFIG

        run $QEMU_ANDROID/configure \
            $CROSS_PREFIX_FLAG \
            --target-list="$QEMU_TARGET_LIST" \
            --prefix=$PREFIX \
            --extra-cflags="$EXTRA_CFLAGS" \
            --extra-ldflags="$EXTRA_LDFLAGS" \
            --disable-attr \
            --disable-blobs \
            --disable-cap-ng \
            --disable-curses \
            --disable-docs \
            --disable-glusterfs \
            --disable-gtk \
            --disable-guest-agent \
            --disable-libnfs \
            --disable-libiscsi \
            --disable-libssh2 \
            --disable-libusb \
            --disable-quorum \
            --disable-seccomp \
            --disable-spice \
            --disable-smartcard-nss \
            --disable-usb-redir \
            --disable-user \
            --disable-vde \
            --disable-vhdx \
            --disable-vhost-net \
            --disable-werror \
            &&

            case $1 in
                windows-*)
                    # Cannot build optionrom when cross-compiling to Windows,
                    # so disable that by removing the ROMS= line from
                    # the generated config-host.mak file.
                    sed -i -e '/^ROMS=/d' config-host.mak

                    # For Windows, config-host.h must be created after
                    # before the rest, or the parallel build will
                    # fail miserably.
                    run make config-host.h $BUILD_FLAGS
                    run make version.o version.lo $BUILD_FLAGS
                    ;;
            esac

            # Now build everything else in parallel.
            run make -j$NUM_JOBS $BUILD_FLAGS

            for QEMU_EXE in $QEMU_TARGET_BUILDS; do
                if [ ! -f "$QEMU_EXE" ]; then
                    panic "$(builder_text) Could not build $QEMU_EXE!!"
                fi
            done

    ) || panic "Build failed!!"

    BINARY_DIR=$INSTALL_DIR/$1
    run mkdir -p "$BINARY_DIR" ||
    panic "Could not create final directory: $BINARY_DIR"

    for QEMU_TARGET in $QEMU_TARGETS; do
        QEMU_EXE=qemu-system-${QEMU_TARGET}$(builder_host_exe_extension)
        dump "$(builder_text) Copying $QEMU_EXE to $BINARY_DIR/"

        run cp -p \
            "$BUILD_DIR"/$QEMU_TARGET-softmmu/$QEMU_EXE \
            "$BINARY_DIR"/$QEMU_EXE

        if [ -z "$OPT_DEBUG" ]; then
            run ${GNU_CONFIG_HOST_PREFIX}strip "$BINARY_DIR"/$QEMU_EXE
        fi
    done

    unset PKG_CONFIG PKG_CONFIG_PATH PKG_CONFIG_LIBDIR SDL_CONFIG
    unset LIBFFI_CFLAGS LIBFFI_LIBS GLIB_CFLAGS GLIB_LIBS
}

# Perform a Darwin build through ssh to a remote machine.
# $1: Darwin host name.
# $2: List of darwin target systems to build for.
do_remote_darwin_build () {
    local PKG_TMP=/tmp/$USER-rebuild-darwin-ssh-$$
    local PKG_SUFFIX=qemu-android-build
    local PKG_DIR=$PKG_TMP/$PKG_SUFFIX
    local PKG_TARBALL=$PKG_SUFFIX.tar.bz2
    local DARWIN_SSH="$1"
    local DARWIN_SYSTEMS="$2"

    DARWIN_SYSTEMS=$(commas_to_spaces "$DARWIN_SYSTEMS")

    dump "Creating tarball for remote darwin build."
    run mkdir -p "$PKG_DIR" && run rm -rf "$PKG_DIR"/*
    copy_directory "$QEMU_ANDROID" "$PKG_DIR/qemu-android-src"
    run mkdir -p "$PKG_DIR/aosp/prebuilts/gcc"
    copy_directory "$(program_directory)" "$PKG_DIR/scripts"
    copy_directory "$(program_directory)"/../dependencies "$PKG_DIR"/dependencies

    run mkdir -p "$PKG_DIR/prebuilts"
    for SYSTEM in $DARWIN_SYSTEMS; do
        copy_directory "$INSTALL_DIR/$SYSTEM" \
                "$PKG_DIR/prebuilts/qemu-android/$SYSTEM"
    done
    copy_directory "$PREBUILTS_DIR"/archive "$PKG_DIR/prebuilts/archive"

    local EXTRA_FLAGS

    var_append EXTRA_FLAGS "--verbosity=$(get_verbosity)"

    if [ "$OPT_NUM_JOBS" ]; then
        var_append EXTRA_FLAGS "-j$OPT_NUM_JOBS"
    fi
    if [ "$OPT_NO_CCACHE" ]; then
        var_append EXTRA_FLAGS "--no-ccache"
    fi
    if [ "$OPT_USE_SDL1" ]; then
        var_append EXTRA_FLAGS "--use-sdl1"
    fi

    # Generate a script to rebuild all binaries from sources.
    # Note that the use of the '-l' flag is important to ensure
    # that this is run under a login shell. This ensures that
    # ~/.bash_profile is sourced before running the script, which
    # puts MacPorts' /opt/local/bin in the PATH properly.
    #
    # If not, the build is likely to fail with a cryptic error message
    # like "readlink: illegal option -- f"
    cat > $PKG_DIR/build.sh <<EOF
#!/bin/bash -l
PROGDIR=\$(dirname \$0)
\$PROGDIR/scripts/$(program_name) \\
    --build-dir=/tmp/$PKG_SUFFIX/build \\
    --prebuilts-dir=/tmp/$PKG_SUFFIX/prebuilts \\
    --aosp-dir=/tmp/$PKG_SUFFIX/aosp \\
    --install-dir=/tmp/$PKG_SUFFIX/prebuilts/qemu-android \\
    --host=$(spaces_to_commas "$DARWIN_SYSTEMS") \\
    --target=$(spaces_to_commas "$TARGETS") \\
    --src-dir=/tmp/$PKG_SUFFIX/qemu-android-src \\
    $EXTRA_FLAGS
EOF
    chmod a+x $PKG_DIR/build.sh
    run tar cjf "$PKG_TMP/$PKG_TARBALL" -C "$PKG_TMP" "$PKG_SUFFIX"

    dump "Unpacking tarball in remote darwin host."
    run scp "$PKG_TMP/$PKG_TARBALL" "$DARWIN_SSH":/tmp/
    run ssh "$DARWIN_SSH" tar xf /tmp/$PKG_SUFFIX.tar.bz2 -C /tmp

    dump "Performing remote darwin build."
    log "COMMAND: ssh \"$DARWIN_SSH\" /tmp/$PKG_SUFFIX/build.sh"
    ssh "$DARWIN_SSH" /tmp/$PKG_SUFFIX/build.sh

    dump "Retrieving darwin binaries."

    for SYSTEM in $DARWIN_SYSTEMS; do
        local BINARY_DIR="$INSTALL_DIR/$SYSTEM"
        run mkdir -p "$BINARY_DIR" ||
                panic "Could not create installation directory: $BINARY_DIR"
        run scp -r "$DARWIN_SSH":/tmp/$PKG_SUFFIX/prebuilts/qemu-android/$SYSTEM/qemu-system-* $BINARY_DIR/
    done
}

# Check that we have the right prebuilts
BAD_PREBUILTS=
for SYSTEM in $HOST_SYSTEMS; do
    if ! timestamp_check "$INSTALL_DIR/$SYSTEM" qemu-android-deps; then
        var_append BAD_PREBUILTS "$SYSTEM"
    fi
done
if [ "$BAD_PREBUILTS" ]; then
    dump "Need to generate the prebuilts for: $BAD_PREBUILTS"
    EXTRA_FLAGS=
    if [ "$DARWIN_SSH" ]; then
        var_append EXTRA_FLAGS "--darwin-ssh=$DARWIN_SSH"
    fi
    $(program_directory)/build-qemu-android-deps.sh \
        --prebuilts-dir=$PREBUILTS_DIR \
        --aosp-dir=$AOSP_DIR \
        --host=$(spaces_to_commas "$BAD_PREBUILTS") \
        $EXTRA_FLAGS ||
            panic "Could not rebuild prebuilt binaries!!"
fi

if [ "$DARWIN_SSH" ]; then
    do_remote_darwin_build "$DARWIN_SSH" "$DARWIN_SYSTEMS"
fi

for SYSTEM in $LOCAL_HOST_SYSTEMS; do
    build_qemu_android $SYSTEM "$AOSP_DIR"
done
