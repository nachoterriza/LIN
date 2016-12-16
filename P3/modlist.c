#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <asm-generic/uaccess.h>
#include <linux/list_sort.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gestion de listas enlazadas y proc fs - LIN");
MODULE_AUTHOR("Ignacio Terriza Díez - Juan Jesús Martos Escribano");

#define BUFFER_LENGTH       PAGE_SIZE
#define MAX_WRITING_SIZE    100
#define MAX_COMMAND_SIZE    8 //Commands are "add", "cleanup", and "remove"

static struct proc_dir_entry *proc_entry;

LIST_HEAD(my_list);

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

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	int available_space = BUFFER_LENGTH-1;
	char buff_kern[MAX_WRITING_SIZE];
	char command[MAX_COMMAND_SIZE];
	int data;
	//int scanned;
  
	if ((*off) > 0) /* The application can write in this entry just once !! */
		return 0;
  
    if (len > available_space)
    {
    	printk(KERN_INFO "Modlist: not enough space!!\n");
    	return -ENOSPC;
    }

    /* Transfer data from user to kernel space */
  	if (copy_from_user(buff_kern,buf,len))  
    		return -EFAULT;

    buff_kern[len]='\0'; /* Add the `\0' */  
	*off+=len;            /* Update the file pointer */

    if(sscanf(buff_kern,"add %i",&data)==1)
	add_item_list(&my_list,data);
    else if(sscanf(buff_kern,"remove %i",&data)==1)
	remove_item_list(&my_list,data);
    else if(sscanf(buff_kern,"cleanup")==0)
	cleanup_list(&my_list);
    else
    {
    	printk(KERN_INFO "Modlist: invalid argument!!\n");
    	return -EINVAL;
    }

    return len;
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
	list_item_t *item = NULL;
	struct list_head *cur_node = NULL;
	char buff_kern[MAX_WRITING_SIZE];
	char *dest = buff_kern;

    if((*off) > 0) /* Tell the application that there is nothing left to read */
    	return 0;


    spin_lock(&sp);
    list_for_each(cur_node,&my_list)
    {
    	item = list_entry(cur_node,list_item_t,links);
    	dest += sprintf(dest,"%i\n",item->data);
    }
    spin_unlock(&sp);

      /* Transfer data from the kernel to userspace */  
	if (copy_to_user(buf, buff_kern, dest-buff_kern))
    	return -EINVAL;
    
  	(*off)+=(dest-buff_kern);  /* Update the file pointer */

	return (dest-buff_kern);


}


static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
};

int init_modlist_module(void)
{
	int ret = 0;


	proc_entry = proc_create("modlist",0666,NULL,&proc_entry_fops);
	if (proc_entry == NULL) {
      ret = -ENOMEM;
      printk(KERN_INFO "Modlist: Can't create /proc entry\n");
    } else {
      printk(KERN_INFO "Modlist: Module loaded\n");
    }

    return ret;

}

void exit_modlist_module(void)
{
	cleanup_list(&my_list);
	remove_proc_entry("modlist", NULL);
	printk(KERN_INFO "Modlist: Module unloaded.\n");
}

module_init(init_modlist_module);
module_exit(exit_modlist_module);
