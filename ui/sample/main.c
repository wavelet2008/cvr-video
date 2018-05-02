/**
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Chad.ma <chad.ma@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <init_hook/init_hook.h>
#include "common.h"
#include "watermark.h"
#include "ueventmonitor/ueventmonitor.h"
#include "ueventmonitor/usb_sd_ctrl.h"
#include "public_interface.h"
#include "msg_list_manager.h"
#include "parameter.h"
#include "display_sample.h"
#include "video.h"
#include "uvc/uvc_user.h"
#include "input/input.h"
#include "audio/playback/audioplay.h"

#define COLOR_KEY_R 0x0
#define COLOR_KEY_G 0x0
#define COLOR_KEY_B 0x1

#define CAPTURE_SOUND_FILE "/usr/local/share/sounds/camera_click.wav"
#define KEYPRESS_SOUND_FILE "/usr/local/share/sounds/KeypressStandard.wav"

struct bitmap watermark_bmap[7];
uint32_t licplate_pos[8] = {0};
int gMode = MODE_RECORDING;
int gPhotoFlag = 0;
int gSuspendFlag = 0;
uint32_t lic_chinese_idx = 0;

void takephoto(void)
{
    if (api_get_sd_mount_state() == SD_STATE_IN && !gPhotoFlag) {
        gPhotoFlag = true;
        audio_sync_play(CAPTURE_SOUND_FILE);
        if (video_record_takephoto(1) == -1) {
            printf("No input video devices!\n");
            gPhotoFlag = false;
        }
    }
}

#ifdef USE_INPUT
void key_event_callback(int cmd, void *msg0, void *msg1)
{
    if (gSuspendFlag) {
        if (cmd == MSG_KEY_UP || cmd == MSG_KEY_LONGUP)
            gSuspendFlag = 0;
        return;
    }

    switch (cmd) {
    case MSG_KEY_DOWN:
        if (gMode != MODE_PLAY && gMode != MODE_PREVIEW)
            audio_sync_play(KEYPRESS_SOUND_FILE);
#ifdef _PCBA_SERVICE_
        api_key_event_notice(cmd, (int)msg0);
        break;
#else
        if ((int)msg0 == SCANCODE_ENTER)
            takephoto();
        break;
    case MSG_KEY_LONGPRESS:
        if ((int)msg0 == SCANCODE_SHUTDOWN)
            api_power_shutdown();
        break;
    case MSG_KEY_UP:
        if ((int)msg0 == SCANCODE_SHUTDOWN) {
            gSuspendFlag = 1;
            api_change_mode(MODE_SUSPEND);
        }
        break;
    case MSG_KEY_LONGUP:
        break;
#endif
    }
}
#endif

void get_licenseplate_str(char (*lic_plate)[3], int len, char* licplate_str)
{
    int i;

    if (licplate_str == NULL)
        return;

    if (parameter_get_licence_plate_flag() == 1) {
        for (i = 0; i < len; i++)
            strcat(licplate_str, (const char*)(lic_plate + i));
    }
}

void update_firmware(void)
{
    int ret;

    ret = api_check_firmware();

    if (ret == FW_OK) {
        /* The BOOT_FWUPDATE which is defined in init_hook.h */
        printf("will reboot and update...\n");
        parameter_flash_set_flashed(BOOT_FWUPDATE);
        api_power_reboot();
    }
}

#if (HAVE_DISPLAY == 1)
void ui_fill_colorkey(void)
{
    struct win * ui_win;
    struct color_key color_key;
    unsigned short rgb565_data;
    unsigned short *ui_buff;
    int i;
    int w, h;

    ui_win = rk_fb_getuiwin();
    ui_buff = (unsigned short *)ui_win->buffer;

    /* enable and set color key */
    color_key.enable = 1;
    color_key.red = (COLOR_KEY_R & 0x1f) << 3;
    color_key.green = (COLOR_KEY_G & 0x3f) << 2;
    color_key.blue = (COLOR_KEY_B & 0x1f) << 3;
    rk_fb_set_color_key(color_key);

    rk_fb_get_out_device(&w, &h);

    /* set ui win color key */
    rgb565_data = (COLOR_KEY_R & 0x1f) << 11 | ((COLOR_KEY_G & 0x3f) << 5) | (COLOR_KEY_B & 0x1f);
    for (i = 0; i < w * h; i ++) {
        ui_buff[i] = rgb565_data;
    }
}

