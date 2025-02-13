/* Copyright (C) MicroArray
 * MicroArray Fprint Driver Code for REE enviroment
 * madev.c
 * Date: 2017-3-9
 * Version: v4.0.05
 * Author: guq
 * Contact: guq@microarray.com.cn
 */
#include "madev.h"

//spdev use for recording the data for other use
static unsigned int irq, ret;
static unsigned int ma_drv_reg;
static unsigned int ma_speed;
static unsigned int is_screen_on;
static struct notifier_block notifier;
static unsigned int int_pin_state;
static unsigned int compatible;
static unsigned int screen_flag;
static DECLARE_WAIT_QUEUE_HEAD(screenwaitq);
static DECLARE_WAIT_QUEUE_HEAD(gWaitq);
static DECLARE_WAIT_QUEUE_HEAD(U1_Waitq);
static DECLARE_WAIT_QUEUE_HEAD(U2_Waitq);

#ifdef CONFIG_PM_WAKELOCKS
struct wakeup_source *gIntWakeLock = NULL;
struct wakeup_source *gProcessWakeLock = NULL;
#else
struct wake_lock gIntWakeLock;
struct wake_lock gProcessWakeLock;
#endif //CONFIG_PM_WAKELOCKS

struct work_struct gWork;
struct workqueue_struct *gWorkq;

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_lock);
static DEFINE_MUTEX(drv_lock);
static DEFINE_MUTEX(ioctl_lock);
#ifdef COMPATIBLE_VERSION3
static DECLARE_WAIT_QUEUE_HEAD(drv_waitq);
#endif

static struct fprint_dev *sdev = NULL;
static struct fprint_spi *smas = NULL;

//static u8 stxb[FBUF];
//static u8 srxb[FBUF];
u8 *stxb = NULL;
u8 *srxb = NULL;
//extern char *Fingerprint_name;

int MA_DRV_VERSION = 0x00004005;

static void mas_work(struct work_struct *pws)
{
	smas->f_irq = 1;
	wake_up(&gWaitq);
#ifdef COMPATIBLE_VERSION3
	wake_up(&drv_waitq);
#endif
}

static irqreturn_t mas_interrupt(int irq, void *dev_id)
{
#ifdef DOUBLE_EDGE_IRQ
	if (mas_get_interrupt_gpio(0) == 1) {
		//TODO IRQF_TRIGGER_RISING
	} else {
		//TODO IRQF_TRIGGER_FALLING
	}
#else
	printk("mas_interrupt.\n");
#ifdef CONFIG_PM_WAKELOCKS
	__pm_wakeup_event(gIntWakeLock, 2000);
#else
	wake_lock_timeout(&gIntWakeLock, msecs_to_jiffies(2000));
#endif
	queue_work(gWorkq, &gWork);
#endif
	return IRQ_HANDLED;
}

/*---------------------------------- fops ------------------------------------*/

/* 读写数据
 * @buf 数据
 * @len 长度
 * @返回值：0成功，否则失败
 */
int mas_sync(u8 *txb, u8 *rxb, int len)
{
	int ret = 0;
	struct spi_message m;
	struct spi_transfer t = {
		.tx_buf = txb,
		.rx_buf = rxb,
		.len = len,
		.delay_usecs = 1,
		.bits_per_word = 8,
		.speed_hz = smas->spi->max_speed_hz,
	};

	mutex_lock(&dev_lock);
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	printk("[%s][%d] spi_message %p \r\n", __func__, __LINE__, &m);
	ret = spi_sync(smas->spi, &m);
	mutex_unlock(&dev_lock);

	return ret;
}

