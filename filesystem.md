# Maximum file size
Inodes contain address to the disk blocks that hold's the inode's content. To increase the number of blocks a file can hold, we use indirection blocks. 
A 512 byte block can contain 128 block pointers. Therefore, a one level indirection block can increase the maximum file size by 128blocks. Using the same logic, a double indirection block adds an extra (128x128) blocks, and a triple indirection blocks adds a (128x128x128) blocks. 

**File Block Structure**
- 12 direct blocks: 12 blocks
- 1 indirection block: 128 blocks
- 1 double indirection block: (128x128) blocks
- 1 triple indirection block: (128x128x128) blocks

```
struct inode{
  ...
  uint addrs[NDIRECT+3];
}
```

**bmap**  
bmap finds the bn-th block allocated for an inode. If the block is not allocated yet, bmap will allocate on demand. We must make sure the bmap API integrates the new file block structure. Below is the code when accessing or allocating a block that is mapped in the triple indirection block. We do the same process for double indirection as well. 

```
if(bn < NTINDIRECT){
    // Loading Triple Indirection Block
    if((addr = ip->addrs[NDIRECT + 2]) == 0){
      ip->addrs[NDIRECT + 2] = addr = balloc(ip->dev);
    }

    // Loading Double indirection Block
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn / (128*128)]) == 0){
      a[bn / (128*128)] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    // Loading Indirection Block
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[(bn % (128*128))/ 128]) == 0){
      a[(bn % (128*128)) / 128] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    // Loading Block Address
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[(bn % (128*128)) % 128]) == 0){
      a[(bn % (128*128)) % 128] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
```

**itrunc**  
itrunc is the API for deallocating all the blocks held by an inode. We make sure all the blocks pointed to by the double indirection and triple indirection blocks are also considered and properly deallocated. Below is the process of deallocating a triple indirection block. We need a three level nested loop.

```
 // Deallocate 128*128*128 triple indirect blocks.
  if(ip->addrs[NDIRECT + 2]){
    // bp, a: Triple Indirection Block
    bp = bread(ip->dev, ip->addrs[NDIRECT + 2]);
    a = (uint*)bp->data;

    for(j = 0; j < 128; j++){
      if(a[j]){
        // bp2, a2: Double Indirection Block
        bp2 = bread(ip->dev, a[j]);
        a2 = (uint*)bp2->data;
  
        for(k=0; k < 128; k++){
          if(a2[k]){
            // bp3, a3: Indirection Block
            bp3 = bread(ip->dev, a2[k]);
            a3 = (uint*)bp3->data;
            
            for(l=0; l<128; l++){
              if(a3[l])
                bfree(ip->dev, a3[l]);
            }

            brelse(bp3);

            // Deallocate Indirection Block
            bfree(ip->dev, a2[k]);
          }
        }

        brelse(bp2);

        // Deallocate Double Indirection Block
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);

    // Deallocate Triple Indirection Block.
    bfree(ip->dev, ip->addrs[NDIRECT + 2]);
    ip->addrs[NDIRECT + 2] = 0;
  }
```

### Large File Read/Write Test Results
**test_largefile**
- source: test_largefile.c
- create test: creates a buffer of 16*1024*1024bytes and writes the data to a created file. 
- read test: reads from the file created in create test. 
- stress test: creates 16MB file -> reads file -> removes file => repeat 4 times

![image](uploads/thread/image7.png)

# Pread, Pwrite
While read and write system calls reference the offset value of the file descriptor and starts reading/writing from there, pread and pwrite takes in the offset from users and does not update or reference the offset values of the file. 
- sys_pread  
  : transforms integer value fd into the file structure object and calls into the actual system call routine, pos_read.
- int pos_read(struct file* f, char* addr, int n, int off)  
  : Use the readi routine to read from "off", into buffer "addr". Does not update the file's (f's) offset field.
- sys_pwrite  
  : Also gets the file structure and calls pos_write.
- int pos_write(struct file* f, char* addr, int n, int off)  
  : Use the writei routine to write at "off". If the write size is too large, divide into mutiple smaller writes to avoid overwhelming the log. 

### Pread, Pwrite Test Results
**test_prw**
- source: test_prw.c
- pwrite test  
  : Creates a new file calles "prw_testfile" and writes 0~9 repeatedly. Then, at a predefined offset of 10, we write a message using pwrite. "Good morning. I am DY." will be writted at offset 10 of the file.
  ```
  src = "Good morning. I am DY.\n";
  n = strlen(src);
  off = 10;
  
  if((w = pwrite(fd, src, n, off)) < 0){
    printf(1, "pwrite error\n");
    return -1;
  }
  ```
- pread test  
  : Reads the file "prw_testfile" and reads from the offset 10 using pread. 

  ```
  off = 10;
  printf(1, "src_string_len:%d\n", src_string_len);
  if((r = pread(fd, buff, n, off)) < 0){
    printf(1, "pread error\n");
    return -1;
  }

  printf(1, "Retrieved string: %s", buff);
  ```
