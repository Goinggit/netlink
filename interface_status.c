#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <asm/types.h> /* 根据`man 7 rtnetlink`，包含此头文件。（此头文件不包含也能编译通过，但为了兼容性，或者说平台移植性，包含上吧，万一去掉后放到非Linux系统下编不过了呢） */
#include <sys/socket.h>
#include <sys/ioctl.h> /* `man 7 rtnetlink`中有"ifi_flags  contains  the  device  flags,  see  netdevice(7)"这句话，`man 7 netdevice`中提到包含此头文件。 */
//#include <net/if.h> /* use the following header instead of this one */
#include <linux/if.h> /* this one is suite for the Linux, more related to interact with Linux kernel's network interfaces */
#include <linux/if_arp.h> /* 包含ARPHRD_LOOPBACK的定义 */
#include <linux/netlink.h> /* 根据`man 7 rtnetlink`，包含此头文件 */
#include <linux/rtnetlink.h> /* 根据`man 7 rtnetlink`，包含此头文件 */
 
#define PRINTF(fmt, ...) printf("%s:%d: " fmt, __FUNCTION__, __LINE__, ## __VA_ARGS__)
 
#define BUF_SIZE 4096 /* 看到`man 7 netlink`中reading netlink message那个EXAMPLE的buf的大小是4096，我也就写这么大了。 */
 
static int init(struct sockaddr_nl *psa);
static int deinit();
static int msg_req();
static int msg_loop(struct msghdr *pmh);
static void sig_handler(int sig);
 
static int sfd = -1;
 
int main (int argc, char *argv[])
{
    int ret = 0;
    /* 下面4行变量声明参考`man 7 netlink`中reading netlink message那个EXAMPLE，和`man 2 recvmsg`中"The recvmsg() call uses a msghdr structure to minimize the number of directly supplied arguments"这行之后的结构体说明。 */
    char buf[BUF_SIZE];
    struct iovec iov = {buf, sizeof(buf)};
    struct sockaddr_nl sa;
    struct msghdr msg = {(void *)&sa, sizeof(sa), &iov, 1, NULL, 0, 0};
 
    /* 程序的主要流程体现在下面4个函数调用：
     * 1. 初始化socket
     * 2. 请求获取当前link状态
     * 3. 循环等待接收link状态的消息
     * 4. 反初始化
     */
    ret = init(&sa);
    if (!ret) {
        ret = msg_req();
    }
    if (!ret) {
        ret = msg_loop(&msg);
    }
    ret = deinit();
 
    return ret;
}
 
static int init(struct sockaddr_nl *psa)
{
    int 			ret = 0;
    struct sigaction sigact;
 
    sigact.sa_handler   = sig_handler;
    if (!ret && -1 == sigemptyset(&sigact.sa_mask)) {
        PRINTF("ERROR! sigemptyset\n");
        ret = -1;
    }
    if (!ret && -1 == sigaction(SIGINT, &sigact, NULL)) { /* SIGINT用于保证按Ctrl+C时进程能够在退出前执行反初始化（资源释放） */
        PRINTF("ERROR! sigaction SIGINT\n");
        ret = -1;
    }
    if (!ret && -1 == sigaction(SIGTERM, &sigact, NULL)) { /* SIGINT用于保证按Ctrl+C时进程能够在退出前执行反初始化（资源释放） */
        PRINTF("ERROR! sigaction SIGTERM\n");
        ret = -1;
    }
 
    if (!ret) {
        /* `man 7 netlink`中有"the netlink protocol does not distinguish between datagram and raw sockets"这样一句话，所以socket函数的第2个参数填SOCK_RAW和SOCK_DGRAM均可。 */
        /* 根据`man 7 netlink`中netlink_family参数的解释，NETLINK_ROUTE作为netlink_family参数的值能够获取link状态信息。 */
        sfd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
        if (-1 == sfd) {
            PRINTF("ERROR! socket: %s\n", strerror(errno));
            ret = -1;
        }
    }
    /* bind代码参考`man 7 netlink`中EXAMPLE部分的第1段代码 */
    memset(psa, 0, sizeof(*psa));
    psa->nl_family = AF_NETLINK;
    psa->nl_groups = RTMGRP_LINK; /* 绑定到RTMGRP_LINK组 */
    if (!ret && bind(sfd, (struct sockaddr *)psa, sizeof(*psa))) {
        PRINTF("ERROR! bind: %s\n", strerror(errno));
        ret = -1;
    }
 
    /* deinit if init failed */
    if (0 != ret) {
        deinit();
    }
 
    return ret;
}
 