static void mas_set_input(void)
{
	struct input_dev *input = NULL;
	input = input_allocate_device();
	if (!input) {
		MALOGW("input_allocate_device failed.");
		return;
	}
	set_bit(EV_KEY, input->evbit);
	set_bit(EV_SYN, input->evbit);
	set_bit(FINGERPRINT_SWIPE_UP, input->keybit); //单触
	set_bit(FINGERPRINT_SWIPE_DOWN, input->keybit);
	set_bit(FINGERPRINT_SWIPE_LEFT, input->keybit);
	set_bit(FINGERPRINT_SWIPE_RIGHT, input->keybit);
	/* Removed for LineageOS
	set_bit(FINGERPRINT_TAP, input->keybit);
	set_bit(FINGERPRINT_DTAP, input->keybit);
	set_bit(FINGERPRINT_LONGPRESS, input->keybit);
	*/

	set_bit(KEY_POWER, input->keybit);

	input->name = MA_CHR_DEV_NAME;
	input->id.bustype = BUS_SPI;
	ret = input_register_device(input);
	if (ret) {
		input_free_device(input);
		MALOGW("failed to register input device.");
		return;
	}
	smas->input = input;
}

//static int mas_ioctl (struct inode *node, struct file *filp, unsigned int cmd, uns igned long arg)
//this function only supported while the linux kernel version under v2.6.36,while the kernel version under v2.6.36, use this line
static long mas_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TIMEOUT_WAKELOCK: //延时锁    timeout lock
#ifdef CONFIG_PM_WAKELOCKS
		__pm_wakeup_event(gProcessWakeLock, 5000);
#else
		wake_lock_timeout(&gProcessWakeLock, 5 * HZ);
#endif
		break;
	case SLEEP: //remove the process out of the runqueue
		smas->f_irq = 0;
		ret = wait_event_freezable(gWaitq, smas->f_irq != 0);
		break;
	case WAKEUP: //wake up, schedule the process into the runqueue
		smas->f_irq = 1;
		wake_up(&gWaitq);
		break;
#ifndef USE_PLATFORM_DRIVE
	case ENABLE_CLK:
		mas_enable_spi_clock(
			smas->spi); //if the spi clock is not opening always, do this methods
		break;
	case DISABLE_CLK:
		mas_disable_spi_clock(smas->spi); //disable the spi clock
		break;
