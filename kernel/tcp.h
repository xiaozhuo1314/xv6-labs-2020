#define TCP_MIN_DATA_OFF 5 // tcp数据偏移字段最小几个4字节,也就是tcp头部长度最小几个4字节;tcp头部最小20字节,data offset占4个比特位,故tcp头部长度为[20,60]

struct tcp {
  uint16 sport; // 源端口
  uint16 dport; // 目的端口
  uint seq;     // 序号
  uint acknum;  // 确认号
  uint reserved:4, // 保留位的前4位
       doff:4;     // 数据偏移
  uint fin:1,
       syn:1,
       rst:1,
       psh:1,
       ack:1,
       urg:1,
       ece:1,
       cwr:1; // ece和cwr是保留位的后两位,且cwr的位数比ece高
  uint16 winsize; // 窗口大小
  uint16 checksum; // 校验和
  uint16 urgptr; // 紧急指针,如果urg标志被设置,那么这个指针就有值
};

/* tcp状态 */
enum tcpstate {
  TCP_CLOSED = 1,
	TCP_LISTEN,
	TCP_SYN_RECV,
	TCP_SYN_SENT,
	TCP_ESTABLISHED,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_CLOSING,
	TCP_TIME_WAIT,
	TCP_MAX_STATE
};