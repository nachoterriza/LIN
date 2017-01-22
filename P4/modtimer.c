#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include "cbuffer.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gestion de listas, cbuffer, workqueues... - LIN");
MODULE_AUTHOR("Ignacio Terriza Díez - Juan Jesús Martos Escribano");

#define BUFFER_LENGTH   50 
#define CBUFFER_LENGTH  40

unsigned long timer_period_ms = 500;
int emergency_threshold = 80;
unsigned int max_random = 300;

struct timer_list my_timer; /* Structure that describes the kernel timer */

/* Work descriptor */
struct work_struct my_work;

unsigned long int flags;
struct semaphore mtx; /* para garantizar Exclusión Mutua */   
struct semaphore sem_cons; /* cola de espera para consumidor(es) */
int nr_cons_waiting=0; /* Número de procesos consumidores esperando */

cbuffer_t* cbuffer;	/* Buffer circular */

LIST_HEAD(my_list);

static struct proc_dir_entry *proc_entry_modtimer, *proc_entry_modconfig;

DEFINE_SPINLOCK(sp);

typedef struct
{
	int data;
	struct list_head links;
}list_item_t;

/*
	Functions for list management

*/

void add_item_list(struct list_head *list, int data)
{
	list_item_t *item = vmalloc(sizeof(list_item_t));
	item->data = data;
	spin_lock(&sp);
	list_add_tail(&(item->links),list);
	spin_unlock(&sp);
}

void remove_item_list(struct list_head *list, int data)
{
	list_item_t *pos, *n = NULL;
	spin_lock(&sp);
	list_for_each_entry_safe(pos,n,list,links)
	{
		if(pos->data == data){
			list_del(&(pos->links));
			vfree(pos);
		}
	}
	spin_unlock(&sp);
}


void cleanup_list(struct list_head *list)
{
	list_item_t *pos, *n = NULL;

	spin_lock(&sp);
	list_for_each_entry_safe(pos,n,list,links)
	{
		list_del(&(pos->links));
		vfree(pos);
	}
	spin_unlock(&sp);
}

static int copy_items_into_list(struct work_struct *work) {

	int size;
	unsigned int t=0;
	unsigned int nums[BUFFER_LENGTH];

	spin_lock_irqsave(&sp,flags);
	size = size_cbuffer_t(cbuffer)/4;
	remove_items_cbuffer_t(cbuffer, (char *) &nums[0], size*4);
	
	spin_unlock_irqrestore(&sp,flags);

	for(;t<size;t++)
		add_item_list(&my_list,nums[t]);

	printk(KERN_INFO "%i elements moved from the buffer to the list\n", size);

	up(&sem_cons);

	up(&mtx);

	return 0;
}

/* Function invoked when timer expires (fires) */
static void fire_timer(unsigned long data)
{
	unsigned int rand = get_random_int() % max_random;
	int size,cpu;

	printk(KERN_INFO "Modtimer: Generated number %u\n",rand);

	spin_lock_irqsave(&sp,flags);
	insert_items_cbuffer_t(cbuffer,(char*)&rand,4); /* Unsigned int = 4 bytes */
	size = size_cbuffer_t(cbuffer);
	spin_unlock_irqrestore(&sp,flags);

	if(size >= emergency_threshold*CBUFFER_LENGTH/100 && !work_pending(&my_work)) {
		cpu = smp_processor_id();

		if(cpu % 2)
			schedule_work_on(cpu--,&my_work);
		else
			schedule_work_on(cpu++,&my_work);

	}

	mod_timer( (&my_timer), jiffies+timer_period_ms*HZ/1000);
}

static ssize_t modtimer_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	list_item_t *pos, *n = NULL;
	char kbuf[BUFFER_LENGTH];
	char *dest = kbuf;
	int ret = 0;

	if(down_interruptible(&mtx))
		return -EINTR;

	while(list_empty(&my_list)) {
		up(&mtx);

		if(down_interruptible(&sem_cons))
			return -EINTR;
		if(down_interruptible(&mtx))
			return -EINTR;
	}

	up(&mtx);

	if(down_interruptible(&mtx))
		return -EINTR;

	list_for_each_entry_safe(pos,n,&my_list,links){
		dest+=sprintf(dest,"%i\n",pos->data);
		list_del(&(pos->links));
		vfree(pos);
	}

	up(&mtx);

	ret = dest-kbuf;

	if(copy_to_user(buf,kbuf,ret))
		return -EINTR;

	(*off) += ret;

	return ret;

}