static int deinit()
{
    int ret = 0;
 
    if (-1 != sfd) {
        if (-1 == close(sfd)) {
            PRINTF("ERROR! close: %s\n", strerror(errno));
            ret = -1;
        }
        sfd = -1;
    }
 
    return ret;
}
 
static int msg_req()
{
    int ret = 0;
    struct {
        struct nlmsghdr  nh;
        struct ifinfomsg ifimsg;
    } req;
 
    /* 发送消息来请求获取当前link状态，这是为了当程序启动后能够显示一下当前的网卡连接状态，
       否则程序运行时网卡连接状态没有变化的话，程序就一直没有任何输出 */
    /* 代码参考`man 3 rtnetlink`中的EXAMPLE，主要参考了
       http://fossies.org/linux/misc/open-fcoe-3.19.tar.gz/open-fcoe-3.19/fcoe-utils/lib/rtnetlink.c
       中的send_getlink_dump函数 */
    memset(&req, 0, sizeof(req));
	/* NLMSG_LENGTH会返回包含nlmsghdr这个header和ifinfomsg这个payload加起来的size，
	   返回的是对齐后的值 */
    req.nh.nlmsg_len    = NLMSG_LENGTH(sizeof(struct ifinfomsg)); 
	/* 根据`man 7 netlink`中NLM_F_REQUEST的解释，
	    所有请求类型的消息都要设置NLM_F_REQUEST；
	    设置了NLM_F_DUMP就能够获取link状态，
	    NLM_F_DUMP相当于NLM_F_ROOT|NLM_F_MATCH，
	    NLM_F_MATCH是"Not implemented yet"，但去掉NLM_F_DUMP，
	    只设置NLM_F_MATCH也是有效果的。 */
    req.nh.nlmsg_flags   = NLM_F_REQUEST | NLM_F_DUMP;  
	/* `man 7 rtnetlink`中说RTM_GETLINK用于
	   "get information about a specific network interface"，
	   "These messages contain an ifinfomsg structure followed by
	    a series of rtattr structures"
	    这句话解释了RTM_GETLINK的消息结构 */
    req.nh.nlmsg_type  	  = RTM_GETLINK;
	/* 根据`man 7 rtnetlink`中ifinfomsg结构体的定义，恒为AF_UNSPEC */
    req.ifimsg.ifi_family = AF_UNSPEC; 
	/* http://man7.org/linux/man-pages/man7/rtnetlink.7.html
	   中有"ifi_index is the unique interface index 
	   (since Linux 3.7, it is possible to feed a nonzero value with 
	   the RTM_NEWLINK message, thus creating a link with the given ifindex)"
	   这句话，说明这个成员设置0没问题 */
    req.ifimsg.ifi_index  = 0; 
	/* `man 7 rtnetlink`
	"ifi_change is reserved for future use and should be always set to 0xFFFFFFFF" */
    req.ifimsg.ifi_change = 0xFFFFFFFF; 
    if (-1 == send(sfd, &req, req.nh.nlmsg_len, 0)) {
        PRINTF("ERROR! send: %s\n", strerror(errno));
        ret = -1;
    }
 
    return ret;
}
 
