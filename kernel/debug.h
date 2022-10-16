/* colour macro from https://github.com/whileskies/xv6-riscv-ext/blob/main/kernel/debug.h */
#define red(str) "\e[01;31m" str "\e\[0m"
#define green(str) "\e[01;32m" str "\e\[0m"
#define yellow(str) "\e[01;33m" str "\e\[0m"
#define purple(str) "\e[01;35m" str "\e\[0m"
#define grey(str) "\e[01;30m" str "\e\[0m"
#define cambrigeblue(str) "\e[01;36m" str "\e\[0m"
#define navyblue(str) "\e[01;34m" str "\e\[0m"

#define dbg(fmt, args...) printf(fmt, ##args)

/* 链路层eth debug */
#define eth_dbg(fmt, args...) dbg(yellow(fmt), ##args)
/* 网络层ip debug */
#define ip_dbg(fmt, args...) dbg(purple(fmt), ##args)
/* 传输层tcp debug */
#define tcp_dbg(fmt, args...) dbg(cambrigeblue(fmt), ##args)
/* 传输层udp debug */
#define udp_dbg(fmt, args...) dbg(grey(fmt), ##args)
/* 待定 */