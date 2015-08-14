// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/Limits.h"

#include "android/looper-qemu.h"

#include "android/qemu/base/async/Looper.h"
#include "android/utils/looper-base.h"

using ::android::internal::GLooper;

::Looper* looper_newCore(void) {
    GLooper* glooper = new GLooper(
            ::android::qemu::createLooper());

    return &glooper->looper;
}
