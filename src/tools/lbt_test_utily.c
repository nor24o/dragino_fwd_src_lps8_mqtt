#include <string.h>
#include <unistd.h>
#include "logger.h"
#include "uart.h"

/* --- PUBLIC DECLARATION ---------------------------------------- */
uint8_t LOG_INFO = 1;
uint8_t LOG_PKT = 1;
uint8_t LOG_WARNING = 1;
uint8_t LOG_ERROR = 1;
uint8_t LOG_REPORT = 1;
uint8_t LOG_JIT = 0;
uint8_t LOG_JIT_ERROR = 0;
uint8_t LOG_BEACON = 0;
uint8_t LOG_DEBUG = 0;
uint8_t LOG_TIMERSYNC = 0;
uint8_t LOG_MEM = 0;

int main(int argc, char* argv[])
{
    int fd, ret, i=10;
    char buffer[32] = {'\0'};

    printf("LBT test utility start...\r\n");

    if (argc < 3) { 
        printf("Need argument, tty_path and tty command\n");
        printf("Usage: %s tty_path command\r\n", argv[0]);
        return -1;
    }

    if ((fd = uart_open(argv[1])) < 0) {
        printf("open %s is failed\n",  argv[1]);
    } else {
        printf("open %s is success\n", argv[1]);
        uart_config(fd, 115200, 9, 9, 9, 9); 

        while (i--) {
            ret = uart_send(fd, argv[2], strlen(argv[2]));
            if (ret < 0)
                printf("write failed\n");
            else 
                printf("wr_static is %d\n", ret);

            ret = uart_read(fd, buffer, sizeof(buffer), 1000); 

            if (ret > 0)  
                printf("read uart buffer: %s \n", buffer);
            else
                printf("read uart error: %d \n", ret);
            
            sleep(6);
        }
    }
    uart_close(fd);
}