static int modtimer_open(struct inode *inode, struct file *file)
{
	if(down_interruptible(&mtx))
		return -EINTR;

	if(nr_cons_waiting) {
		up(&mtx);
		return -EUSERS;
	}

	nr_cons_waiting++;
	up(&mtx);

	/* Initialize field */
	my_timer.data=0;
	my_timer.function=fire_timer;
	my_timer.expires=jiffies + HZ;  /* Activate it one second from now */
	/* Activate the timer for the first time */
	add_timer(&my_timer); 

	/* Increment reference counter */
	try_module_get(THIS_MODULE);
	
	return 0;
}

static int modtimer_release(struct inode *inode, struct file *file)
{
	
	/* Wait until completion of the timer function (if it's currently running) and delete timer */
	del_timer_sync(&my_timer);

	/* Wait until all jobs scheduled so far have finished */
	flush_scheduled_work();

	/* Clean up cbuffer */
	spin_lock_irqsave(&sp,flags);
	clear_cbuffer_t(cbuffer);
	spin_unlock_irqrestore(&sp,flags);

	/* Clean up linked list (free memory) */
	cleanup_list(&my_list);

	/* Decrement reference counter */
	module_put(THIS_MODULE);

	/* Decrement consumer number */
	nr_cons_waiting--;

	return 0;
}

static const struct file_operations modtimer_fops = {
	.read = modtimer_read,
	.open = modtimer_open,
	.release = modtimer_release,
};

static ssize_t modconfig_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	char kbuf[BUFFER_LENGTH];
	int buf_length;

	if ((*off) > 0){ /* Tell the application that there is nothing left to read */
		return 0;
	}

	buf_length = sprintf(kbuf,"timer_period_ms %lu\nemergency_threshold %i\nmax_random %u\n",timer_period_ms,emergency_threshold,max_random);

	if(copy_to_user(buf,kbuf,buf_length))
		return -EINVAL;

	(*off) += buf_length; /* Update the file pointer */

	return buf_length;
}

static ssize_t modconfig_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[BUFFER_LENGTH];
	unsigned long timer;
	int threshold;
	unsigned int max;

	if ((*off) > 0){ /* Tell the application that there is nothing left to read */
		return 0;
	}

	if(copy_from_user(kbuf,buf,len))
		return -EINVAL;

	kbuf[len]='\0'; /* Add the `\0' */  
	*off+=len;            /* Update the file pointer */


	if(sscanf(kbuf,"timer_period_ms %lu",&timer)==1) {
		timer_period_ms=timer;
		printk(KERN_INFO "Modtimer: timer_period_ms changed. New value: %lu\n", timer_period_ms);
	}

	if(sscanf(kbuf,"emergency_threshold %i",&threshold)==1) {
		if(threshold>=0 && threshold<=100) {
			emergency_threshold=threshold;
			printk(KERN_INFO "Modtimer: emergency_threshold changed. New value: %i\n", emergency_threshold);
		} else {
			printk(KERN_INFO "Modtimer: incorrect value for emergency value");
		}
	}

	if(sscanf(kbuf,"max_random %ui",&max)==1) {
		max_random=max;
		printk(KERN_INFO "Modtimer: max_random changed. New value: %u\n", max_random);
	}

	return len;

}

static const struct file_operations modconfig_fops = {
	.read = modconfig_read,
	.write = modconfig_write,
};

int init_modtimer_module( void ) {

	cbuffer=create_cbuffer_t(CBUFFER_LENGTH);

	proc_entry_modtimer=proc_create("modtimer",0666,NULL,&modtimer_fops);
	proc_entry_modconfig=proc_create("modconfig",0666,NULL,&modconfig_fops);

	if(proc_entry_modtimer==NULL){
		destroy_cbuffer_t(cbuffer);
		printk(KERN_INFO "Modtimer: Can't initialize /proc/modtimer entry\n");
		return -ENOMEM;
	}
	
	if(proc_entry_modconfig==NULL){
		destroy_cbuffer_t(cbuffer);
		printk(KERN_INFO "Modtimer: Can't initialize /proc/modconfig entry\n");
		return -ENOMEM;
	}
	
    /* Initialize work structure (with function) */
	INIT_WORK(&my_work,copy_items_into_list);

	/* Inicializacion a 1 del semáforo que permite acceso en exclusión mutua a la SC */
	sema_init(&mtx,1);

	/* Inicialización a 0 del semáforo usado como cola de espera */
	sema_init(&sem_cons,0);

	/* Create timer */
	init_timer(&my_timer);

	printk(KERN_INFO "Modtimer: Module loaded\n");

	return 0;
}

void exit_modtimer_module( void ) {
	remove_proc_entry("modtimer",NULL);
	remove_proc_entry("modconfig",NULL);

	destroy_cbuffer_t(cbuffer);

	cleanup_list(&my_list);

	printk(KERN_INFO "Modtimer: module unloaded\n");
}

module_init( init_modtimer_module );
module_exit( exit_modtimer_module );