void hdmi_plug(int status)
{
    if (status == 1) {
        rk_fb_set_lcd_backlight(0);
        rk_fb_set_out_device(OUT_DEVICE_HDMI);
    } else {
        rk_fb_set_out_device(OUT_DEVICE_LCD);
    }
    usleep(500000);

    if (status == 0)
        rk_fb_set_lcd_backlight(parameter_get_video_backlt());
    ui_fill_colorkey();
}
#endif

void sdcart_plug(void)
{
    if (api_get_sd_mount_state() == SD_STATE_IN) {
        update_firmware();
        if ((gMode == MODE_RECORDING) && parameter_get_video_autorec())
            api_start_rec();
    }
}

void camera_plug(int id, int status)
{
    if (gMode != MODE_RECORDING && gMode != MODE_PHOTO)
        return;

    if (status == 1) {
        struct video_param *front = parameter_get_video_frontcamera_reso();
        struct video_param *back = parameter_get_video_backcamera_reso();
        struct video_param *cif = parameter_get_video_cifcamera_reso();
        usleep(200000);
        if (api_video_get_record_state() == VIDEO_STATE_RECORD)
            video_record_addvideo(id, front, back, cif, 1, 0);
        else
            video_record_addvideo(id, front, back, cif, 0, 1);
    } else {
        if (id == uvc_get_user_video_id()) {
            uvc_gadget_pthread_exit();
            android_usb_config_uvc();
        } else {
            video_record_deletevideo(id);
        }
    }
}

void usb_plug(int status)
{
    int usb_sd_chage = status;

    printf("MSG_USBCHAGE : usb_sd_chage = ( %d )\n", status);

    if (usb_sd_chage == 1) {
        printf("usb_sd_chage == 1:USB_MODE==0\n");
        if (api_get_sd_mount_state() == SD_STATE_IN) {
            if (api_video_get_record_state() == VIDEO_STATE_RECORD) {
                api_stop_rec();
            }
            cvr_usb_sd_ctl(USB_CTRL, 1);
        }

    } else if (usb_sd_chage == 0) {
        cvr_usb_sd_ctl(USB_CTRL, 0);
        printf("usb_sd_chage == 0:USB_MODE==0\n");
    }
}

void changemode_notify(int premode, int mode)
{
    switch (premode) {
    case MODE_PHOTO:
    case MODE_RECORDING:
        break;
    case MODE_PLAY:
        if (mode == MODE_SUSPEND) {
            gMode = MODE_PREVIEW;
        }
        break;
    case MODE_USBDIALOG:
        break;
    case MODE_USBCONNECTION:
        break;
    case MODE_PREVIEW:
        break;
    }
    if (mode != MODE_SUSPEND)
        gMode = mode;
}