#endif
	case ENABLE_INTERRUPT:
		enable_irq(
			irq); //enable the irq,in fact, you can make irq enable always
		break;
	case DISABLE_INTERRUPT:
		disable_irq(irq); //disable the irq
		break;
	/* Removed for LineageOS
	case TAP_DOWN:
		input_report_key(smas->input, FINGERPRINT_TAP, 1);
		input_sync(smas->input); //tap down
		break;
	case TAP_UP:
		input_report_key(smas->input, FINGERPRINT_TAP, 0);
		input_sync(smas->input); //tap up
		break;
	case SINGLE_TAP:
		input_report_key(smas->input, FINGERPRINT_TAP, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_TAP, 0);
		input_sync(smas->input); //single tap
		break;
	case DOUBLE_TAP:
		input_report_key(smas->input, FINGERPRINT_DTAP, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_DTAP, 0);
		input_sync(smas->input); //double tap
		break;
	case LONG_TAP:
		input_report_key(smas->input, FINGERPRINT_LONGPRESS, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_LONGPRESS, 0);
		input_sync(smas->input); //long tap
		break;
	*/
	case MA_KEY_UP:
		input_report_key(smas->input, FINGERPRINT_SWIPE_UP, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_SWIPE_UP, 0);
		input_sync(smas->input);
		break;
	case MA_KEY_LEFT:
		input_report_key(smas->input, FINGERPRINT_SWIPE_LEFT, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_SWIPE_LEFT, 0);
		input_sync(smas->input);
		break;
	case MA_KEY_DOWN:
		input_report_key(smas->input, FINGERPRINT_SWIPE_DOWN, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_SWIPE_DOWN, 0);
		input_sync(smas->input);
		break;
	case MA_KEY_RIGHT:
		input_report_key(smas->input, FINGERPRINT_SWIPE_RIGHT, 1);
		input_sync(smas->input);
		input_report_key(smas->input, FINGERPRINT_SWIPE_RIGHT, 0);
		input_sync(smas->input);
		break;
	case SET_MODE:
		mutex_lock(&ioctl_lock);

		ret = copy_from_user(&ma_drv_reg, (unsigned int *)arg,
				     sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case GET_MODE:
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &ma_drv_reg,
				   sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case MA_IOC_GVER:
		mutex_lock(&ioctl_lock);
		//*((unsigned int*)arg) = MA_DRV_VERSION;
		ret = copy_to_user((unsigned int *)arg, &MA_DRV_VERSION,
				   sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case SCREEN_ON:
		mas_fingerprint_power(1);
		break;
	case SCREEN_OFF:
		mas_fingerprint_power(0);
		break;
	case SET_SPI_SPEED:
		ret = copy_from_user(&ma_speed, (unsigned int *)arg,
				     sizeof(unsigned int));
		//ma_spi_change(smas->spi, ma_speed, 0);
		break;
	case WAIT_FACTORY_CMD:
		smas->u2_flag = 0;
		ret = wait_event_freezable(U2_Waitq, smas->u2_flag != 0);
		break;
	case WAKEUP_FINGERPRINTD:
		smas->u2_flag = 1;
		wake_up(&U2_Waitq);
		break;
	case WAIT_FINGERPRINTD_RESPONSE:
		smas->u1_flag = 0;
		ret = wait_event_freezable(U1_Waitq, smas->u1_flag != 0);
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &ma_drv_reg,
				   sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case WAKEUP_FACTORY_TEST_SEND_FINGERPRINTD_RESPONSE:
		mutex_lock(&ioctl_lock);
		ret = copy_from_user(&ma_drv_reg, (unsigned int *)arg,
				     sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		msleep(4);
		smas->u1_flag = 1;
		wake_up(&U1_Waitq);
		break;
	case WAIT_SCREEN_STATUS_CHANGE:
		screen_flag = 0;
		ret = wait_event_freezable(screenwaitq, screen_flag != 0);
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &is_screen_on,
				   sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	case GET_INTERRUPT_STATUS:
		int_pin_state = mas_get_interrupt_gpio(0);
		if (int_pin_state == 0 || int_pin_state == 1) {
			mutex_lock(&ioctl_lock);
			ret = copy_to_user((unsigned int *)arg, &int_pin_state,
					   sizeof(unsigned int));
			mutex_unlock(&ioctl_lock);
		}
		break;
	case GET_SCREEN_STATUS:
		mutex_lock(&ioctl_lock);
		ret = copy_to_user((unsigned int *)arg, &is_screen_on,
				   sizeof(unsigned int));
		mutex_unlock(&ioctl_lock);
		break;
	default:
		ret = -EINVAL;
		MALOGW("mas_ioctl no such cmd");
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long mas_compat_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int retval = 0;
	retval = filp->f_op->unlocked_ioctl(filp, cmd, arg);
	return retval;
}
#endif

#ifdef COMPATIBLE_VERSION3
int version3_ioctl(int cmd, int arg)
{
	int ret = 0;

	pr_info("%s: start cmd=0x%.3x arg=%d\n", __func__, cmd, arg);

	switch (cmd) {
	case IOCTL_DEBUG:
		sdeb = (u8)arg;
		break;
	case IOCTL_IRQ_ENABLE:
		break;
#ifndef USE_PLATFORM_DRIVE
	case IOCTL_SPI_SPEED:
		smas->spi->max_speed_hz = (u32)arg;
		spi_setup(smas->spi);
		break;
#endif //USE_PLATFORM_DRIVE
	case IOCTL_COVER_NUM:
		ret = COVER_NUM;
		break;
	case IOCTL_GET_VDATE:
		ret = 20160425;
		break;
	case IOCTL_CLR_INTF:
		smas->f_irq = FALSE;
		break;
	case IOCTL_GET_INTF:
		ret = smas->f_irq;
		break;
	case IOCTL_REPORT_FLAG:
		smas->f_repo = arg;
		break;
	case IOCTL_REPORT_KEY:
		input_report_key(smas->input, arg, 1);
		input_sync(smas->input);
		input_report_key(smas->input, arg, 0);
		input_sync(smas->input);
		break;
	case IOCTL_SET_WORK:
		smas->do_what = arg;
		break;
	case IOCTL_GET_WORK:
		ret = smas->do_what;
		break;
	case IOCTL_SET_VALUE:
		smas->value = arg;
		break;
	case IOCTL_GET_VALUE:
		ret = smas->value;
		break;
	case IOCTL_TRIGGER:
		smas->f_wake = TRUE;
		wake_up_interruptible(&drv_waitq);
		break;
	case IOCTL_WAKE_LOCK:
#ifdef CONFIG_PM_WAKELOCKS
		if (&smas->wl != NULL) {
			if (&smas->wl->active) {
				__pm_stay_awake(&smas->wl);
			}
		}
#else
		if (!wake_lock_active(&smas->wl))
			wake_lock(&smas->wl);
#endif
		break;
	case IOCTL_WAKE_UNLOCK:
#ifdef CONFIG_PM_WAKELOCKS
		if (&smas->wl != NULL) {
			if (&smas->wl->active) {
				__pm_relax(&smas->wl);
			}
		}
#else
		if (wake_lock_active(&smas->wl))
			wake_unlock(&smas->wl);
#endif
		break;
	case IOCTL_KEY_DOWN:
		input_report_key(smas->input, KEY_F11 1);
		input_sync(smas->input);
		break;
	case IOCTL_KEY_UP:
		input_report_key(smas->input, KEY_F11, 0);
		input_sync(smas->input);
		break;
	}

	printd("%s: end. ret=%d f_irq=%d, f_repo=%d\n", __func__, ret,
	      smas->f_irq, smas->f_repo);

	return ret;
}
#endif

#ifndef USE_PLATFORM_DRIVE
/* 写数据
 * @return 成功:count, -1count太大，-2拷贝失败
 */
static ssize_t mas_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *f_pos)
{
	int val = 0;
	// MALOGD("start");
	//cmd ioctl, old version used the write interface to do ioctl, this is only for the old version
	if (count == 6) {
		int cmd, arg;
		u8 tmp[6];
		ret = copy_from_user(tmp, buf, count);
		cmd = tmp[0];
		cmd <<= 8;
		cmd += tmp[1];
		arg = tmp[2];
		arg <<= 8;
		arg += tmp[3];
		arg <<= 8;
		arg += tmp[4];
		arg <<= 8;
		arg += tmp[5];
#ifdef COMPATIBLE_VERSION3
		val = (int)version3_ioctl(NULL, (unsigned int)cmd,
					  (unsigned long)arg);
#endif
	} else {
		memset(stxb, 0, FBUF);
		ret = copy_from_user(stxb, buf, count);
		if (ret) {
			MALOGW("copy form user failed");
			val = -2;
		} else {
			val = count;
		}
	}
	// MALOGD("end");
	return val;
}

/* 读数据
 * @return 成功:count, -1count太大，-2通讯失败, -3拷贝失败
 */
static ssize_t mas_read(struct file *filp, char __user *buf, size_t count,
			loff_t *f_pos)
{
	int val, ret = 0;
	// MALOGD("start");
	ret = mas_sync(stxb, srxb, count);
	if (ret) {
		MALOGW("mas_sync failed.");
		return -2;
	}
	ret = copy_to_user(buf, srxb, count);
	if (!ret)
		val = count;
	else {
		val = -3;
		MALOGW("copy_to_user failed.");
	}
	//  MALOGD("end.");
	return val;
}
#endif

void *kernel_memaddr = NULL;
unsigned long kernel_memesize = 0;

int mas_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long page;
	if (!kernel_memaddr) {
		kernel_memaddr = kmalloc(128 * 1024, GFP_KERNEL);
		if (!kernel_memaddr) {
			return -1;
		}
	}
	page = virt_to_phys((void *)kernel_memaddr) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, page,
			    (vma->vm_end - vma->vm_start), vma->vm_page_prot))
		return -1;
	vma->vm_flags |= VM_RESERVED;
	printk("remap_pfn_rang page:[%lu] ok.\n", page);
	return 0;
}

