#include <linux/types.h>    
#include <asm/types.h>    
#include <inttypes.h>    
#include <sys/file.h>    
#include <sys/user.h>    
#include <sys/socket.h>    
#include <linux/netlink.h>    
#include <linux/rtnetlink.h>    
#include <linux/if.h>    
#include <unistd.h>    
#include <stdlib.h>    
#include <string.h>    
#include <stdio.h>    
#include <stdbool.h>    
#include <errno.h>    
  
  
#include <arpa/inet.h>  
typedef uint32_t u32;    
typedef uint16_t u16;    
    
struct nlsock {    
    int sock;    
    int seq;    
    struct sockaddr_nl snl;    
    char *name;    
} nl_cmd = { -1, 0, {0}, "netlink-cmd" };    
    
static int index_oif = 0;    
struct nl_if_info {    
    u32 addr;    
    char *name;    
};    
static int nl_socket ( struct nlsock *nl, unsigned long groups )    
{    
    int ret;    
    struct sockaddr_nl snl;    
    int sock;    
    int namelen;    
      
    /*创建netlink套接字*/  
    sock = socket ( AF_NETLINK, SOCK_RAW, NETLINK_ROUTE );    
    if ( sock < 0 ) {    
        fprintf ( stderr,  "Can't open %s socket: %s", nl->name,    
                strerror ( errno ) );    
        return -1;    
    }    
    
    ret = fcntl ( sock, F_SETFL, O_NONBLOCK );    
    if ( ret < 0 ) {    
        fprintf ( stderr,  "Can't set %s socket flags: %s", nl->name,    
                strerror ( errno ) );    
        close ( sock );    
        return -1;    
    }    
      
    /*设置地址信息*/  
    memset ( &snl, 0, sizeof snl );    
    snl.nl_family = AF_NETLINK;    
    snl.nl_groups = groups;    
    
    /* Bind the socket to the netlink structure for anything. */    
    ret = bind ( sock, ( struct sockaddr * ) &snl, sizeof snl );    
    if ( ret < 0 ) {    
        fprintf ( stderr,  "Can't bind %s socket to group 0x%x: %s",    
                nl->name, snl.nl_groups, strerror ( errno ) );    
        close ( sock );    
        return -1;    
    }    
    
    /* multiple netlink sockets will have different nl_pid */    
    namelen = sizeof snl;    
    ret = getsockname ( sock, ( struct sockaddr * ) &snl, &namelen );    
    if ( ret < 0 || namelen != sizeof snl ) {    
        fprintf ( stderr,  "Can't get %s socket name: %s", nl->name,    
                strerror ( errno ) );    
        close ( sock );    
        return -1;    
    }    
    
    nl->snl = snl;    
    nl->sock = sock;    
    return ret;    
}    
/* 
*   发送请求 
*/  
static int nl_request ( int family, int type, struct nlsock *nl )    
{    
    int ret;    
    struct sockaddr_nl snl;    
    
    struct {    
        struct nlmsghdr nlh;    
        struct rtgenmsg g;    
    } req;    
    
    
    /* Check netlink socket. */    
    if ( nl->sock < 0 ) {    
        fprintf ( stderr, "%s socket isn't active.", nl->name );    
        return -1;    
    }    
    
    memset ( &snl, 0, sizeof snl );    
    snl.nl_family = AF_NETLINK;    
    
    req.nlh.nlmsg_len = sizeof req;                                     //  
      
    req.nlh.nlmsg_type = type;                                          //设置自定义消息类型  
      
    req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;     //消息的附加选项  
      
    req.nlh.nlmsg_pid = 0;                                              //设置发送者的pid  
      
    req.nlh.nlmsg_seq = ++nl->seq;                                   //  
      
    req.g.rtgen_family = family;                                        //  
    
    ret = sendto ( nl->sock, ( void* ) &req, sizeof req, 0,    
            ( struct sockaddr* ) &snl, sizeof snl );    
    if ( ret < 0 ) {    
        fprintf ( stderr, "%s sendto failed: %s", nl->name, strerror ( errno ) );    
        return -1;    
    }    
    return 0;    
}    
    
/* Receive message from netlink interface and pass those information  
   to the given function. */    
static int    
nl_parse_info ( int ( *filter ) ( struct sockaddr_nl *, struct nlmsghdr *, void * ),    
        struct nlsock *nl, void *arg )    
{    
    int status;    
    int ret = 0;    
    int error;    
      
