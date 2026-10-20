/********************************** (C) COPYRIGHT *******************************
 * File Name          : lwipopts.h
 * Description        : LWIP 配置（gateway 项目独立配置，不共享 SDK）
 *
 * 本文件位于 main/gateway/，由 CMake include 路径优先于 SDK 的
 * sdk/CH592_DEV/lwip/port/lwipopts.h，实现项目级独立裁剪。
 *
 * 设计目标：RAW API + TCP/UDP，无 socket / 无 DNS / 无 DHCP / 无 ICMP / 无 RAW。
 * 所有内存尽量静态，减少 RAM 占用。
 *******************************************************************************/

#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ---------------------------------------------------------------------------
 * 基础
 * ------------------------------------------------------------------------- */
/* LWIP_NO_SYS=1 → 裸机（NO_SYS=1）；否则使用 FreeRTOS。
 * 由构建系统（CMake 的 LWIP_NOSYS 选项）定义。 */
#ifndef LWIP_NO_SYS
#define LWIP_NO_SYS                 0
#endif

#if LWIP_NO_SYS
#define NO_SYS                      1      /* 裸机模式 */
#define SYS_LIGHTWEIGHT_PROT        0
#else
#define NO_SYS                      0      /* 使用 FreeRTOS（需 sys_arch） */
#define SYS_LIGHTWEIGHT_PROT        1      /* 使用 sys_arch_protect */
#endif
#define LWIP_NETCONN                0      /* 不用 netconn（用 raw API） */
#define LWIP_SOCKET                 0      /* 不用 socket */
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (1 * 1024)   /* 堆：1KB（须 >= TCP_SND_BUF） */
#define MEMP_OVERFLOW_CHECK         0
#define MEMP_SANITY_CHECK           0

/* ---------------------------------------------------------------------------
 * 内部内存池（memp）
 * ------------------------------------------------------------------------- */
#define MEMP_NUM_PBUF               4
#define MEMP_NUM_RAW_PCB            1
#define MEMP_NUM_UDP_PCB            1
#define MEMP_NUM_TCP_PCB            2
#define MEMP_NUM_TCP_PCB_LISTEN     1
#define MEMP_NUM_TCP_SEG            8
#define MEMP_NUM_REASSDATA          0      /* 不支持 IP 分片重组（省 RAM） */
#define MEMP_NUM_FRAG_PBUF          0
#define MEMP_NUM_ARP_QUEUE          1      /* 仅 1 个待解析 ARP 请求（省 RAM） */
#define MEMP_NUM_IGMP_GROUP         0
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL)

/* ---------------------------------------------------------------------------
 * PBUF
 * ------------------------------------------------------------------------- */
#define PBUF_POOL_SIZE              4      /* 池：4 个 */
#define PBUF_POOL_BUFSIZE           514    /* = TCP_MSS(460)+IP(20)+TCP(20)+Link(14)，容纳完整帧 */
#define LWIP_SUPPORT_CUSTOM_PBUF    1

/* ---------------------------------------------------------------------------
 * IP / ICMP / UDP / TCP
 * ------------------------------------------------------------------------- */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
/* 协议裁剪：由 CMake 的 GW_LWIP_ICMP / GW_LWIP_RAW / GW_LWIP_UDP 控制
 * （CH592 默认全开；CH591 默认全关） */
#ifndef GW_LWIP_ICMP
#  define GW_LWIP_ICMP  1
#endif
#ifndef GW_LWIP_RAW
#  define GW_LWIP_RAW   1
#endif
#ifndef GW_LWIP_UDP
#  define GW_LWIP_UDP   1
#endif
#define LWIP_ICMP                   GW_LWIP_ICMP   /* ping / ICMP 错误响应 */
#define LWIP_RAW                    GW_LWIP_RAW    /* raw API */
#define LWIP_UDP                    GW_LWIP_UDP    /* UDP（DHCP 服务器需要） */
#define LWIP_TCP                    1
#define LWIP_DNS                    0      /* 不用 DNS */
#define LWIP_DHCP                   0      /* 静态 IP */

#define IP_REASSEMBLY               0      /* 关闭分片重组（省 RAM） */
#define IP_FRAG                     0      /* 关闭分片发送 */
#define IP_OPTIONS_ALLOWED          0
#define IP_DEFAULT_TTL              64

#define TCP_MSS                     460    /* 匹配 RNDIS_MTU=500（MTU-40） */
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_SNDQUEUELOWAT           (TCP_SND_QUEUELEN / 2)
#define TCP_QUEUE_OOSEQ             0      /* 关闭乱序队列（省 RAM） */
#define MEMP_NUM_TCP_SEG            8
#define LWIP_TCP_SACK_OUT           0
#define LWIP_WND_SCALE              0
#define TCP_LISTEN_BACKLOG          1

/* 关闭 TCP sanity 检查（我们的窗口/池配置为省 RAM 做了权衡） */
#define LWIP_DISABLE_TCP_SANITY_CHECKS 1

/* ---------------------------------------------------------------------------
 * 网络接口
 * ------------------------------------------------------------------------- */
#define LWIP_NETIF_HOSTNAME         0
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_TX_SINGLE_PBUF   1      /* RNDIS 需连续发送 */
#define LWIP_NETIF_API              0
#define LWIP_NETIF_LOOPBACK         0
#define LWIP_HAVE_LOOPIF            0

/* ---------------------------------------------------------------------------
 * 协议扩展
 * ------------------------------------------------------------------------- */
#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              2      /* 仅 PC + 网关，2 项足够（省 RAM） */
#define LWIP_ETHERNET               1
#define LWIP_IGMP                   0
#define LWIP_BROADCAST_PING         0      /* ICMP 已关，无广播 ping */
#define LWIP_MULTICAST_PING         0

/* ---------------------------------------------------------------------------
 * 统计 / 调试（关闭以省空间）
 * ------------------------------------------------------------------------- */
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0
#define LWIP_DBG_MIN_LEVEL          0

/* ---------------------------------------------------------------------------
 * 线程 / 超时
 * ------------------------------------------------------------------------- */
#define LWIP_TIMERS                 1
#define LWIP_TCPIP_CORE_LOCKING     0
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#if !LWIP_NO_SYS
#define TCPIP_THREAD_NAME           "tcpip"
#define TCPIP_THREAD_STACKSIZE      512
#define TCPIP_THREAD_PRIO           (tskIDLE_PRIORITY + 2)
#define TCPIP_MBOX_SIZE             4
#define DEFAULT_UDP_RECVMBOX_SIZE   4
#define DEFAULT_TCP_RECVMBOX_SIZE   4
#define DEFAULT_ACCEPTMBOX_SIZE     4
#endif

/* ---------------------------------------------------------------------------
 * 校验和
 * ------------------------------------------------------------------------- */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

#endif /* LWIP_LWIPOPTS_H */
