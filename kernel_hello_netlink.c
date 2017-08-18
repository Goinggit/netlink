#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <net/sock.h>
#include <linux/netlink.h>

#define NETLINK_TEST 17
    __u32 pid;

static struct sock *netlinkfd = NULL;

int send_to_user(void      *arg) //发送到用户空间
{
    int 			size;
    struct sk_buff  *skb;
    unsigned int 	old_tail;
	int 			retval;
	struct 	tcmsg 	*tcm;
	struct nlmsghdr *nlh; //报文头
	struct nlmsghdr *hrd = (struct nlmsghdr *)arg;

	tcm		 		 = (struct 	tcmsg*)kmalloc(sizeof(struct tcmsg), GFP_KERNEL);
    size 	 		 = NLMSG_SPACE(sizeof(struct tcmsg)); //报文大小
    //分配一个新的套接字缓存,使用GFP_ATOMIC标志进程不>会被置为睡眠
    skb  	 		 = alloc_skb(size, GFP_ATOMIC); 
	old_tail 		 = skb->tail;
    //初始化一个netlink消息首部
    nlh = nlmsg_put(skb, 0, 0, 0, NLMSG_SPACE(strlen(info))-sizeof(struct nlmsghdr), 0); 
    nlh  	 		 = nlmsg_put(skb, 0, 0, 0, NLMSG_SPACE(sizeof(struct tcmsg))-sizeof(struct nlmsghdr), 0); 
    
	nlh->nlmsg_pid   = hrd->nlmsg_pid;
	tcm->tcm_family	 = AF_UNSPEC;
	tcm->tcm__pad1	 = 0;
	tcm->tcm__pad2	 = 0;
	tcm->tcm_ifindex = 1;
	tcm->tcm_parent	 = 1001;
	tcm->tcm_info	 = 1002;
    nlh->nlmsg_len   = skb->tail - old_tail; //设置消息长度

    //设置控制字段
    NETLINK_CB(skb).portid 	  = 0;
    NETLINK_CB(skb).dst_group = 0;

    printk(KERN_DEBUG "[kernel space] skb->data:%s\n", (char *)NLMSG_DATA((struct nlmsghdr *)skb->data));

    //发送数据
    retval = netlink_unicast(netlinkfd, skb, hrd->nlmsg_pid, MSG_DONTWAIT);
    printk(KERN_DEBUG "[kernel space] netlink_unicast return: %d\n", retval);
    return 0;
}

void kernel_receive(struct sk_buff *__skb) //内核从用户空间接收数据
{
    struct sk_buff  *skb;
    struct nlmsghdr *nlh  = NULL;

    char 			*data = "This is eric's test message from kernel";

    skb = skb_get(__skb);

    if(skb->len >= sizeof(struct nlmsghdr)){
        nlh = (struct nlmsghdr *)skb->data;
        if((nlh->nlmsg_len >= sizeof(struct nlmsghdr))
            && (__skb->len >= nlh->nlmsg_len)){
            printk(KERN_DEBUG "[kernel space] data receive from user are:%s\n", (char *)NLMSG_DATA(nlh));
            send_to_user(nlh);
        }
    }else{
        printk(KERN_DEBUG "[kernel space] data receive from user are:%s\n",(char *)NLMSG_DATA(nlmsg_hdr(__skb)));
        send_to_user(data);
    }

    kfree_skb(skb);
}

int __init test_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = 
	{
    .input = kernel_receive,
	};

    netlinkfd = netlink_kernel_create(&init_net, NETLINK_TEST, &cfg);
    if(!netlinkfd){
        printk(KERN_ERR "can not create a netlink socket\n");
        return -1;
    }
    return 0;
}

void __exit test_netlink_exit(void)
{
    sock_release(netlinkfd->sk_socket);
    printk(KERN_DEBUG "test_netlink_exit!!\n");
}

module_init(test_netlink_init);
module_exit(test_netlink_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("going");

