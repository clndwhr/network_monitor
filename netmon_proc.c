#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>
#include <linux/string.h>     /* strlen, strcmp */
#include <linux/version.h>    /* LINUX_VERSION_CODE / KERNEL_VERSION */
#include <net/net_namespace.h>

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
        seq_printf(m, "{\"interface\":\"%s\",\"rx_bytes\":%lu,\"tx_bytes\":%lu}\n",
                   iface, dev->stats.rx_bytes, dev->stats.tx_bytes);
        dev_put(dev);
    } else {
        // Show all interfaces except "lo"
        rtnl_lock();
        for_each_netdev(&init_net, dev) {
            if (strcmp(dev->name, "lo") == 0)
                continue;
            seq_printf(m, "{\"interface\":\"%s\",\"rx_bytes\":%lu,\"tx_bytes\":%lu}\n",
                       dev->name, dev->stats.rx_bytes, dev->stats.tx_bytes);
        }
        rtnl_unlock();
    }
    return 0;
}

static int netmon_open(struct inode *inode, struct file *file) {
    return single_open(file, netmon_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops netmon_fops = {
    .proc_open = netmon_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct file_operations netmon_fops = {
    .open = netmon_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

static int __init netmon_init(void) {
    proc_create(PROC_NAME, 0444, NULL, &netmon_fops);
    return 0;
}

static void __exit netmon_exit(void) {
    remove_proc_entry(PROC_NAME, NULL);
}

module_init(netmon_init);
module_exit(netmon_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network interface statistics monitor");
MODULE_AUTHOR("Christopher Landwehr<github.com/clndwhr/>");