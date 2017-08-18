#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>


#define NETLINK_TEST 17
#define MSG_LEN 100
static inline void *nlmsg_data(const struct nlmsghdr *nlh)
{
	return (unsigned char *) nlh + NLMSG_HDRLEN;
}

struct msg_to_kernel
{
    struct nlmsghdr hdr;
    char data[MSG_LEN];
};
struct u_packet_info
{
    struct nlmsghdr hdr;
    struct 	tcmsg 	tcm;
};

typedef enum op_code{
	READ,
	WRITE
}op_code;

int main(int argc, char* argv[]) 
{
	//初始化
	struct sockaddr_nl 		local;
	struct sockaddr_nl 		kpeer;
	int 					skfd;
	int						ret;
	int 					kpeerlen  		 = sizeof(struct sockaddr_nl);
	struct nlmsghdr			*message   		 = (struct nlmsghdr *)malloc(1000);
	skfd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TEST);
	if(skfd < 0){
		printf("can not create a netlink socket\n");
		return -1;
	}

	memset(&local, 0, sizeof(local));
	local.nl_family  = AF_NETLINK;
	local.nl_pid	 = getpid();
	local.nl_groups  = 0;
	if(bind(skfd, (struct sockaddr *)&local, sizeof(local)) != 0){
		printf("bind() error\n");
		return -1;
	}

	memset(&kpeer, 0, sizeof(kpeer));
	/*kpeer.nl_pid 用户空间发往内核空间，必须为0 */
	kpeer.nl_family = AF_NETLINK;
	kpeer.nl_pid 	= 0;
	kpeer.nl_groups = 0;

	iov.iov_base 		 = (void *)message;
	
	memset(message, 0, sizeof(struct nlmsghdr));
	message->nlmsg_flags = 0;
	message->nlmsg_seq   = 0;
	message->nlmsg_len   = NLMSG_SPACE(strlen("hello"));
	message->nlmsg_pid   = local.nl_pid;
	data				 = NLMSG_DATA(message);
	memcpy(data, "hello", strlen("hello"));
	iov.iov_len			 = NLMSG_SPACE(strlen("hello"));
	
	memset(&msg, 0, sizeof(msg));
	msg.msg_name 	= (void *)&kpeer;
	msg.msg_namelen = sizeof(kpeer);
	msg.msg_iov     = &iov;
	msg.msg_iovlen	= 1;

	
	ret = sendmsg(skfd, &msg, 0);
	if(ret < 0){
		perror("fail to send: ");
		return ret;
	}
	printf("send %d bit data to kenel\n", ret);
	//接受内核态确认信息

	ret = recvmsg(skfd, &msg, 0);
	if(ret <=0 ){
		if(ret == 0){
			printf("message end !!!\n");
		}
		else{
			perror("recv form kerner:");
			return -1;
		}
	}
	else{
		printf("message receive %d bit from kernel\n", ret);
		struct nlmsghdr *hdr = (struct nlmsghdr *)msg.msg_iov;
		struct 	tcmsg * tcm = NLMSG_DATA(hdr);
		printf("nlmsg_pid =%d \n", hdr->nlmsg_pid);
		printf("nlmsg_len =%d \n", hdr->nlmsg_len);
		printf("tcm->tcm_ifindex =%d \n", tcm->tcm_ifindex);
		printf("tcm->tcm_handle =%d \n", tcm->tcm_parent);
		printf("tcm->tcm_info =%d \n", tcm->tcm_info);
	}
	
	//内核和用户进行通信

	free(message);
	close(skfd);
	return 0;
}