#ifdef COMPATIBLE_VERSION3
static unsigned int mas_poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	printd("%s: start. f_irq=%d f_repo=%d f_wake=%d\n", __func__,
	       smas->f_irq, smas->f_repo, smas->f_wake);

	poll_wait(filp, &drv_waitq, wait);
	if (smas->f_irq && smas->f_repo) {
		smas->f_repo = FALSE;
		mask |= POLLIN | POLLRDNORM;
	} else if (smas->f_wake) {
		smas->f_wake = FALSE;
		mask |= POLLPRI;
	}

	printd("%s: end. mask=%d\n", __func__, mask);

	return mask;
}
#endif

/*---------------------------------- fops ------------------------------------*/
static const struct file_operations sfops = {
	.owner = THIS_MODULE,
#ifndef USE_PLATFORM_DRIVE
	.write = mas_write,
	.read = mas_read,
#endif
	.unlocked_ioctl = mas_ioctl,
	.mmap = mas_mmap,
//.ioctl = mas_ioctl,
//using the previous line replacing the unlock_ioctl while the linux kernel under version2.6.36
#ifdef CONFIG_COMPAT
	.compat_ioctl = mas_compat_ioctl,
#endif
#ifdef COMPATIBLE_VERSION3
	.poll = mas_poll,
#endif
};
/*---------------------------------- fops end ---------------------------------*/

