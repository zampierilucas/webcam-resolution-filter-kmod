#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/videodev2.h>
#include <linux/kprobes.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <media/v4l2-dev.h>
#include <linux/errno.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/limits.h>

#define DRIVER_NAME "webcam_res_filter"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Claude Code");
MODULE_DESCRIPTION("Filter webcam resolutions to hide unwanted resolution formats");
MODULE_VERSION("1.0");

static int max_width = -1;   /* -1 means no maximum limit */
static int max_height = -1;  /* -1 means no maximum limit */
static int min_width = -1;   /* -1 means no minimum limit */
static int min_height = -1;  /* -1 means no minimum limit */
static char *device_path = NULL;

module_param(max_width, int, 0644);
MODULE_PARM_DESC(max_width, "Maximum allowed width (-1 for no limit, default: no limit)");

module_param(max_height, int, 0644);
MODULE_PARM_DESC(max_height, "Maximum allowed height (-1 for no limit, default: no limit)");

module_param(min_width, int, 0644);
MODULE_PARM_DESC(min_width, "Minimum allowed width (-1 for no limit, default: no limit)");

module_param(min_height, int, 0644);
MODULE_PARM_DESC(min_height, "Minimum allowed height (-1 for no limit, default: no limit)");

module_param(device_path, charp, 0644);
MODULE_PARM_DESC(device_path, "Target device path (e.g., /dev/video1, default: all devices)");

static struct kretprobe krp_video_ioctl2;

struct ioctl_data {
    unsigned int cmd;
    void __user *arg;
    struct file *file;
};

static int should_filter_device(struct file *file)
{
    struct path *path;
    char *pathname, *buf;
    int should_filter = 1; /* Default: filter all devices if no specific device specified */

    if (!device_path)
        return 1; /* Filter all devices when no specific device is specified */

    if (!file || !file->f_path.dentry)
        return 0;

    buf = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!buf)
        return 0;

    path = &file->f_path;
    pathname = d_path(path, buf, PATH_MAX);
    if (!IS_ERR(pathname)) {
        if (strcmp(pathname, device_path) == 0) {
            should_filter = 1; /* This is our target device */
        } else {
            should_filter = 0; /* Different device, don't filter */
        }
    }

    kfree(buf);
    return should_filter;
}


static int is_resolution_allowed(int width, int height)
{
    /* If no limits are set, allow all resolutions */
    if (min_width == -1 && min_height == -1 && max_width == -1 && max_height == -1) {
        return 1;
    }
    
    /* Check if resolution is within ALL specified bounds */
    if (min_width != -1 && width < min_width) return 0;
    if (min_height != -1 && height < min_height) return 0;
    if (max_width != -1 && width > max_width) return 0;
    if (max_height != -1 && height > max_height) return 0;
    
    return 1;
}

static int video_ioctl2_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct ioctl_data *data = (struct ioctl_data *)ri->data;

#ifdef CONFIG_X86_64
    data->file = (struct file *)regs->di;
    data->cmd = regs->si;
    data->arg = (void __user *)regs->dx;
#elif defined(CONFIG_ARM64)
    data->file = (struct file *)regs->regs[0];
    data->cmd = regs->regs[1];
    data->arg = (void __user *)regs->regs[2];
#else
    #error "Unsupported architecture"
#endif

    return 0;
}

static int should_filter_framesize(struct v4l2_frmsizeenum *frmsize)
{
    switch (frmsize->type) {
    case V4L2_FRMSIZE_TYPE_DISCRETE:
        return !is_resolution_allowed(frmsize->discrete.width, frmsize->discrete.height);
    case V4L2_FRMSIZE_TYPE_STEPWISE:
    case V4L2_FRMSIZE_TYPE_CONTINUOUS:
        return !is_resolution_allowed(frmsize->stepwise.min_width, frmsize->stepwise.min_height);
    }
    return 0;
}

