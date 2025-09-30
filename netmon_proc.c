#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>
+#include <linux/string.h>     /* strlen, strcmp */
#include <linux/version.h>    /* LINUX_VERSION_CODE / KERNEL_VERSION */

#define PROC_NAME "netmon"

static char *iface = NULL;
module_param(iface, charp, 0);

static int netmon_show(struct seq_file *m, void *v) {
    struct net_device *dev;

    if (iface && strlen(iface) > 0) {
        dev = dev_get_by_name(&init_net, iface);
        if (!dev) {
            seq_printf(m, "Interface %s not found\n", iface);
            return 0;
        }
        seq_printf(m, "{\"interface\":\"%s\",\"rx_bytes\":%lu,\"tx_bytes\":%lu}\n",iface, dev->stats.rx_bytes, dev->stats.tx_bytes);
        dev_put(dev);
    } else {
        // Show all interfaces except "lo"
        read_lock(&device_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
        for_each_netdev(&init_net, dev) {
#else
        for (dev = first_net_device(&init_net); dev; dev = next_net_device(dev)) {
#endif
            if (strcmp(dev->name, "lo") == 0)
                continue;
            seq_printf(m, "{\"interface\":\"%s\",\"rx_bytes\":%lu,\"tx_bytes\":%lu}\n",dev->name, dev->stats.rx_bytes, dev->stats.tx_bytes);
        }
        read_unlock(&device_lock);
    }
    return 0;
}

static int netmon_open(struct inode *inode, struct file *file) {
    return single_open(file, netmon_show, NULL);
}

static const struct proc_ops netmon_fops = {
    .proc_open = netmon_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init netmon_init(void) {
    proc_create(PROC_NAME, 0, NULL, &netmon_fops);
    return 0;
}

static void __exit netmon_exit(void) {
    remove_proc_entry(PROC_NAME, NULL);
}

module_init(netmon_init);
module_exit(netmon_exit);
MODULE_LICENSE("GPL");