void msg_manager_cb(void *msgData, void *msg0, void *msg1)
{
    struct public_message msg;

    memset(&msg, 0, sizeof(msg));

    if (NULL != msgData) {
        msg.id = ((struct public_message *)msgData)->id;
        msg.type = ((struct public_message *)msgData)->type;
    }

    if (msg.type != TYPE_LOCAL && msg.type != TYPE_BROADCAST)
        return;

    switch (msg.id) {
    case MSG_SET_LANGUAGE:
        break;
    case MSG_SDCARDFORMAT_START:
        break;
    case MSG_SDCARDFORMAT_NOTIFY:
        break;
    case MSG_VIDEO_REC_START:
        break;
    case MSG_VIDEO_REC_STOP:
        break;
    case MSG_VIDEO_MIC_ONOFF:
        break;
    case MSG_VIDEO_SET_ABMODE:
        break;
    case MSG_VIDEO_SET_MOTION_DETE:
        break;
    case MSG_VIDEO_MARK_ONOFF:
        break;
    case MSG_VIDEO_ADAS_ONOFF:
        break;
    case MSG_VIDEO_UPDATETIME:
        break;
    case MSG_ADAS_UPDATE:
        break;
    case MSG_VIDEO_PHOTO_END:
        if ((int)msg1 == 1 && gPhotoFlag) {
            gPhotoFlag = false;
        }
        break;
    case MSG_SDCORD_MOUNT_FAIL:
        api_sdcard_format(0);
        break;
    case MSG_SDCORD_CHANGE:
        sdcart_plug();
        break;
    case MSG_FS_INITFAIL:
        api_sdcard_format(0);
        break;
    case MSG_HDMI_EVENT:
#if (HAVE_DISPLAY == 1)
        hdmi_plug((int)msg1);
#endif
        break;
    case MSG_CVBSOUT_EVENT:
        break;
    case MSG_POWEROFF:
        break;
    case MSG_BATT_UPDATE_CAP:
        break;
    case MSG_BATT_LOW:
        break;
    case MSG_BATT_DISCHARGE:
        break;
    case MSG_CAMERE_EVENT:
        camera_plug((int)msg0, (int)msg1);
        break;
    case MSG_USB_EVENT:
        usb_plug((int)msg1);
        break;
    case MSG_USER_RECORD_RATE_CHANGE:
        break;
    case MSG_USER_MDPROCESSOR:
        break;
    case MSG_USER_MUXER:
        break;
    case MSG_VIDEOPLAY_EXIT:
        break;
    case MSG_VIDEOPLAY_UPDATETIME:
        break;
    case MSG_MODE_CHANGE_NOTIFY:
        changemode_notify((int)msg0, (int)msg1);
        break;
    case MSG_VIDEO_FRONT_REC_RESOLUTION:
        break;
    case MSG_GPS_INFO:
        break;
    default:
        break;
    }
}

int load_resources(void)
{
    int i;
    char img[128];
    char respath[] = "/usr/local/share/minigui/res/images/";
    char watermark_img[7][30] = {"watermark.bmp", "watermark_240p.bmp", "watermark_360p.bmp", "watermark_480p.bmp",
                                 "watermark_720p.bmp", "watermark_1080p.bmp", "watermark_1440p.bmp"
                                };

    /* load watermark bmp */
    for (i = 0; i < ARRAY_SIZE(watermark_bmap); i++) {
        snprintf(img, sizeof(img), "%s%s", respath, watermark_img[i]);
        if (load_bitmap(&watermark_bmap[i], img)) {
            printf("load watermark bmp error, i = %d\n", i);
            return -1;
        }
    }

    return 0;
}

void watermark_update(void)
{
    static time_t pre_sec;
    struct timeval tv_time;

    gettimeofday(&tv_time, NULL);
    if (pre_sec != tv_time.tv_sec) {
        pre_sec = tv_time.tv_sec;
        if (((gMode == MODE_RECORDING) || (gMode == MODE_PHOTO)) &&
            parameter_get_video_mark()) {
            struct tm* today;
            time_t ltime;
            uint32_t show_time[MAX_TIME_LEN] = { 0 };

            time(&ltime);
            today = localtime(&ltime);
            watermark_get_show_time(show_time, today, MAX_TIME_LEN);
            video_record_update_time(show_time, MAX_TIME_LEN);
        }
    }
}

int main(int argc, char *argv[])
{
    printf("no gui main enter\n");
#if (HAVE_DISPLAY == 1)
    rk_fb_init(FB_FORMAT_RGB_565);
    ui_fill_colorkey();
    set_display_sample_window();
#endif
#ifdef USE_INPUT
    input_init(key_event_callback);
#endif
    load_resources();
    api_poweron_init(msg_manager_cb);
    api_video_init(VIDEO_MODE_REC);
    while (1) {
        usleep(200000);
        watermark_update();
    }
#ifdef USE_INPUT
    input_deinit();
#endif
    api_poweroff_deinit();
    printf("no gui main exit\n");

    return 0;
}
