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
int send_to_kernel(int skfd, struct sockaddr_nl * kpeer, 
	op_code type, struct nlmsghdr* message, void* data)
{
	int      ret 		= -1; 
	message->nlmsg_len  = NLMSG_SPACE(strlen(data));
	message->nlmsg_type = type;
	memset(NLMSG_DATA(message), 0, strlen(data) + 1);
	memcpy(NLMSG_DATA(message), data, strlen(data));
	ret = sendto(skfd, message, message->nlmsg_len, 0 
		  		 ,(struct sockaddr *)kpeer, sizeof(kpeer));
	if(!ret){
		perror("send pid:");
		return -1;
	}
	printf("message sendto kernel are:%s, len:%d\n", 
			(char *)NLMSG_DATA(message), message->nlmsg_len);
	return 0;
}
int main(int argc, char* argv[]) 
{
	//初始化
	struct sockaddr_nl 		local;
	struct sockaddr_nl 		kpeer;
	int 					skfd;
	int						ret;
	int 					kpeerlen  		 = sizeof(struct sockaddr_nl);
	struct nlmsghdr			*message   		 = (struct nlmsghdr *)malloc(1000);
	char					*info			 = (char *)malloc(1000);
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

	memset(message, 0, sizeof(struct nlmsghdr));
	message->nlmsg_flags = 0;
	message->nlmsg_seq   = 0;
	message->nlmsg_pid   = local.nl_pid;

	send_to_kernel(skfd, &kpeer, READ, message, "going!!!");

	//接受内核态确认信息

	ret = recvfrom(skfd, info, 100, 
					0, (struct sockaddr*)&kpeer, &kpeerlen);
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
		struct nlmsghdr *hdr = (struct nlmsghdr *)info;
		struct 	tcmsg * tcm = NLMSG_DATA(hdr);
		printf("nlmsg_pid =%d \n", hdr->nlmsg_pid);
		printf("nlmsg_len =%d \n", hdr->nlmsg_len);
		printf("tcm->tcm_ifindex =%d \n", tcm->tcm_ifindex);
		printf("tcm->tcm_handle =%d \n", tcm->tcm_parent);
		printf("tcm->tcm_info =%d \n", tcm->tcm_info);
	}
	
	//内核和用户进行通信

	free(message);
	free(info);
	close(skfd);
	return 0;
}

