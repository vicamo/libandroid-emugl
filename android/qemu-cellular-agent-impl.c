/* Copyright (C) 2015 The Android Open Source Project
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

#include "android/cellular-agent-impl.h"

#include "android/android.h"
#include "android/cellular-agent.h"
#include "android/shaper.h"
#include "telephony/modem_driver.h"

void cellular_setSignalStrength(int zeroTo31)
{
    // (See do_gsm_signal() in android/console.c)

    if (android_modem) {
        if (zeroTo31 <  0) zeroTo31 =  0;
        if (zeroTo31 > 31) zeroTo31 = 31;

        amodem_set_signal_strength(android_modem, zeroTo31, 99);
    }
}

void cellular_setVoiceStatus(enum CellularStatus voiceStatus)
{
    // (See do_gsm_voice() in android/console.c)
    ARegistrationState  state = A_REGISTRATION_UNKNOWN;

    if (android_modem) {
        switch (voiceStatus) {
            case Cellular_Stat_Home:          state = A_REGISTRATION_HOME;          break;
            case Cellular_Stat_Roaming:       state = A_REGISTRATION_ROAMING;       break;
            case Cellular_Stat_Searching:     state = A_REGISTRATION_SEARCHING;     break;
            case Cellular_Stat_Denied:        state = A_REGISTRATION_DENIED;        break;
            case Cellular_Stat_Unregistered:  state = A_REGISTRATION_UNREGISTERED;  break;
        }

        amodem_set_voice_registration(android_modem, state);
    }
}

void cellular_setDataStatus(enum CellularStatus dataStatus)
{
    // (See do_gsm_data() in android/console.c)
    ARegistrationState  state = A_REGISTRATION_UNKNOWN;

    switch (dataStatus) {
        case Cellular_Stat_Home:          state = A_REGISTRATION_HOME;          break;
        case Cellular_Stat_Roaming:       state = A_REGISTRATION_ROAMING;       break;
        case Cellular_Stat_Searching:     state = A_REGISTRATION_SEARCHING;     break;
        case Cellular_Stat_Denied:        state = A_REGISTRATION_DENIED;        break;
        case Cellular_Stat_Unregistered:  state = A_REGISTRATION_UNREGISTERED;  break;
    }

    if (android_modem) {
        amodem_set_data_registration(android_modem, state);
    }

    qemu_net_disable = (state != A_REGISTRATION_HOME    &&
                        state != A_REGISTRATION_ROAMING );
}

void cellular_setStandard(enum CellularStandard cStandard)
{
    // (See do_network_speed() in android/console.c)
    char *speedName;

    switch (cStandard) {
        case Cellular_Std_GSM:    speedName = "gsm";    break;
        case Cellular_Std_HSCSD:  speedName = "hscsc";  break;
        case Cellular_Std_GPRS:   speedName = "gprs";   break;
        case Cellular_Std_EDGE:   speedName = "edge";   break;
        case Cellular_Std_UMTS:   speedName = "umts";   break;
        case Cellular_Std_HSDPA:  speedName = "hsdpa";  break;
        case Cellular_Std_full:   speedName = "full";   break;

        default:
            return; // Error
    }

    // Find this entry in the speed table and set
    // qemu_net_download_speed and qemu_net_upload_speed
    android_parse_network_speed(speedName);

    // Tell the network shaper the new rates
    netshaper_set_rate(slirp_shaper_in,  qemu_net_download_speed);
    netshaper_set_rate(slirp_shaper_out, qemu_net_upload_speed);

    if (android_modem) {
        amodem_set_data_network_type(android_modem,
                                     android_parse_network_type(speedName));
    }
}
