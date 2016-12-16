#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/errno.h>

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0
#define LED_NUM_1_ON 0x4
#define LED_CAPS_2_ON 0x2
#define LED_SCROLL_3_ON 0x1

struct tty_driver* kbd_driver= NULL;

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
   printk(KERN_INFO "ledctl: loading\n");
   printk(KERN_INFO "ledctl: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}


SYSCALL_DEFINE1(ledctl,unsigned int,leds)
{
    unsigned int led_state = 0;

    kbd_driver = get_kbd_driver_handler();

    if(kbd_driver==NULL) {
      printk(KERN_INFO "Error: el valor del driver del teclado es nulo.");
      return -ENOTCONN;
    }

    if(leds & LED_SCROLL_3_ON) {
      led_state |= LED_SCROLL_3_ON;
    }
    if(leds & LED_NUM_1_ON) {
      led_state |= LED_CAPS_2_ON;
    }
    if(leds & LED_CAPS_2_ON) {
      led_state |= LED_NUM_1_ON;
    }

    if(set_leds(kbd_driver,led_state)==-ENOIOCTLCMD) {
      printk(KERN_INFO "Error: set leds no puede cambiar los leds.");
      return -ENOIOCTLCMD;
    }

    return 0;
}