    /*接受队列里的所有信息，每次接受4096，并解析*/  
    while ( 1 ) {    
        char buf[4096];    
        struct iovec iov = { buf, sizeof buf };  //netlink消息头  
        struct sockaddr_nl snl;    
        struct msghdr msg = { ( void* ) &snl, sizeof snl, &iov, 1, NULL, 0, 0};  //设置消息头  
        struct nlmsghdr *h;  //netlink消息头  
    
        status = recvmsg ( nl->sock, &msg, 0 );    
    
        if ( status < 0 ) {    
            if ( errno == EINTR )    
                continue;    
            if ( errno == EWOULDBLOCK || errno == EAGAIN )    
                break;    
            fprintf ( stderr, "%s recvmsg overrun", nl->name );    
            continue;    
        }    
        /*如果不是内核的消息，忽略*/  
        if ( snl.nl_pid != 0 ) {    
            fprintf ( stderr, "Ignoring non kernel message from pid %u",    
                    snl.nl_pid );    
            continue;    
        }    
        /*转出错处理*/  
        if ( status == 0 ) {    
            fprintf ( stderr, "%s EOF", nl->name );    
            return -1;    
        }    
        /*如果socket地址长度不对，转出错处理*/  
        if ( msg.msg_namelen != sizeof snl ) {    
            fprintf ( stderr, "%s sender address length error: length %d",    
                    nl->name, msg.msg_namelen );    
            return -1;    
        }    
    
        for ( h = ( struct nlmsghdr * ) buf; NLMSG_OK ( h, status );  //枚举缓冲中的netlink消息  
                h = NLMSG_NEXT ( h, status ) ) {    
            /* Finish of reading. */    
            if ( h->nlmsg_type == NLMSG_DONE )  //消息已读取完毕  
                return ret;    
    
            /* Error handling. */    
            if ( h->nlmsg_type == NLMSG_ERROR ) {  //消息读取出错  
                struct nlmsgerr *err = ( struct nlmsgerr * ) NLMSG_DATA ( h );    
    
                /* If the error field is zero, then this is an ACK */    
                if ( err->error == 0 ) {    
                    /* return if not a multipart message, otherwise continue */    
                    if ( ! ( h->nlmsg_flags & NLM_F_MULTI ) ) {    
                        return 0;    
                    }    
                    continue;    
                }    
    
                if ( h->nlmsg_len < NLMSG_LENGTH ( sizeof ( struct nlmsgerr ) ) ) {    
                    fprintf ( stderr, "%s error: message truncated",    
                            nl->name );    
                    return -1;    
                }    
                fprintf ( stderr, "%s error: %s, type=%u, seq=%u, pid=%d",    
                        nl->name, strerror ( -err->error ),    
                        err->msg.nlmsg_type, err->msg.nlmsg_seq,    
                        err->msg.nlmsg_pid );    
                /*  
                ret = -1;  
                continue;  
                */    
                return -1;    
            }    
    
    
            /* skip unsolicited messages originating from command socket */    
            if ( nl != &nl_cmd && h->nlmsg_pid == nl_cmd.snl.nl_pid ) {    
                continue;    
            }    
              
            /*读取一条netlink消息，就解析得到的消息*/  
            error = ( *filter ) ( &snl, h, arg );    
            if ( error < 0 ) {    
                fprintf ( stderr, "%s filter function error/n", nl->name );    
                ret = error;    
            }    
        }    
    
        /* After error care. */    
        if ( msg.msg_flags & MSG_TRUNC ) {    
            fprintf ( stderr, "%s error: message truncated", nl->name );    
            continue;    
        }    
        if ( status ) {    
            fprintf ( stderr, "%s error: data remnant size %d", nl->name,    
                    status );    
            return -1;    
        }    
    }    
    return ret;    
}    
    
static void nl_parse_rtattr ( struct rtattr **tb, int max, struct rtattr *rta, int len )    
{    
    while ( RTA_OK ( rta, len ) ) {    
        if ( rta->rta_type <= max )    
            tb[rta->rta_type] = rta;    
        rta = RTA_NEXT ( rta,len );    
    }    
}    
    