static int init_file_node(void)
{
	int ret;
	//MALOGF("start");
	ret = alloc_chrdev_region(&sdev->idd, 0, 1, MA_CHR_DEV_NAME);
	if (ret < 0) {
		MALOGW("alloc_chrdev_region error!");
		return -1;
	}

	sdev->chd = cdev_alloc();
	if (!sdev->chd) {
		MALOGW("cdev_alloc error!");
		return -1;
	}

	sdev->chd->owner = THIS_MODULE;
	sdev->chd->ops = &sfops;

	cdev_add(sdev->chd, sdev->idd, 1);

	sdev->cls = class_create(THIS_MODULE, MA_CHR_DEV_NAME);
	if (IS_ERR(sdev->cls)) {
		MALOGE("class_create");
		return -1;
	}

	sdev->dev = device_create(sdev->cls, NULL, sdev->idd, NULL,
				  MA_CHR_FILE_NAME);
	ret = IS_ERR(sdev->dev) ? PTR_ERR(sdev->dev) : 0;
	if (ret) {
		MALOGE("device_create");
	}
	//MALOGF("end");
	return 0;
}

static int deinit_file_node(void)
{
	cdev_del(sdev->chd);
	sdev->chd = NULL;
	kfree(sdev->chd);
	device_destroy(sdev->cls, sdev->idd);
	unregister_chrdev_region(sdev->idd, 1);
	class_destroy(sdev->cls);
	return 0;
}

static int init_interrupt(void)
{
	const char *tname = MA_EINT_NAME;
	int ret = 0;

	irq = mas_get_irq();
	if (irq <= 0) {
		ret = irq;
		MALOGE("mas_get_irq");
	}

#ifdef DOUBLE_EDGE_IRQ
	ret = request_irq(irq, mas_interrupt,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, tname,
			  NULL);
#else
	ret = request_irq(irq, mas_interrupt, IRQF_TRIGGER_RISING, tname, NULL);
#endif
	if (ret < 0) {
		MALOGE("request_irq");
	}

	return ret;
}

static int deinit_interrupt(void)
{
	if (irq) {
		disable_irq(irq);
		free_irq(irq, NULL);
	}
	return 0;
}

