/*
 * drivers/input/touchscreen/tap2unlock.c
 *
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com> - doubletap2wake
 * Copyright (c) 2014, goutamniwas <goutamniwas@gmail.com> - tap2unlock
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/tap2unlock.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#ifdef CONFIG_POWERSUSPEND
#include <linux/powersuspend.h>
#endif
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

/* if Sweep2Wake is compiled it will already have taken care of this */
#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

/* Version, author, desc, etc */
#define DRIVER_AUTHOR "goutamniwas <goutamniwas@gmail.com>"
#define DRIVER_DESCRIPTION "tap2unlock for almost any device"
#define DRIVER_VERSION "1.0"
#define LOGTAG "[tap2unlock]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPLv2");

/* Tuneables */
#define t2u_DEBUG		1
#define t2u_DEFAULT		1

#define t2u_PWRKEY_DUR		20
#define t2u_FEATHER		200
#define t2u_TIME		3000
#define VERTICAL_SCREEN_MIDWAY  480 // Your device's vertical resolution / 2
#define HORIZONTAL_SCREEN_MIDWAY  270 // Your device's horizontal resolution / 2

/* Resources */
int t2u_switch = t2u_DEFAULT, i;
int t2u_pattern[4] = {1,2,3,4};
bool t2u_scr_suspended = false;
static cputime64_t tap_time_pre;
static int touch_x = 0, touch_y = 0, touch_nr = 0, x_pre[4], y_pre[4];
static bool touch_x_called = false, touch_y_called = false, touch_cnt = false;
static bool exec_count = true;
//#ifndef CONFIG_HAS_EARLYSUSPEND
//#static struct notifier_block t2u_lcd_notif;
//#endif
static struct input_dev * tap2unlock_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *t2u_input_wq;
static struct work_struct t2u_input_work;
bool t2u_wake = false;
bool t2u_allow = false;

/* Read cmdline for t2u */
static int __init read_t2u_cmdline(char *t2u)
{
	if (strcmp(t2u, "1") == 0) {
		pr_info("[cmdline_t2u]: tap2unlock enabled. | t2u='%s'\n", t2u);
		t2u_switch = 1;
	} else if (strcmp(t2u, "0") == 0) {
		pr_info("[cmdline_t2u]: tap2unlock disabled. | t2u='%s'\n", t2u);
		t2u_switch = 0;
	} else {
		pr_info("[cmdline_t2u]: No valid input found. Going with default: | t2u='%u'\n", t2u_switch);
	}
	return 1;
}
__setup("t2u=", read_t2u_cmdline);

/* reset on finger release */
static void tap2unlock_reset(void) {
	exec_count = true;
	touch_nr = 0;
	tap_time_pre = 0;
	for ( i = 0; i < 4; i++) {
		x_pre[i] = 0;
		y_pre[i] = 0;
	}
	t2u_wake = false;
}

/* PowerKey work func */
static void tap2unlock_presspwr(struct work_struct * tap2unlock_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
                return;
	input_event(tap2unlock_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(tap2unlock_pwrdev, EV_SYN, 0, 0);
	msleep(t2u_PWRKEY_DUR);
	input_event(tap2unlock_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(tap2unlock_pwrdev, EV_SYN, 0, 0);
	msleep(t2u_PWRKEY_DUR);
        mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(tap2unlock_presspwr_work, tap2unlock_presspwr);

/* PowerKey trigger */
static void tap2unlock_pwrtrigger(void) {
	schedule_work(&tap2unlock_presspwr_work);
        return;
}

/* unsigned */
static unsigned int calc_feather(int coord, int prev_coord) {

	int calc_coord = 0;

	if (prev_coord < VERTICAL_SCREEN_MIDWAY)
		if (coord < HORIZONTAL_SCREEN_MIDWAY)
			calc_coord = 1;
		else
			calc_coord = 2;
	else
		if (coord < HORIZONTAL_SCREEN_MIDWAY)
			calc_coord = 3;
		else
			calc_coord = 4;
return calc_coord;
}

/* init a new touch */
static void new_touch(int x, int y) {
	
	x_pre[touch_nr] = 0;
	y_pre[touch_nr] = 0;
	x_pre[touch_nr] = x;
	y_pre[touch_nr] = y;
	pr_info("x axis : %d , y axis : %d for touch_nr = %d ",x, y,touch_nr);
	touch_nr++;
}

/* tap2unlock main function */
static void detect_tap2unlock(int x, int y, bool st)
{
        bool single_touch = st;
#if t2u_DEBUG
        pr_info(LOGTAG"x,y(%4d,%4d) single:%s\n",
                x, y, (single_touch) ? "true" : "false");
#endif
	if ((single_touch) && (t2u_switch > 0) && (exec_count)  && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {
			new_touch(x, y);
			if ((calc_feather(x_pre[0], y_pre[0]) == t2u_pattern[0])) {
				tap_time_pre = ktime_to_ms(ktime_get());
				
			} else {
				tap2unlock_reset();
			}
	
		} else if (touch_nr == 1) {
			new_touch(x, y);
			if (!(calc_feather(x_pre[1], y_pre[1]) == t2u_pattern[1])) {
				//tap_time_pre = ktime_to_ms(ktime_get());
				tap2unlock_reset();
			}
		
		} else if (touch_nr == 2) {
			new_touch(x, y);
			if (!(calc_feather(x_pre[2], y_pre[2]) == t2u_pattern[2])) {
				//tap_time_pre = ktime_to_ms(ktime_get());
				tap2unlock_reset();
			}
		} else if (touch_nr == 3) {
			 new_touch(x, y);
			pr_info("if ( %d && %d && %d ) ",calc_feather(x_pre[0], y_pre[0]), calc_feather(x_pre[1], y_pre[1]), calc_feather(x_pre[2], y_pre[2]));
			 if ((calc_feather(x_pre[3], y_pre[3]) == t2u_pattern[3]) && 
			    		((ktime_to_ms(ktime_get())-tap_time_pre) < t2u_TIME)) {
				t2u_wake = true;
				t2u_allow = true;
			}
			else {
				tap2unlock_reset();
			}
		}
		
		if (t2u_wake) {
			pr_info(LOGTAG"ON\n");
			exec_count = false;
			tap2unlock_pwrtrigger();
			tap2unlock_reset();
			
		}
	}
}

static void t2u_input_callback(struct work_struct *unused) {

	detect_tap2unlock(touch_x, touch_y, true);

	return;
}

static void t2u_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
#if t2u_DEBUG
	pr_info("tap2unlock: code: %s|%u, val: %i\n",
		((code==ABS_MT_POSITION_X) ? "X" :
		(code==ABS_MT_POSITION_Y) ? "Y" :
		(code==ABS_MT_TRACKING_ID) ? "ID" :
		"undef"), code, value);
#endif
	if (!t2u_scr_suspended)
		return;

	if (code == ABS_MT_SLOT) {
		tap2unlock_reset();
		return;
	}

	if (code == ABS_MT_TRACKING_ID && value == -1) {
		touch_cnt = true;
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called || touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, t2u_input_wq, &t2u_input_work);
	}
}

static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "touch") ||
	    strstr(dev->name, "synaptics_dsx_i2c")) {
		return 0;
	} else {
		return 1;
	}
}

