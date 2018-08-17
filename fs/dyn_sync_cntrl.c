/*
 * Dynamic sync control driver V2
 * 
 * by andip71 (alias Lord Boeffla)
 * modified by xNombre (Andrzej Perczak)
 * 
 * All credits for original implemenation to faux123
 * 
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/writeback.h>
#include <linux/dyn_sync_cntrl.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/string.h>

// Declarations
bool suspend_active __read_mostly = false;
bool dyn_fsync_active = DYN_FSYNC_ACTIVE_DEFAULT;

static struct notifier_block fb_notif;
static struct delayed_work sync_work;
// Functions

static ssize_t dyn_fsync_active_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", dyn_fsync_active);
}


static ssize_t dyn_fsync_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	if(strtobool(buf, &dyn_fsync_active))
		return -EINVAL;

	if (dyn_fsync_active)
	{
		pr_info("%s: dynamic fsync enabled\n", __FUNCTION__);
		if (fb_register_client(&fb_notif) != 0)
		{
			pr_err("%s: failed to register fb callback\n", __func__);
		}
	}
	else
	{
		pr_info("%s: dynamic fsync disabled\n", __FUNCTION__);
		fb_unregister_client(&fb_notif);
	}

	return count;
}


static ssize_t dyn_fsync_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
		DYN_FSYNC_VERSION_MAJOR,
		DYN_FSYNC_VERSION_MINOR);
}


static ssize_t dyn_fsync_suspend_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "suspend active: %u\n", suspend_active);
}

static void emergency_event(void)
{
	if(dyn_fsync_active) {
		cancel_delayed_work(&sync_work);
		suspend_active = false;
		emergency_sync();
		pr_warn("dynamic fsync: force flush!\n");
	}
}

static int dyn_fsync_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	emergency_event();
	return NOTIFY_DONE;
}


static int dyn_fsync_notify_sys(struct notifier_block *this, unsigned long code,
				void *unused)
{
	if(code == SYS_DOWN || code == SYS_HALT)
		emergency_event();

	return NOTIFY_DONE;
}

static int fb_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
 	struct fb_event *evdata = data;
  	int *blank;
  
  	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
  		blank = evdata->data;
  		switch (*blank) {
  			case FB_BLANK_UNBLANK:
  				//display on
				suspend_active = true;
  				break;
  			case FB_BLANK_POWERDOWN:
  			case FB_BLANK_HSYNC_SUSPEND:
  			case FB_BLANK_VSYNC_SUSPEND:
  			case FB_BLANK_NORMAL:
  				//display off
				suspend_active = false;
				schedule_delayed_work(&sync_work, 
					msecs_to_jiffies(SYNC_WORK_DELAY_MSEC));
  				break;
  		}
         }

 	return NOTIFY_OK;
}

// Module structures

static struct notifier_block dyn_fsync_notifier = 
{
	.notifier_call = dyn_fsync_notify_sys,
};

static struct kobj_attribute dyn_fsync_active_attribute = 
	__ATTR(Dyn_fsync_active, 0664,
		dyn_fsync_active_show,
		dyn_fsync_active_store);

static struct kobj_attribute dyn_fsync_version_attribute = 
	__ATTR(Dyn_fsync_version, 0444, dyn_fsync_version_show, NULL);

static struct kobj_attribute dyn_fsync_suspend_attribute = 
	__ATTR(Dyn_fsync_suspend, 0444, dyn_fsync_suspend_show, NULL);

static struct attribute *dyn_fsync_active_attrs[] =
{
	&dyn_fsync_active_attribute.attr,
	&dyn_fsync_version_attribute.attr,
	&dyn_fsync_suspend_attribute.attr,
	NULL,
};

static struct attribute_group dyn_fsync_active_attr_group =
{
	.attrs = dyn_fsync_active_attrs,
};

static struct notifier_block dyn_fsync_panic_block = 
{
	.notifier_call  = dyn_fsync_panic_event,
	.priority       = INT_MAX,
};

static struct kobject *dyn_fsync_kobj;


// Module init/exit

static int dyn_fsync_init(void)
{
	int sysfs_result;

	dyn_fsync_kobj = kobject_create_and_add("dyn_fsync", kernel_kobj);
	if (!dyn_fsync_kobj) 
	{
		pr_err("%s dyn_fsync_kobj create failed!\n", __FUNCTION__);
		return -ENOMEM;
	}

	sysfs_result = sysfs_create_group(dyn_fsync_kobj,
			&dyn_fsync_active_attr_group);
	if (sysfs_result) 
	{
		pr_err("%s dyn_fsync sysfs create failed!\n", __FUNCTION__);
		goto err;
	}

	fb_notif.notifier_call = fb_notifier_callback;
	if (dyn_fsync_active)
	{
		pr_info("%s: dynamic fsync enabled\n", __FUNCTION__);
		if (fb_register_client(&fb_notif) != 0)
		{
			pr_err("%s: failed to register fb callback\n", __func__);
		}
	}

	INIT_DELAYED_WORK(&sync_work, sync_filesystems);

	register_reboot_notifier(&dyn_fsync_notifier);
	atomic_notifier_chain_register(&panic_notifier_list,
		&dyn_fsync_panic_block);

	pr_info("%s dynamic fsync initialisation completed\n", __FUNCTION__);

	return sysfs_result;

err:
	kobject_put(dyn_fsync_kobj);
	return -EFAULT;
}


static void dyn_fsync_exit(void)
{
	unregister_reboot_notifier(&dyn_fsync_notifier);
	atomic_notifier_chain_unregister(&panic_notifier_list, &dyn_fsync_panic_block);
	kobject_put(dyn_fsync_kobj);
        fb_unregister_client(&fb_notif);

	pr_info("%s dynamic fsync unregistration complete\n", __FUNCTION__);
}

module_init(dyn_fsync_init);
module_exit(dyn_fsync_exit);

MODULE_DESCRIPTION("dynamic fsync - automatic fs sync optimizer");
MODULE_LICENSE("GPL v2");