static int init_vars(void)
{
	sdev = kmalloc(sizeof(struct fprint_dev), GFP_KERNEL);
	smas = kmalloc(sizeof(struct fprint_spi), GFP_KERNEL);
	if (sdev == NULL || smas == NULL) {
		MALOGW("smas kmalloc failed.");
		if (sdev != NULL)
			kfree(sdev);
		if (smas != NULL)
			kfree(smas);
		return -ENOMEM;
	}

	stxb = kmalloc(FBUF, GFP_KERNEL);
	srxb = kmalloc(FBUF, GFP_KERNEL);
	if (stxb == NULL || srxb == NULL) {
		MALOGW("stxb  srxb kmalloc failed.");
		if (stxb != NULL)
			kfree(stxb);
		if (srxb != NULL)
			kfree(srxb);
		return -ENOMEM;
	}

#ifdef CONFIG_PM_WAKELOCKS
	gIntWakeLock = wakeup_source_register(NULL, "microarray_int_wakelock");
	gProcessWakeLock =
		wakeup_source_register(NULL, "microarray_process_wakelock");
#else
	wake_lock_init(&gIntWakeLock, WAKE_LOCK_SUSPEND,
		       "microarray_int_wakelock");
	wake_lock_init(&gProcessWakeLock, WAKE_LOCK_SUSPEND,
		       "microarray_process_wakelock");
#endif
	INIT_WORK(&gWork, mas_work);
	gWorkq = create_singlethread_workqueue("mas_workqueue");
	if (!gWorkq) {
		MALOGW("create_single_workqueue error!");
		return -ENOMEM;
	}
	return 0;
}
static int deinit_vars(void)
{
	destroy_workqueue(gWorkq);
#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_unregister(gIntWakeLock);
	wakeup_source_unregister(gProcessWakeLock);
#else
	wake_lock_destroy(&gIntWakeLock);
	wake_lock_destroy(&gProcessWakeLock);
#endif
	if (sdev != NULL)
		kfree(sdev);

	if (smas != NULL)
		kfree(smas);

	if (stxb != NULL)
		kfree(stxb);

	if (srxb != NULL)
		kfree(srxb);

	return 0;
}

#ifndef USE_PLATFORM_DRIVE
static int init_spi(struct spi_device *spi)
{
	msleep(50);
	smas->spi = spi;
	smas->spi->max_speed_hz = SPI_SPEED;
	smas->spi->mode = SPI_MODE_0; //CPOL=CPHA=0
	smas->spi->bits_per_word = 8;
	spi_setup(spi);
	INIT_LIST_HEAD(&smas->dev_entry);
	return 0;
}

static int deinit_spi(struct spi_device *spi)
{
	smas->spi = NULL;
	mas_disable_spi_clock(spi);
	return 0;
}
#endif

/*
 * init_connect function to check whether the chip is microarray's
 * @return 0 not 1 yes
 * param void
 */
#ifdef REE
int init_connect(void)
{
	int i;
	int res = 0;

	for (i = 0; i < 4; i++) {
		stxb[0] = 0x8c;
		stxb[1] = 0xff;
		stxb[2] = 0xff;
		stxb[3] = 0xff;
		mas_sync(stxb, srxb, 4);
		msleep(8);
		stxb[0] = 0x00;
		stxb[1] = 0xff;
		stxb[2] = 0xff;
		stxb[3] = 0xff;
		ret = mas_sync(stxb, srxb, 4);
		if (ret != 0)
			MALOGW("do init_connect failed!");
		printk("guq srxb[3] = %d srxb[2] = %d\n", srxb[3], srxb[2]);
		if (srxb[3] == 0x41 || srxb[3] == 0x45) {
			res = 1;
		} else {
			res = 0;
		}
	}
	if (res == 1) {
		stxb[0] = 0x80;
		stxb[1] = 0xff;
		stxb[2] = 0xff;
		stxb[3] = 0xff;
		mas_sync(stxb, srxb, 4);
	}
	return res;
}

