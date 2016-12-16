#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/ftrace.h>
#include <linux/semaphore.h>
#include "cbuffer.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Fifo management in proc_fs");
MODULE_AUTHOR("");

#define BUFFER_LENGTH		50

cbuffer_t* cbuffer; /* Buffer circular */
int prod_count = 0; /* Número de procesos que abrieron la entrada
/proc para escritura (productores) */
int cons_count = 0; /* Número de procesos que abrieron la entrada
/proc para lectura (consumidores) */
struct semaphore mtx; /* para garantizar Exclusión Mutua */
struct semaphore sem_prod; /* cola de espera para productor(es) */
struct semaphore sem_cons; /* cola de espera para consumidor(es) */
int nr_prod_waiting=0; /* Número de procesos productores esperando */
int nr_cons_waiting=0; /* Número de procesos consumidores esperando */
static struct proc_dir_entry *proc_entry;

/* Se invoca al hacer open() de entrada /proc */
static int fifoproc_open(struct inode *inode, struct file *file)
{
	if(down_interruptible(&mtx))
		return EINTR;

	if(file->f_mode & FMODE_READ)
	{
		cons_count++;

		up(&sem_prod);
		nr_prod_waiting--;

		while(!prod_count)
		{
			nr_cons_waiting++;
			up(&mtx);
			if(down_interruptible(&sem_cons))
				return -EINTR;
		}
	}else{
		prod_count++;

		up(&sem_cons);
		nr_cons_waiting--;

		while(!cons_count)
		{
			nr_prod_waiting++;
			up(&mtx);
			if(down_interruptible(&sem_prod))
				return -EINTR;
		}
	}
	up(&mtx);
	return 0;
}

/* Se invoca al hacer close() de la entrada /proc */
static int fifoproc_release(struct inode *inode, struct file *file)
{
	if(down_interruptible(&mtx))
		return -EINTR;

	if(file->f_mode & FMODE_READ)
	{
		cons_count--;

		if(nr_prod_waiting>0)
		{
			up(&sem_prod);
			nr_prod_waiting--;
		}
	}else{
		prod_count--;

		if(nr_cons_waiting>0)
		{
			up(&sem_cons);
			nr_cons_waiting--;
		}
	}

	if(!(cons_count || prod_count))
		clear_cbuffer_t(cbuffer);
	
	up(&mtx);
	return 0;
}

/* Se invoca al hacer read() de la entrada /proc */
static ssize_t fifoproc_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	char kbuf[BUFFER_LENGTH];

	if(len>BUFFER_LENGTH)
		return -ENOSPC;

	if(down_interruptible(&mtx))
		return -EINTR;

	while(size_cbuffer_t(cbuffer)<len && prod_count>0)
	{
		nr_cons_waiting++;
		up(&mtx);
		if(down_interruptible(&sem_cons))
			return -EINTR;
	}

	if(!(size_cbuffer_t(cbuffer) || prod_count))
	{
		up(&mtx);
		return 0;
	}

	remove_items_cbuffer_t(cbuffer,kbuf,len);

	if(nr_prod_waiting>0)
	{
		up(&sem_prod);
		nr_prod_waiting--;
	}

	up(&mtx);

	if(copy_to_user(buf,kbuf,len))
		return -EFAULT;

	return len;
}

/* Se invoca al hacer write de la entrada /proc */
static ssize_t fifoproc_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	char kbuf[BUFFER_LENGTH];

	if(len>BUFFER_LENGTH)
		return -ENOSPC;

	if(copy_from_user(kbuf,buf,len))
		return -EFAULT;

	if(down_interruptible(&mtx))
		return -EINTR;
	
	/* Esperar hasta que haya hueco para insertar (debe haber consumidores) */
	while(nr_gaps_cbuffer_t(cbuffer)<len && cons_count>0)
	{
		nr_prod_waiting++;
		up(&mtx);
		if(down_interruptible(&sem_prod))
			return -EINTR;
	}

	/* Detectar fin de comunicación por error (consumidor cierra FIFO antes) */
	if(!cons_count)
	{
		up(&mtx);
		return -EPIPE;
	}

	insert_items_cbuffer_t(cbuffer,kbuf,len);

	/* Despertar a posible consumidor bloqueado */
	if(nr_cons_waiting>0)
	{
		up(&sem_cons);
		nr_cons_waiting--;
	}
	up(&mtx);
	return len;
}

static const struct file_operations proc_entry_fops = {
	.read = fifoproc_read,
	.write = fifoproc_write,
	.open = fifoproc_open,
	.release = fifoproc_release,
};

int init_fifoproc_module( void )
{

	/* Inicializacion a 1 del semáforo que permite acceso en exclusión mutua a la SC */
	sema_init(&mtx,1);
	/* Inicialización a 0 de los semáforos usados como colas de espera */
	sema_init(&sem_prod,0);
	sema_init(&sem_cons,0);


	/* Inicializacion del buffer */
	cbuffer = create_cbuffer_t(BUFFER_LENGTH);

	proc_entry = proc_create("fifoproc",0666, NULL, &proc_entry_fops);

	if(proc_entry==NULL)
	{
		destroy_cbuffer_t(cbuffer);
		printk(KERN_INFO "fifoproc: No puedo crear la entrada en proc\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "fifoproc: Cargado el Modulo.\n");

	return 0;
}

void exit_fifoproc_module( void )
{
	remove_proc_entry("fifoproc", NULL);
	destroy_cbuffer_t(cbuffer);
	printk(KERN_INFO "fifoproc: Modulo descargado.\n");
}

module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );
