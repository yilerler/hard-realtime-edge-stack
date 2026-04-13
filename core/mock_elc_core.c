#include <linux/kthread.h>   // ⚠️ 新增：核心執行緒支援
#include <linux/delay.h>     // ⚠️ 新增：usleep_range 支援
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/spinlock.h>  // 🛡️ 引入自旋鎖
#include <linux/random.h>    // 🎲 引入亂數產生器 (模擬真實 I/O 數據)
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "sensor_ioctl.h"

#define DEVICE_NAME "mock_elc"
#define CLASS_NAME "elc_class"
#define FILTER_WINDOW_SIZE 5

/* * 🗄️ 邊緣邏輯控制器 (ELC) 核心設備結構體
 * 掌管全局狀態、暫存器地圖與自旋鎖
 */
struct elc_device {
    struct cdev cdev;
    spinlock_t lock;
    struct task_struct *mock_thread; // ⚠️ 將 timer 換成 thread 指標
    struct sensor_data reg_map;
    
    int is_active;
    struct class *dev_class;
    struct device *dev_device;

    int filter_history[FILTER_WINDOW_SIZE];
    int filter_index;
};

static dev_t dev_num;
static struct elc_device *my_dev;

/* =========================================================================
 * ⬇️ Layer 1: 南向混沌物理引擎 (Southbound Chaos Engine - SIL Mock)
 * 職責：模擬真實工業環境中的高頻時序、電磁突波與實體崩潰。
 * 拋棄所有業務邏輯(PM2.5/RFID)，只產出純粹的「物理壓力訊號」。
 * ========================================================================= */
#include <linux/random.h>

// 定義真實物理世界的三大狀態
enum physics_state {
    STATE_CRUISING = 0,     // 平穩運作 (安全)
    STATE_EMI_SPIKE = 1,    // 電磁干擾突波 (假警報，測試防呆)
    STATE_CRITICAL_CRASH = 2 // 物理崩潰 (如水壓喪失、馬達暴衝，測試急停)
};

static enum physics_state current_state = STATE_CRUISING;
static int state_tick = 0; // 用來記錄在同一個狀態停留了多久

// 假設這是一個通用的「關鍵安全指標」(可能是距離 mm，也可能是水壓 psi)
#define SAFE_BASELINE 1000
#define CRITICAL_THRESHOLD 500

static int hal_generate_chaos_signal(void) {
    int output_signal = SAFE_BASELINE;
    int micro_noise = (int)(get_random_u32() % 20) - 10; // ±10 的物理微震盪
    
    state_tick++;

    // 🌪️ 狀態轉移上帝邏輯：決定何時降下災難
    if (current_state == STATE_CRUISING && state_tick > 2000) {
        // 平穩運行一段時間後，隨機觸發雜訊或真實崩潰 (機率 8:2)
        current_state = (get_random_u32() % 10 > 7) ? STATE_CRITICAL_CRASH : STATE_EMI_SPIKE;
        state_tick = 0;
    }

    // ⚙️ 狀態機執行邏輯：根據當前狀態吐出物理訊號
    switch (current_state) {
        case STATE_CRUISING:
            output_signal = SAFE_BASELINE + micro_noise;
            break;

        case STATE_EMI_SPIKE:
            // 瞬間產生極端高值 (例如 2500)，模擬電磁干擾
            output_signal = 2500 + micro_noise;
            // 突波只維持極短時間 (例如 5 個 tick)，然後瞬間恢復
            if (state_tick > 5) {
                current_state = STATE_CRUISING;
                state_tick = 0;
            }
            break;

        case STATE_CRITICAL_CRASH:
            // 斷崖式物理崩潰！數值瞬間掉到危險閥值以下
            output_signal = 300 + micro_noise; // < CRITICAL_THRESHOLD
            // 崩潰會維持一段時間，這正是考驗 Spinlock 能不能在這段時間內鎖死並拉起警報的時刻
            if (state_tick > 50) {
                current_state = STATE_CRUISING; // 模擬系統保護介入後重啟
                state_tick = 0;
            }
            break;
    }

    return output_signal;
}

/* =========================================================================
 * 🧠 Layer 2: ELC 高頻物理中斷模擬引擎 (Kthread Chaos Injector)
 * 職責：作為獨立執行緒，以每秒 1000 次的頻率產生混沌訊號，暴力搶奪自旋鎖。
 * ========================================================================= */
