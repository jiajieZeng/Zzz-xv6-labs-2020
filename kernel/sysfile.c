//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}


int                                                                                             
lazyallocation(uint64 va)
{   
    struct proc *p = myproc();
    struct vma *vma;
    struct file *fptr;
    struct inode *iptr;
    int found = 0;
    char *mem; 
    for (int i = 0; i < NVMA; i++) {
       vma = &p->vmaslot[i];
       // printf("%d-->begin:%d, length:%d, prot:%d, flags:%d, offset:%d, mapped:%d\n",i, vma->begin, vma->length, vma->prot, vma->flags, vma->offset, vma->mapped); 
       if (vma->mapped && va >= vma->begin && va < vma->begin + vma->length) {
          found = 1;
          break;
       }
    }
    if (found == 0) {
       return -1;
    }
    if ((mem = kalloc()) == 0) {
       return -1;
    }   
    memset(mem, 0, PGSIZE);
    
    //atomic
    begin_op();
    fptr = (struct file*)vma->f;
    iptr = (struct inode*)fptr->ip;
    ilock(iptr);
    uint offset = vma->offset + PGROUNDDOWN(va - vma->begin);
    readi(iptr, 0, (uint64)mem, offset , PGSIZE);
    iunlock(iptr);
    end_op();
    int pteflag = PTE_U;
    if (vma->prot & PROT_READ) {
       pteflag |= PTE_R;
    }
    if (vma->prot & PROT_WRITE) {
        pteflag |= PTE_W;
    }
    if (vma->prot & PROT_EXEC) {
        pteflag |= PTE_X;
    }
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, pteflag) != 0) {
       kfree(mem);
       return -1;
    }
    return 0;
}


uint64
sys_mmap(void)
{
    uint64 length, offset, top;
    int prot, flags, fd;
    struct file *f;
    struct proc *p;
    struct vma *vma = 0, *tvma;
    if (argaddr(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
            argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0) {
        return -1;
    }
    if (length == 0) {
        return -1;
    }
    if ((f->readable == 0 && (prot & (PROT_READ))) || (f->writable == 0 && (prot & (PROT_WRITE)) && (flags & (MAP_PRIVATE)) == 0)) {
        return -1;
    }
    p = myproc();
    top = TRAPFRAME;
    int found = 0;
    for (int i = 0; i < NVMA; i++) {
        tvma = &p->vmaslot[i];
        if (tvma->mapped == 1) {
            if (tvma->begin < top) {
                top = tvma->begin;
                top = PGROUNDDOWN(top);
            }
        } else if (tvma->mapped == 0) {
            if (!found) {
                found = 1;
                vma = tvma;
                vma->mapped = 1;
            }
       // printf("%d-->begin:%d, length:%d, prot:%d, flags:%d, offset:%d, mapped:%d\n",i, vma->begin, vma->length, vma->prot, vma->flags, vma->offset, vma->mapped); 
        }
    }
    if (found == 0 && vma == 0) {
        return -1;
    }  
    length = PGROUNDUP(length);
    vma->begin = top - length;
    vma->length = length;
    vma->prot = prot;
    vma->flags = flags;
    vma->offset = offset;
    vma->f = f;
    filedup(f);
    
    return vma->begin;
}

int
fileuvmunmap(pagetable_t pagetable, uint64 va, uint64 bytes, struct vma* vma)
{
    pte_t *pte;
    struct file* f;
    for (uint64 i = va; i < va + bytes; i += PGSIZE) {
       if ((pte = walk(pagetable, i, 0)) == 0) {
          return -1;
       }
       if ((*pte) & PTE_V) {
          if (((*pte) & PTE_D) && (vma->flags & MAP_SHARED)) {
             // write back
             f = vma->f;
             uint64 writeoff = i - vma->begin;
             if (writeoff < 0) {
                filewrite(f, i, PGSIZE + writeoff);
             } else if (writeoff + PGSIZE > vma->length) {
                filewrite(f, i, vma->length - writeoff);
             } else {
                filewrite(f, i, PGSIZE);
             }
          }
       }                                                                                        
       uvmunmap(pagetable, i, 1, 1);
       *pte = 0;
    }
    memset(vma, 0, sizeof(struct vma));
    return 1;
}


uint64
sys_munmap(void)
{
    struct proc *p = myproc();
    uint64 va, length, tva;
    struct vma *vma = 0, *tvma;
    if (argaddr(0, &va) < 0 || argaddr(1, &length) < 0) {
        return -1;
    }
    for (int i = 0; i < NVMA; i++) {
        tvma = &p->vmaslot[i];
        if (tvma->mapped && va >= tvma->begin && va < tvma->begin + tvma->length) {
            vma = tvma;
            break;
        }
    }
    if (vma == 0) {
        return -1;
    }
    // n munmap call might cover only a portion of an mmap-ed region, but you can assume that it will either unmap at the start, or at the end, or the whole region (but not punch a hole in the middle of a region). 
    if (va > vma->begin && va + length < vma->begin + vma->length) {
        return -1;
    }
    tva = va;
    if (va > vma->begin) {
        tva = PGROUNDDOWN(tva);
    }
    int n = length - (tva - va);
    if (n < 0) {
        return -1;
    }
    if (fileuvmunmap(p->pagetable, tva, (uint64)n, vma) < 0) {
        return -1;
    }
    if (va <= vma->begin && va + length > vma->begin) {
        uint64 dif = va + length - vma->begin;
        vma->begin = va + length;
        vma->offset = vma->offset + dif;
    }
    vma->length -= length;
    if (vma->length <= 0) {
        fileclose(vma->f);
        vma->mapped = 0;
    }
    return 0;
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;


  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