static int nl_get_oif ( struct sockaddr_nl *snl, struct nlmsghdr *h, void *arg )    
{    
    int len;    
    struct rtmsg *rtm;    
    struct rtattr *tb [RTA_MAX + 1];    
    u_char flags = 0;    
    
    char anyaddr[16] = {0};    
    
    int index;    
    int table;    
    void *dest;    
    void *gate;    
    
    rtm = NLMSG_DATA ( h );   //得到消息数据部分  
    
    if ( h->nlmsg_type != RTM_NEWROUTE )    
        return 0;    
    if ( rtm->rtm_type != RTN_UNICAST )    
        return 0;    
    
    table = rtm->rtm_table;    
    
    len = h->nlmsg_len - NLMSG_LENGTH ( sizeof ( struct rtmsg ) );    
    if ( len < 0 )    
        return -1;    
    
    memset ( tb, 0, sizeof tb );    
    nl_parse_rtattr ( tb, RTA_MAX, RTM_RTA ( rtm ), len );    
    
    if ( rtm->rtm_flags & RTM_F_CLONED )    
        return 0;    
    if ( rtm->rtm_protocol == RTPROT_REDIRECT )    
        return 0;    
    if ( rtm->rtm_protocol == RTPROT_KERNEL )    
        return 0;    
    
    if ( rtm->rtm_src_len != 0 )    
        return 0;    
    
     
    
    // 这里可以对所有路由进行识别  
    
     
    
    // 取得out interface index    
    
    if ( tb[RTA_OIF] ) {    
        index = * ( int * ) RTA_DATA ( tb[RTA_OIF] );    
    }    
    
    if ( tb[RTA_DST] )    
        dest = RTA_DATA ( tb[RTA_DST] );    
    else    
        dest = anyaddr;    
    
    /* Multipath treatment is needed. */    
    if ( tb[RTA_GATEWAY] )    
        gate = RTA_DATA ( tb[RTA_GATEWAY] );    
    
     
    
    // 判断是否为默认路由  
    
    if ( dest == anyaddr && gate ) {    
        if ( arg != NULL ) {    
            * ( int * ) arg = index;    
            return 0;    
        }    
    }    
    return 0;    
}    
    
static int nl_get_if_addr ( struct sockaddr_nl *snl, struct nlmsghdr *h, void *arg )    
{    
    int len;    
    struct ifaddrmsg *ifa;    
    struct rtattr *tb [IFA_MAX + 1];    
    void *addr = NULL;    
    void *broad = NULL;    
    u_char flags = 0;    
    char *label = NULL;    
    struct in_addr ifa_addr, ifa_local;    
    char ifa_label[IFNAMSIZ + 1];    
     
    ifa = NLMSG_DATA ( h );  //得到消息数据部分  
    if ( ifa->ifa_family != AF_INET )    
     return 0;    
         
    if ( h->nlmsg_type != RTM_NEWADDR && h->nlmsg_type != RTM_DELADDR )    
        return 0;    
    
    len = h->nlmsg_len - NLMSG_LENGTH ( sizeof ( struct ifaddrmsg ) );    
    if ( len < 0 )    
        return -1;    
    
    memset ( tb, 0, sizeof tb );    
    
    nl_parse_rtattr ( tb, IFA_MAX, IFA_RTA ( ifa ), len );    
        
    if (tb[IFA_ADDRESS] == NULL)    
    tb[IFA_ADDRESS] = tb[IFA_LOCAL];    
  
    /*网卡地址*/  
    if ( tb[IFA_ADDRESS] )    
     ifa_addr = *(struct in_addr*) RTA_DATA ( tb[IFA_ADDRESS] );    
         
    if ( tb[IFA_LOCAL] )    
     ifa_local = *(struct in_addr *) RTA_DATA ( tb[IFA_LOCAL] );    
  
     /*网卡名*/  
    if ( tb[IFA_LABEL] )    
     strncpy( ifa_label, RTA_DATA ( tb[IFA_LABEL] ), IFNAMSIZ );     
         
    
    // 打印所有地址信息  
    printf( "addr=%s    loal=%s name=%s\n",     
     inet_ntoa(ifa_addr),    
     inet_ntoa(ifa_local),    
     ifa_label );    
    return 0;    
}    
    
int main()    
{    
    int ret;    
    char if_name[PAGE_SIZE]={0};    
    int err;    
    struct nl_if_info if_info = { -1, "eth0" };    
      
    /*初始化nl_cmd*/  
    ret = nl_socket ( &nl_cmd, 0 );    
    if ( ret < 0 ) {    
        return ret;    
    }    
      
      
    ret = nl_request ( AF_INET, RTM_GETROUTE, &nl_cmd );  //获取路由信息  
    if ( ret < 0 ) {    
        return ret;    
    }    
  
    ret = nl_parse_info ( nl_get_oif, &nl_cmd, &index_oif );    
    if ( ret < 0 )    
        return ret;    
      
    printf ( "oif=%08x  ", index_oif );    
    if ( index_oif > 0 ) {    
     err = if_indextoname( index_oif, if_name );    
     if (err ) {    
         printf ( "interface=%s\n", if_name );    
     }    
   }    
     
     
    ret = nl_request ( AF_INET, RTM_GETADDR, &nl_cmd );  //获取地址信息  
    if ( ret < 0 )    
        return ret;    
     
    ret = nl_parse_info ( nl_get_if_addr, &nl_cmd, &if_info );    
    if ( ret < 0 )    
        return ret;    
        
    
    return 0;    
}    

