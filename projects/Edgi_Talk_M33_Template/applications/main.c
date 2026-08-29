#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

int main(void)
{
    rt_kprintf("Hello RT-Thread\r\n");
    rt_kprintf("This core is cortex-m33, used to boot cortex-m55\r\n");
    rt_kprintf("M33/M55 shared SOCMEM: 0x261f0000-0x261fffff "
               "(64 KiB)\r\n");
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}
