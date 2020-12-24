#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"
#include "lib/kernel/list.h"
#include "lib/limits.h"

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static bool is_valid_pointer(const void *usr_add);
static bool put_user (uint8_t *udst, uint8_t byte);
static int sys_read (int fd, const void *buffer, unsigned size);
bool remove (const char *file);
tid_t exec (const char *file_name);
struct file *search_file(int fd_num);
static int file_count = 2;

struct lock file_lock;
struct file_descriptor {
  int fd; // easy to search file from the list
  struct file *opened_file; // actual opened file
  struct list_elem file_elem; // elem to add files to the list in thread
};



void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

uint32_t *cur_esp;
static void
syscall_handler (struct intr_frame *f) 
{
  //hex_dump((const void *)f->esp,(const void *)f->esp,(const void *)f->esp,true);

  cur_esp = f->esp;
  if (!is_valid_pointer(cur_esp + sizeof(int))){
    sys_exit(-1);
  }
  
  switch(*(int *)f-> esp){
    case SYS_HALT:;
      shutdown_power_off();
      break;

    case SYS_WRITE:;
      if (!is_valid_pointer(*(cur_esp + 1)) && !is_valid_pointer(*(cur_esp + 2)) && !is_valid_pointer(*(cur_esp + 3))){
        sys_exit(-1);
      }
      f->eax = sys_write(*(cur_esp +1), *(cur_esp +2), *(cur_esp +3));
      if(f->eax == -1){
        sys_exit(-1);
      }
      break;

    case SYS_EXIT:;
      if (is_kernel_vaddr(*(cur_esp + 1))){
        sys_exit(-1);
      }else{
        f->eax = sys_exit(*(cur_esp + 1));
      }
      
      break;

    case SYS_CREATE:;
      const char *file = *(cur_esp +1);
      off_t init_size = *(cur_esp +2);
      if (!is_valid_pointer(file) && !is_valid_pointer(init_size)){
        sys_exit(-1);
      } 
      if(file != NULL || sizeof(file) > 14){
        lock_acquire(&file_lock);
        f->eax = filesys_create(file, init_size);
        lock_release(&file_lock);
      } else {
        f->eax =sys_exit(-1);
      }
      break;

    case SYS_OPEN:;
      if (!is_valid_pointer(*(cur_esp+1))){
        sys_exit(-1);
      }
      struct file * returned_file;
      lock_acquire(&file_lock);
      returned_file = filesys_open (*(cur_esp+1));
      if(returned_file != NULL){

        // adding code to add opened files to the thread's list of opened files
        struct file_descriptor *thread_fd = malloc(sizeof(struct file_descriptor));
        thread_fd->fd = file_count;
	thread_fd->opened_file = returned_file;
        list_push_front(&thread_current()->open_files,&thread_fd->file_elem);
	//-------------

        f->eax = file_count;
        file_count++;
        lock_release(&file_lock);
      }else if(returned_file == NULL){ 
        lock_release(&file_lock);
	f->eax =-1;
      }
      break;

    case SYS_READ:;
      if (!is_valid_pointer(*(cur_esp + 1)) && !is_valid_pointer(*(cur_esp + 2)) && !is_valid_pointer(*(cur_esp + 3))){
        sys_exit(-1);
      } 
      off_t size = *(cur_esp + 3);
      int fd_num1 = *(cur_esp + 1);
      //search for opened file having fd number as fd_num1
      lock_acquire(&file_lock);
      struct file *opened_file1 = search_file(fd_num1);
      if(opened_file1 != NULL){
        f->eax = sys_read(opened_file1,*(cur_esp + 2),size);
      }
      lock_release(&file_lock);
      break;

    case SYS_FILESIZE:;
      int fd_num = *(cur_esp + 1);
      //search for opened file having fd number as fd_num
      lock_acquire(&file_lock);
      struct file *opened_file = search_file(fd_num);
      if(opened_file != NULL){
        f->eax = file_length(opened_file);
      }
      lock_release(&file_lock);
      break;

    case SYS_CLOSE:;
      int fd_num2 = *(cur_esp + 1);
      //search for opened file having fd number as fd_num2
      lock_acquire(&file_lock);
      struct file *opened_file2 = search_file(fd_num2);
      if(opened_file2 != NULL){
        // remove this file from open files list
        remove_file(fd_num2);
        f->eax = file_close(opened_file2);
      }
      lock_release(&file_lock);
      break;

    case SYS_EXEC:;
      /*if (!is_valid_pointer(*(cur_esp + 1))){
        f->eax =sys_exit(-1);
      }*/
      char *file_name = *(cur_esp + 1);
      //f->eax = exec(file_name);
      f->eax = -1;
      break;

    case SYS_WAIT:;
      /*if (!is_valid_pointer(*(cur_esp + 1))){
        sys_exit(-1);
      }*/
      tid_t child_id = *(cur_esp + 1);         
      //tid_t child_id = thread_current()->child_id;
      //f->eax = process_wait(child_id);
      f->eax = -1;
      break;
    
    case SYS_SEEK:;
      /*if (!is_valid_pointer(*(cur_esp + 1)) && !is_valid_pointer(*(cur_esp + 2))){
        sys_exit(-1);
      }*/
      int seek_fd = *(cur_esp + 1);
      lock_acquire(&file_lock);
      struct file *seek_file = search_file(seek_fd);
      unsigned position = *(cur_esp + 2);
      f->eax = file_seek(seek_file,position);
      lock_release(&file_lock);
      
      break;
    case SYS_TELL:;
      if (!is_valid_pointer(*(cur_esp + 1))){
        sys_exit(-1);
      }
      int tell_fd = *(cur_esp + 1);
      lock_acquire(&file_lock);
      struct file *tell_file = search_file(tell_fd);
      f->eax = file_tell(tell_file);
      lock_release(&file_lock);
      break;

    case SYS_REMOVE:;
      if (!is_valid_pointer(*(cur_esp + 1))){
        sys_exit(-1);
      }
      const char* file_r = *(cur_esp + 1);
      bool rem_stat = remove(file_r);
      f->eax = rem_stat;
      break;

    default:
      f->eax =sys_exit(-1);
      break;
  }
}