static int get_allowed_resolution_by_index(unsigned int filtered_index, struct v4l2_frmsizeenum *result)
{
    /* Map filtered index to known allowed resolutions */
    static const struct {
        int width;
        int height;
    } allowed_resolutions[] = {
        {1280, 720},   /* 720p */
        {1920, 1080},  /* 1080p */
        {640, 480},    /* VGA - fallback */
    };
    
    int num_allowed = sizeof(allowed_resolutions) / sizeof(allowed_resolutions[0]);
    int allowed_count = 0;
    
    /* Find which allowed resolutions match current filter criteria */
    for (int i = 0; i < num_allowed; i++) {
        if (is_resolution_allowed(allowed_resolutions[i].width, allowed_resolutions[i].height)) {
            if (allowed_count == filtered_index) {
                /* Found the resolution for this filtered index */
                result->type = V4L2_FRMSIZE_TYPE_DISCRETE;
                result->discrete.width = allowed_resolutions[i].width;
                result->discrete.height = allowed_resolutions[i].height;
                result->index = filtered_index;
                return 1; /* Success */
            }
            allowed_count++;
        }
    }
    
    return 0; /* Index out of range */
}

static int video_ioctl2_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct ioctl_data *data = (struct ioctl_data *)ri->data;
    long retval = regs_return_value(regs);

    if (data->cmd == VIDIOC_ENUM_FRAMESIZES && retval == 0) {
        /* Check if we should filter this device */
        if (!should_filter_device(data->file)) {
            return 0; /* Don't filter this device */
        }

        /* Check if any filtering is active */
        if (min_width == -1 && min_height == -1 && max_width == -1 && max_height == -1) {
            return 0; /* No filtering active */
        }

        struct v4l2_frmsizeenum frmsize;

        if (copy_from_user(&frmsize, data->arg, sizeof(frmsize)) == 0) {
            /* Use index remapping to provide only allowed resolutions */
            struct v4l2_frmsizeenum filtered_frmsize;
            
            if (get_allowed_resolution_by_index(frmsize.index, &filtered_frmsize)) {
                /* Copy the pixel format from the original request */
                filtered_frmsize.pixel_format = frmsize.pixel_format;
                
                /* Copy the filtered resolution back to user space */
                if (copy_to_user(data->arg, &filtered_frmsize, sizeof(filtered_frmsize)) == 0) {
                    /* Successfully provided filtered resolution */
                    return 0;
                }
            } else {
                /* No more allowed resolutions - signal end of enumeration */
                regs_set_return_value(regs, -EINVAL);
            }
        }
    }

    return 0;
}

static int __init webcam_res_filter_init(void)
{
    int ret;

    printk(KERN_INFO "%s: Loading webcam resolution filter module\n", DRIVER_NAME);
    
    /* Show active limits */
    if (min_width != -1 || min_height != -1 || max_width != -1 || max_height != -1) {
        printk(KERN_INFO "%s: Active limits:", DRIVER_NAME);
        if (min_width != -1) printk(KERN_CONT " min_width=%d", min_width);
        if (min_height != -1) printk(KERN_CONT " min_height=%d", min_height);
        if (max_width != -1) printk(KERN_CONT " max_width=%d", max_width);
        if (max_height != -1) printk(KERN_CONT " max_height=%d", max_height);
        printk(KERN_CONT "\n");
    } else {
        printk(KERN_INFO "%s: No resolution limits set - all resolutions allowed\n", DRIVER_NAME);
    }
    
    if (device_path) {
        printk(KERN_INFO "%s: Target device: %s\n", DRIVER_NAME, device_path);
    } else {
        printk(KERN_INFO "%s: Target device: all devices\n", DRIVER_NAME);
    }

    krp_video_ioctl2.kp.symbol_name = "video_ioctl2";
    krp_video_ioctl2.handler = video_ioctl2_ret;
    krp_video_ioctl2.entry_handler = video_ioctl2_entry;
    krp_video_ioctl2.data_size = sizeof(struct ioctl_data);
    krp_video_ioctl2.maxactive = 20;

    ret = register_kretprobe(&krp_video_ioctl2);
    if (ret < 0) {
        printk(KERN_ERR "%s: Failed to register kretprobe for video_ioctl2: %d\n",
               DRIVER_NAME, ret);
        return ret;
    }

    printk(KERN_INFO "%s: Successfully loaded\n", DRIVER_NAME);
    return 0;
}

static void __exit webcam_res_filter_exit(void)
{
    unregister_kretprobe(&krp_video_ioctl2);
    printk(KERN_INFO "%s: Unloaded webcam resolution filter module\n", DRIVER_NAME);
}

module_init(webcam_res_filter_init);
module_exit(webcam_res_filter_exit);