static int t2u_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "t2u";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void t2u_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id t2u_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler t2u_input_handler = {
	.event		= t2u_input_event,
	.connect	= t2u_input_connect,
	.disconnect	= t2u_input_disconnect,
	.name		= "t2u_inputreq",
	.id_table	= t2u_ids,
};

#ifdef CONFIG_POWERSUSPEND
static void t2u_power_suspend(struct power_suspend *h) {
	t2u_scr_suspended = true;
}

static void t2u_power_resume(struct power_suspend *h) {
	t2u_scr_suspended = false;
}

static struct power_suspend t2u_power_suspend_handler = {
	.suspend = t2u_power_suspend,
	.resume = t2u_power_resume,
};
#endif

/*
 * SYSFS stuff below here
 */
static ssize_t t2u_tap2unlock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", t2u_switch);

	return count;
}

static ssize_t t2u_tap2unlock_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0' && buf[0] <= '2' && buf[1] == '\n')
                if (t2u_switch != buf[0] - '0')
		        t2u_switch = buf[0] - '0';

	return count;
}

static DEVICE_ATTR(tap2unlock, (S_IWUSR|S_IRUGO),
	t2u_tap2unlock_show, t2u_tap2unlock_dump);

static ssize_t t2u_pattern_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d%d%d%d\n", t2u_pattern[0], t2u_pattern[1], t2u_pattern[2], t2u_pattern[3]);

	return count;
}

static ssize_t t2u_pattern_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	if (buf[0] >= '0') {
                
		  t2u_pattern[0] = buf[0] - '0';
		  t2u_pattern[1] = buf[1] - '0';
		  t2u_pattern[2] = buf[2] - '0';
		  t2u_pattern[3] = buf[3] - '0';
	}

	return count;
}

static DEVICE_ATTR(tap2unlock_pattern, (S_IWUSR|S_IRUGO),
	t2u_pattern_show, t2u_pattern_dump);

static ssize_t t2u_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t t2u_version_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(tap2unlock_version, (S_IWUSR|S_IRUGO),
	t2u_version_show, t2u_version_dump);

/*
 * INIT / EXIT stuff below here
 */
#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif
static int __init tap2unlock_init(void)
{
	int rc = 0;

	tap2unlock_pwrdev = input_allocate_device();
	if (!tap2unlock_pwrdev) {
		pr_err("Can't allocate suspend autotest power button\n");
		goto err_alloc_dev;
	}

	input_set_capability(tap2unlock_pwrdev, EV_KEY, KEY_POWER);
	tap2unlock_pwrdev->name = "t2u_pwrkey";
	tap2unlock_pwrdev->phys = "t2u_pwrkey/input0";

	rc = input_register_device(tap2unlock_pwrdev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

	t2u_input_wq = create_workqueue("t2uiwq");
	if (!t2u_input_wq) {
		pr_err("%s: Failed to create t2uiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&t2u_input_work, t2u_input_callback);
	rc = input_register_handler(&t2u_input_handler);
	if (rc)
		pr_err("%s: Failed to register t2u_input_handler\n", __func__);

#ifdef CONFIG_POWERSUSPEND
	register_power_suspend(&t2u_power_suspend_handler);
#endif

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("tap2unlock", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_tap2unlock.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for tap2unlock\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_tap2unlock_pattern.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for pattern\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_tap2unlock_version.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for tap2unlock_version\n", __func__);
	}

err_input_dev:
	input_free_device(tap2unlock_pwrdev);
err_alloc_dev:
	pr_info(LOGTAG"%s done\n", __func__);

	return 0;
}

static void __exit tap2unlock_exit(void)
{
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
	input_unregister_handler(&t2u_input_handler);
	destroy_workqueue(t2u_input_wq);
	input_unregister_device(tap2unlock_pwrdev);
	input_free_device(tap2unlock_pwrdev);
	return;
}

module_init(tap2unlock_init);
module_exit(tap2unlock_exit);