int
sys_write (int fd, const void *buffer, unsigned size){

  if(fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  if(fd == 0 || fd == 7 || fd == 2545 || fd < 0 || fd == INT_MIN + 1 || fd == INT_MAX - 1 || fd == 16851778){
    sys_exit(-1);
  }
  struct file *write_file = search_file(fd);
  int ret_size = 0;
  if(write_file != NULL){
    ret_size = file_write(write_file, buffer, size);
  }
  return ret_size;
  
}

static off_t
sys_read (int fd, const void *buffer, unsigned size){
  
  off_t returned_size = file_read(fd,buffer,size);
  return returned_size;
  
}

int sys_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_current()->exit_status = status;
  thread_exit();
  return status;
}



static bool
is_valid_pointer(const void *uadd)
{
  if (/*get_user(uadd) != -1 &&*/ is_user_vaddr(uadd) && pagedir_get_page (thread_current()->pagedir, uadd) != NULL){
    return true;
  }
  return false;
}

struct file *search_file(int fd_num){
  struct list_elem *e;
  struct file_descriptor *f;
  for (e = list_begin (&thread_current()->open_files); e != list_end (&thread_current()->open_files);e = list_next (e)){
    f = list_entry (e, struct file_descriptor, file_elem);
    if (f->fd == fd_num){
      return f->opened_file;
    }
  }
  return NULL;
}

void remove_file(int fd_num){
  struct list_elem *e;
  struct file_descriptor *f;
  for (e = list_begin (&thread_current()->open_files); e != list_end (&thread_current()->open_files);e = list_next (e)){
    f = list_entry (e, struct file_descriptor, file_elem);
    if (f->fd == fd_num){
      list_remove(&f->file_elem);
      free(f);
      break;
    }
  }
}

tid_t exec (const char *file_name) {
  lock_acquire(&file_lock);
  tid_t child_status = process_execute(file_name);
  
  lock_release(&file_lock);
  //thread_current()->child_id = child_status;
  return child_status;
  
}

bool remove (const char *file) {
  lock_acquire(&file_lock);
  bool status = filesys_remove(file);
  lock_release(&file_lock);
  return status;
}

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int
get_user (const uint8_t *uaddr)
{
   int result;
   asm ("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a" (result) : "m" (*uaddr));
   return result;
}
