/* Copyright (C) MicroArray
 * MicroArray Fprint Driver Code * madev.h
 * Date: 2017-3-9
 * Version: v4.0.05
 * Author: guq
 * Contact: guq@microarray.com.cn
 */

#ifndef __MADEV_H_
#define __MADEV_H_

//settings macro
#define QUALCOMM //[MTK|QUALCOMM|SPRD]

#define TEE //[TEE|REE] //select platform

#define MALOGD_LEVEL	KERN_EMERG //[KERN_DEBUG|KERN_EMERG] usually, the debug level is used for the release version

#define MA_CHR_FILE_NAME	"madev0" //do not neeed modify usually
#define MA_CHR_DEV_NAME		"madev" //do not neeed modify usually

#define USE_PLATFORM_DRIVE

#define MA_EINT_NAME		"afs121_irq"

//#define DOUBLE_EDGE_IRQ

//#define COMPATIBLE_VERSION3

//key define   just modify the KEY_FN_* for different platform
#define FINGERPRINT_SWIPE_UP		KEY_FN_F1 //827
#define FINGERPRINT_SWIPE_DOWN		KEY_FN_F2 //828
#define FINGERPRINT_SWIPE_LEFT		KEY_FN_F3 //829
#define FINGERPRINT_SWIPE_RIGHT		KEY_FN_F4 //830
#define FINGERPRINT_TAP			KEY_FN_F5 //831
#define FINGERPRINT_DTAP		KEY_FN_F6 //832
#define FINGERPRINT_LONGPRESS		KEY_FN_F7 //833

//key define end
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP) //kingsun/zlc:

//old macro
#define SPI_SPEED (6 * 1000000) //120/121:10M, 80/81:6M

//表面类型
#define COVER_T		1
#define COVER_N		2
#define COVER_M		3
#define COVER_NUM	COVER_N

//指纹类型
#define AFS120	0x78
//#define AFS80 	0x50

#define FBUF (32 * 1024)

#if defined(AFS120)
#define W	120 //宽
#define H	120 //高
#define WBUF	121
#elif defined(AFS80)
#define W	80 //宽
#define H	192 //高
#define WBUF	81
#define FIMG	(W * H)
#endif

//settings macro end
#include <linux/poll.h>
#include <linux/notifier.h>
#include <linux/fb.h>
//this two head file for the screen on/off test
#include <linux/freezer.h>

#include <asm/ioctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/ioctl.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#else
#include <linux/wakelock.h>
#endif
#include <linux/mm.h>
#include "ioctl_cmd.h"

#ifdef MTK
#include "mtk-settings.h"
#elif defined QUALCOMM
#include "qcom-settings.h"
#elif defined SPRD
#include "sprd-settings.h"
#elif defined X86
#include "x86-settings.h"
#endif

//value define
//fprint_spi struct use to save the value
struct fprint_spi {
	u8 do_what; //工作内容
	u8 f_wake; //唤醒标志
	int value;
	volatile u8 f_irq; //中断标志
	volatile u8 u1_flag; //reserve for ours thread interrupt
	volatile u8 u2_flag; //reserve for ours thread interrupt
	volatile u8 f_repo; //上报开关
	spinlock_t spi_lock;
	struct spi_device *spi;
	struct list_head dev_entry;
	struct spi_message msg;
	struct spi_transfer xfer;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *workq;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend suspend;
#endif
#ifdef CONFIG_PM_WAKELOCKS
	struct wakeup_source *wl;
#else
	struct wake_lock wl;
#endif
};
//end

struct fprint_dev {
	dev_t idd;
	int major;
	int minor;
	struct cdev *chd;
	struct class *cls;
	struct device *dev;
};

//function define

//extern the settings.h function
#ifdef USE_PLATFORM_DRIVE
extern int mas_qcm_platform_init(struct platform_device *spi);
extern int mas_qcm_platform_uninit(struct platform_device *spi);
extern void mas_set_wakeup(struct platform_device *spi);
#else
extern int mas_qcm_platform_init(struct spi_device *spi);
extern int mas_qcm_platform_uninit(struct spi_device *spi);
extern void mas_select_transfer(struct spi_device *spi, int len);
extern void mas_enable_spi_clock(struct spi_device *spi);
extern void mas_disable_spi_clock(struct spi_device *spi);
extern void ma_spi_change(struct spi_device *spi, unsigned int speed, int flag);
extern void mas_set_wakeup(struct spi_device *spi);
#endif

extern int mas_fingerprint_power(bool flags);
extern int mas_finger_get_gpio_info(struct platform_device *pdev);
extern int mas_finger_set_gpio_info(int cmd);
extern unsigned int mas_get_irq(void);
extern int mas_get_platform(void);
extern int mas_remove_platform(void);
extern int mas_get_interrupt_gpio(unsigned int index);
//end

//use for the log print
#define MALOG_TAG "MAFP_"
#define MALOGE(x) \
	printk(KERN_ERR \
	       "%s%s: error log! the function %s is failed, ret = %d\n", \
	       MALOG_TAG, __func__, x, ret); //error log
#define MALOGF(x) \
	printk(MALOGD_LEVEL "%s%s: debug log! %s!\n", MALOG_TAG, __func__, \
	       x); //flag log
#define MALOGD(x) MALOGF(x) //debug log
#define MALOGW(x) \
	printk(KERN_WARNING "%s%s: warning log! the function %s's ret = %d\n", \
	       MALOG_TAG, __func__, x, ret); //warning log
//use for the log print

/**
 *	the old ioctl command, compatible for the old version
 */
//ioctl cmd
#ifdef COMPATIBLE_VERSION3
#define IOCTL_DEBUG		0x100 //调试信息 			//debug message
#define IOCTL_IRQ_ENABLE	0x101 //中断使能 			//enable interrupt
#define IOCTL_SPI_SPEED		0x102 //SPI速度 			//spi speed
#define IOCTL_READ_FLEN		0x103 //读帧长度(保留)		//the length of one frame
#define IOCTL_LINK_DEV		0x104 //连接设备(保留)		//connect the device
#define IOCTL_COVER_NUM		0x105 //材料编号			//the index of the material
#define IOCTL_GET_VDATE		0x106 //版本日期			//the date fo the version

#define IOCTL_CLR_INTF		0x110 //清除中断标志
#define IOCTL_GET_INTF		0x111 //获取中断标志
#define IOCTL_REPORT_FLAG	0x112 //上报标志
#define IOCTL_REPORT_KEY	0x113 //上报键值
#define IOCTL_SET_WORK		0x114 //设置工作
#define IOCTL_GET_WORK		0x115 //获取工作
#define IOCTL_SET_VALUE		0x116 //设值
#define IOCTL_GET_VALUE		0x117 //取值
#define IOCTL_TRIGGER		0x118 //自触发
#define IOCTL_WAKE_LOCK		0x119 //唤醒上锁
#define IOCTL_WAKE_UNLOCK	0x120 //唤醒解锁

#define IOCTL_SCREEN_ON		0x121

#define IOCTL_KEY_DOWN		0x121 //按下
#define IOCTL_KEY_UP		0x122 //抬起
#define IOCTL_SET_X		0x123 //偏移X
#define IOCTL_SET_Y		0x124 //偏移Y
#define IOCTL_KEY_TAP		0x125 //单击
#define IOCTL_KEY_DTAP		0x126 //双击
#define IOCTL_KEY_LTAP		0x127 //长按

#define IOCTL_ENABLE_CLK	0x128
#define TRUE	1
#define FALSE	0
#endif

#endif /* __MADEV_H_ */