static int mock_thread_fn(void *data) {
    struct elc_device *dev = (struct elc_device *)data;
    unsigned long flags;
    int raw_dist, clean_dist, i;
    long sum;

    // kthread 主迴圈：只要模組沒被卸載，就會無限狂奔
    while (!kthread_should_stop()) {
        sum = 0;

        // 1. 呼叫 Layer 1 的混沌引擎獲取物理數據
        raw_dist = hal_generate_chaos_signal();

        // 2. 核心邊緣運算：DSP 滑動平均濾波 (清洗雜訊)
        dev->filter_history[dev->filter_index] = raw_dist;
        dev->filter_index = (dev->filter_index + 1) % FILTER_WINDOW_SIZE;
        for (i = 0; i < FILTER_WINDOW_SIZE; i++) sum += dev->filter_history[i];
        clean_dist = (int)(sum / FILTER_WINDOW_SIZE);

        /* ---------------------------------------------------------
         * 🔒 進入中斷安全禁區 (取得 Spinlock)
         * --------------------------------------------------------- */
        spin_lock_irqsave(&dev->lock, flags);

        // 3. 更新統一暫存器地圖
        dev->reg_map.timestamp = jiffies;
        dev->reg_map.distance_mm = clean_dist;
        // 因為我們移除了 PM2.5 等業務，這裡直接填 0，讓焦點回到物理防禦
        dev->reg_map.pm25 = 0;
        dev->reg_map.noise_db = 0;
        dev->reg_map.rfid_card_id = 0; 

        // 4. 物理急停狀態機 (Hardware Interlock)
        if (clean_dist < CRITICAL_THRESHOLD) {
            if (dev->reg_map.motor_status != STATUS_EMERGENCY_STOP) {
                printk(KERN_EMERG "[ELC Chaos] 💥 CRITICAL CRASH INJECTED! Distance: %d. Interlock Triggered!\n", clean_dist);
                dev->reg_map.motor_status = STATUS_EMERGENCY_STOP;
            }
        } else {
            if (dev->reg_map.motor_status != STATUS_NORMAL) {
                printk(KERN_INFO "[ELC Chaos] 🛡️ SAFE: System Recovered from Crash. Motor Ready.\n");
                dev->reg_map.motor_status = STATUS_NORMAL;
            }
        }

        /* ---------------------------------------------------------
         * 🔓 解除禁區
         * --------------------------------------------------------- */
        spin_unlock_irqrestore(&dev->lock, flags);

        // 5. 模擬硬體震盪器：每 1 毫秒 (1000Hz) 甦醒一次發動突襲
        usleep_range(1000, 1200); 
    }
    
    return 0;
}

/* =========================================================================
 * ⬆️ Layer 3: 北向通訊介面層 (Northbound Interface)
 * 職責：處理 Node.js 下達的 ioctl 請求，嚴格防禦 copy_to_user 睡眠陷阱。
 * ========================================================================= */
static long elc_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct elc_device *dev = file->private_data;
    
    // ⚠️ 關鍵防禦：宣告在 Stack 上的 24 Bytes 區域變數 (極度安全，無溢位風險)
    struct sensor_data local_copy; 
    unsigned long flags;
    int ret = 0;

    switch (cmd) {
        case IOCTL_GET_DATA:
            // 1. 🔒 快速鎖定，只做 O(1) 的記憶體深拷貝 (Deep Copy)
            spin_lock_irqsave(&dev->lock, flags);
            local_copy = dev->reg_map; 
            spin_unlock_irqrestore(&dev->lock, flags);
            
            // 2. 🔓 解鎖後，才執行危險且可能觸發 Page Fault 睡眠的 copy_to_user
            if (copy_to_user((struct sensor_data *)arg, &local_copy, sizeof(struct sensor_data))) {
                ret = -EFAULT;
            }
            break;
            
        case IOCTL_SET_MOCK_DISTANCE:
            // 保留給未來的故障注入 (Fault Injection) 測試
            break;

        default:
            ret = -EINVAL;
    }
    return ret;
}

static int elc_open(struct inode *inode, struct file *file) {
    struct elc_device *dev = container_of(inode->i_cdev, struct elc_device, cdev);
    file->private_data = dev;
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = elc_open,
    .unlocked_ioctl = elc_ioctl,
};

/* =========================================================================
 * 生命週期管理 (Init & Exit)
 * ========================================================================= */
static int __init elc_init(void) {
    int ret;
    if ((ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME)) < 0) return ret;

    // 於 Kernel Heap 動態配置主體結構，避免 Stack Overflow
    my_dev = kzalloc(sizeof(struct elc_device), GFP_KERNEL);
    if (!my_dev) {
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }

    spin_lock_init(&my_dev->lock); // 初始化自旋鎖

    cdev_init(&my_dev->cdev, &fops);
    if ((ret = cdev_add(&my_dev->cdev, dev_num, 1)) < 0) goto err_cdev;

    // 建立 Device Class (相容新舊版 Kernel API)
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
        my_dev->dev_class = class_create(CLASS_NAME);
    #else
        my_dev->dev_class = class_create(THIS_MODULE, CLASS_NAME);
    #endif
    
    if (IS_ERR(my_dev->dev_class)) { ret = PTR_ERR(my_dev->dev_class); goto err_class; }

    my_dev->dev_device = device_create(my_dev->dev_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(my_dev->dev_device)) { ret = PTR_ERR(my_dev->dev_device); goto err_device; }

    // 初始化狀態機與物理模擬
    my_dev->is_active = 1;
    my_dev->reg_map.motor_status = STATUS_NORMAL;
// ⚠️ 核心啟動：喚醒那頭名為 kthread 的物理野獸！
    my_dev->mock_thread = kthread_run(mock_thread_fn, my_dev, "elc_chaos_thread");
    if (IS_ERR(my_dev->mock_thread)) {
        printk(KERN_ERR "ELC Core: Failed to create kthread\n");
        // 為了簡單起見，這裡先略過 error handling 的 goto 鏈，實務上要補上
    }

    printk(KERN_INFO "ELC Core: V4.4 Chaos Engine Initialized successfully.\n");
    return 0;
    
err_device:
    class_destroy(my_dev->dev_class);
err_class:
    cdev_del(&my_dev->cdev);
err_cdev:
    kfree(my_dev);
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit elc_exit(void) {
    my_dev->is_active = 0;
    
    // ⚠️ 停止並摧毀 kthread
    if (my_dev->mock_thread) {
        kthread_stop(my_dev->mock_thread);
    }

    device_destroy(my_dev->dev_class, dev_num);
    class_destroy(my_dev->dev_class);
    cdev_del(&my_dev->cdev);
    kfree(my_dev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "ELC Core: Chaos Engine Offline. Goodbye.\n");
}

module_init(elc_init);
module_exit(elc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua");
MODULE_DESCRIPTION("Edge Logic Controller (ELC) Modbus Polling Engine & Hardware Interlock");