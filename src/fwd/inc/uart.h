#ifndef _LGW_UART_H_ 
#define _LGW_UART_H_ 

int uart_open(const char* path); 

/* baude: B9600 B19200 B38400 B115200 , default:115200 
 * flow: 0 -> no hw flow,  1 -> hw flow,  2 -> sw flow, default: no sw&hw
 * bits: default 8bit(CS8)
 * parity: 0-> parenb & inpck , 1 -> parenb & cstopb , 2 -> parenb & parodd & inpck & istrip , 3 -> parenb & inpck & istrip & ~parodd
 * stop:   0 -> ~cstopb,  1 -> cstopb 
 */
int uart_config(int fd, int baude, int c_flow, int bits, int parity, int stop);

/* read timout_ms default 1s
 *
 */
int uart_read(int fd, char* r_buf, int lenth, int timeout_ms);

int uart_send(int fd, char* data, int len);

int uart_close(int fd);

#endif