int deinit_connect(void)
{
	return 0;
}
#endif

static int mas_fb_notifier_callback(struct notifier_block *self,
				    unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	unsigned int blank;
	if (event != FB_EVENT_BLANK) {
		return 0;
	}
	blank = *(int *)evdata->data;
	switch (blank) {
	case FB_BLANK_UNBLANK:
		is_screen_on = 1;
		break;
	case FB_BLANK_POWERDOWN:
		is_screen_on = 0;
		break;
	default:
		break;
	}
	screen_flag = 1;
	wake_up(&screenwaitq);
	return 0;
}

static int init_notifier_call(void);
static int deinit_notifier_call(void);

static int init_notifier_call(void)
{
	notifier.notifier_call = mas_fb_notifier_callback;
	fb_register_client(&notifier);
	is_screen_on = 1;
	return 0;
}
static int deinit_notifier_call(void)
{
	fb_unregister_client(&notifier);
	return 0;
}

#ifdef USE_PLATFORM_DRIVE
int mas_probe(struct platform_device *spi)
{
#else
int mas_probe(struct spi_device *spi)
{
#endif
	int ret;
	pr_err("mas_probe,by eric.wang\n");

	ret = mas_qcm_platform_init(spi);
	pr_err("mas_qcm_platform_init:ret=%d,by eric.wang\n", ret);
	if (ret)
		goto err0;

	ret = init_vars();
	pr_err("init_vars:ret=%d,by eric.wang\n", ret);
	if (ret) {
		goto err1;
	}

	ret = init_interrupt();
	pr_err("init_interrupt:ret=%d,by eric.wang\n", ret);
	if (ret) {
		goto err2;
	}

	ret = init_file_node();
	pr_err("init_file_node:ret=%d,by eric.wang\n", ret);
	if (ret) {
		goto err3;
	}

#ifndef USE_PLATFORM_DRIVE
	ret = init_spi(spi);
	pr_err("init_spi:ret=%d,by eric.wang\n", ret);
	if (ret) {
		goto err4;
	}
#endif

#ifdef REE
	ret = init_connect();
#elif defined TEE
	ret = 1;
#endif
	if (ret == 0) { //not chip
		compatible = 0;
		pr_info("%s:init_connect failed.\n", __func__);
#ifdef USE_PLATFORM_DRIVE
		goto err3;
#else
		goto err5;
#endif
	} else
		pr_err("%s:init_connect successfully.\n", __func__);

	mas_set_input();
	MALOGF("end");

	ret = init_notifier_call();
	if (ret != 0) {
		ret = -ENODEV;
		goto err6;
	}

	mas_set_wakeup(spi);
	//Fingerprint_name="Microarray_A121N";
	pr_err("%s:completed.\n", __func__);
	return ret;

err6:
	deinit_notifier_call();
#ifdef REE
err5:
	deinit_connect();
#endif
#ifndef USE_PLATFORM_DRIVE
err4:
	deinit_spi(spi);
#endif
err3:
	deinit_file_node();
err2:
	mas_qcm_platform_uninit(spi);
err1:
	deinit_interrupt();
	deinit_vars();
err0:
	return -EINVAL;
}

#ifdef USE_PLATFORM_DRIVE
int mas_remove(struct platform_device *spi)
{
#else
int mas_remove(struct spi_device *spi)
{
#endif
	deinit_file_node();
	deinit_interrupt();
	deinit_vars();
	return 0;
}

static int __init mas_init(void)
{
	int ret = 0;
	MALOGF("start");
	compatible = 1;
	ret = mas_get_platform();
	if (ret) {
		MALOGE("mas_get_platform");
	}

	return ret;
}

static void __exit mas_exit(void)
{
}

late_initcall_sync(mas_init);
module_exit(mas_exit);

MODULE_AUTHOR("Microarray");
MODULE_DESCRIPTION("Driver for microarray fingerprint sensor");
MODULE_LICENSE("GPL");