static int msg_loop(struct msghdr *pmh)
{
    int ret = 0;
    ssize_t nread = -1;
    char *buf = (char *)(pmh->msg_iov->iov_base);
    struct nlmsghdr *nh;
    struct ifinfomsg *ifimsg;
    struct rtattr *rta;
    int attrlen;
 
    /* 循环体参考`man 7 netlink`中reading netlink message那个EXAMPLE */
    while (!ret) {
        nread = recvmsg(sfd, pmh, 0);
        if (-1 == nread) {
            PRINTF("ERROR! recvmsg: %s\n", strerror(errno));
            ret = -1;
        }
		/*  man 7 netlink
		    "Netlink messages consist of a byte stream 
		    with one or multiple nlmsghdr headers and associated payload"
		    这句话的周围解释了netlink消息的结构，
		    所以消息的开头是nlmsghdr类型的header 
			man 3 netlink，NLMSG_OK用来检查收到的netlink消息是否OK 
			man 3 netlink，
			NLMSG_NEXT用于定位到下一个nlmsghdr header的开头，
			注意这个宏会改变nread的值 
			man 3 netlink 
			"The caller must check if the current nlmsghdr 
			didn't have the NLMSG_DONE set—this function 
			doesn't return NULL on end"，所以要检查NLMSG_DONE 
			The end of multipart message. */
        for (nh = (struct nlmsghdr *)buf; !ret && NLMSG_OK(nh, nread); 
			nh = NLMSG_NEXT(nh, nread)) { 
				
			/* man 3 netlink
			"The caller must check if the current nlmsghdr didn't have the NLMSG_DONE 
			set—this function doesn't return NULL on end" */
			/* The end of multipart message. */
            if (NLMSG_DONE == nh->nlmsg_type) {
                break;
            }
 
            if (NLMSG_ERROR == nh->nlmsg_type) {
                /* Do some error handling. */
                PRINTF("ERROR! NLMSG_ERROR\n");
                ret = -1;
            }
 
            /*  Continue with parsing payload. 
			  	man 7 rtnetlink
				"These messages contain an ifinfomsg structure followed 
				by a series of rtattr structures"
				这句话解释了RTM_NEWLINK和RTM_DELLINK的消息结构
				前一句说明了RTM_NEWLINK和RTM_DELLINK分别用于
				"Create"和"remove""information about a specific network interface"，
				link的"Create"和"remove"动作产生的消息是程序要解析 */
            if (!ret && (RTM_NEWLINK == nh->nlmsg_type || RTM_DELLINK == nh->nlmsg_type)) { 
				/* NLMSG_DATA用于定位nlmsghdr header后面的payload的内存地址 */
                ifimsg = (struct ifinfomsg *)NLMSG_DATA(nh); 
				/* 排除loopback网卡适配器 */
                if (ARPHRD_LOOPBACK != ifimsg->ifi_type) { 
                    /* man 7 rtnetlink
                       "These messages contain an ifinfomsg structure followed 
                       by a series of rtattr structures"
                       说明ifinfomsg之后的内容就是属性 */
                       /* NLMSG_LENGTH返回的值是nlmsghdr和ifinfomsg加起来的长度，
					   减去后，剩下的就是属性的长度 */
                    attrlen = nh->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifinfomsg)); 
					
					/* IFLA_RTA用于定位ifinfomsg payload后面的属性的内存地址 */
					/* 根据`man 3 rtnetlink`，RTA_OK用来检查rta指向的属性是否有效 */
					/* 根据`man 3 rtnetlink`，RTA_NEXT用于定位到下一个属性的开头， 
					   注意这个宏会改变attrlen的值 */
                    for (rta = IFLA_RTA(ifimsg); 
                            RTA_OK(rta, attrlen) && rta->rta_type <= IFLA_MAX; 
                            rta = RTA_NEXT(rta, attrlen)) { 
                        /* 当rta_type为IFLA_IFNAME时，属性包含的数据是网卡名字符串 */
                        if (IFLA_IFNAME == rta->rta_type) { 
								/* 打印网卡名，如eth0 */
                                printf("%s: ", (char*)RTA_DATA(rta)); 
                        }
                    }
                    /* 实际上，当link up时，IFF_RUNNING和IFF_LOWER_UP都会被设置，
                       link down时都会被清除，以IFF_LOWER_UP作为判断依据也行，
                       但根据`man 7 netdevice`，IFF_LOWER_UP是从内核2.6.17版本才有的，
                       所以使用IFF_RUNNING兼容性可能好点 */
                    if (IFF_RUNNING & ifimsg->ifi_flags)
                        printf("link up\n");
                    else
                        printf("link down\n");
                }
            }
        }
    }
 
    return ret;
}
 
static void sig_handler(int sig)
{
    exit(deinit());
}

