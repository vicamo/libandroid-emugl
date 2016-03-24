// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "net-android.h"
#include "android/android.h"
#include "android/network/constants.h"
#include "android/telephony/modem_driver.h"
#include "android/shaper.h"

extern "C" {
#include "net/net.h"
}

double   qemu_net_upload_speed   = 0.;
double   qemu_net_download_speed = 0.;
int      qemu_net_min_latency = 0;
int      qemu_net_max_latency = 0;

NetShaper  slirp_shaper_in;
NetShaper  slirp_shaper_out;
NetDelay   slirp_delay_in;

#if defined(CONFIG_SLIRP)
static void* s_slirp_state = nullptr;
static void* s_net_client_state = nullptr;
static Slirp* s_slirp = nullptr;

static void
slirp_delay_in_cb(void* data, size_t size, void* opaque)
{
    slirp_input(s_slirp, static_cast<const uint8_t*>(data), size);
}

static void
slirp_shaper_in_cb(void* data, size_t size, void* opaque)
{
    netdelay_send_aux(slirp_delay_in, data, size, opaque);
}

static void
slirp_shaper_out_cb(void* data, size_t size, void* opaque)
{
    qemu_send_packet(static_cast<NetClientState*>(s_net_client_state),
                     static_cast<const uint8_t*>(data),
                     size);
}

void
slirp_init_shapers(void* slirp_state, void* net_client_state, Slirp* slirp)
{
    s_slirp_state = slirp_state;
    s_net_client_state = net_client_state;
    s_slirp = slirp;
    slirp_delay_in = netdelay_create(slirp_delay_in_cb);
    slirp_shaper_in = netshaper_create(1, slirp_shaper_in_cb);
    slirp_shaper_out = netshaper_create(1, slirp_shaper_out_cb);

    netdelay_set_latency(slirp_delay_in, qemu_net_min_latency,
                         qemu_net_max_latency);
    netshaper_set_rate(slirp_shaper_out, qemu_net_download_speed);
    netshaper_set_rate(slirp_shaper_in, qemu_net_upload_speed);
}
#endif  // CONFIG_SLIRP

int
android_parse_network_speed(const char*  speed)
{
    double upload = 0., download = 0.;
    if (!android_network_speed_parse(speed, &upload, &download)) {
        return -1;
    }

    qemu_net_upload_speed = upload;
    qemu_net_download_speed = download;

    return 0;
}


int
android_parse_network_latency(const char*  delay)
{
    double min_delay_ms = 0., max_delay_ms = 0.;
    if (!android_network_latency_parse(delay, &min_delay_ms, &max_delay_ms)) {
        return -1;
    }
    qemu_net_min_latency = (int)min_delay_ms;
    qemu_net_max_latency = (int)max_delay_ms;

    return 0;
}
