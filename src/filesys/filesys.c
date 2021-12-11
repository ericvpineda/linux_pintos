#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Project 3 helpers */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp);
struct inode* check_path(const char* path, int want_parent, char desired_name[NAME_MAX + 1],
                         struct dir** result_dir);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();
  cache_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  cache_flush();
  free_map_close();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size, int isdir) {
  block_sector_t inode_sector = 0;
  bool success = false;

  char name_part[NAME_MAX + 1];

  // dir should be the dir in which we want to create file/dir called name (name's parent)
  struct dir* parent_dir = NULL;

  // inode should be the inode of the file/dir we want to create (so it should be null)
  struct inode* inode = check_path(name, 1, name_part, &parent_dir);

  // file/dir already exists, so error
  if (inode != NULL) {
    return false;
  }

  success = (parent_dir != NULL && free_map_allocate(1, &inode_sector) &&
             inode_create(inode_sector, initial_size, isdir) &&
             dir_add(parent_dir, name_part, inode_sector));

  // if we want to create a directory, we must also call dir_create
  if (isdir) {
    success = success && dir_create(inode_sector, 0, parent_dir->inode->sector);
  }

  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);

  // if we successfully created file/dir, increment files_rem in parent inode
  if (success) {
    struct inode* parent_inode = dir_get_inode(parent_dir);
    struct inode_disk* id = get_inode_disk(parent_inode);
    id->files_rem += 1;
    cache_write(fs_device, parent_inode->sector, id);
    free(id);
  }

  dir_close(parent_dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* name) {

  char filename[NAME_MAX + 1];

  // dir should be the dir of the file we want to open
  struct dir* dir = NULL;

  // inode should be the inode of the file we want to open
  struct inode* inode = check_path(name, 0, filename, &dir);

  // if file does not exist or inode is a dir, error
  if (inode == NULL || inode_isdir(inode)) {
    dir_close(dir);
    return NULL;
  }

  // close dir
  dir_close(dir);

  return file_open(inode);
}

/* Opens the directory with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no directory named NAME exists,
   or if an internal memory allocation fails. */
struct dir* filesys_open_dir(const char* name) {

  char filename[NAME_MAX + 1];

  // dir should be the dir we want to open
  struct dir* dir = NULL;

  // inode should be the inode of the dir we want to open
  struct inode* inode = check_path(name, 0, filename, &dir);

  // if dir does not exist or inode is a dir, error
  if (inode == NULL || !inode_isdir(inode)) {
    dir_close(dir);
    return NULL;
  }

  // alternate solution: not sure if we still have a bug since we always return a dir
  // dir_close(dir);
  // return dir_open(inode);

  return dir;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {
  char name_part[NAME_MAX + 1];

  // dir should be parent dir that contains file/dir called name
  struct dir* parent_dir = NULL;

  // inode should be the inode we want to remove
  struct inode* inode = check_path(name, 1, name_part, &parent_dir);
  if (parent_dir == NULL) {
    return false;
  }

  struct inode_disk* parent_id = get_inode_disk(parent_dir->inode);

  // only proceed with removal if file/dir exists
  if (inode != NULL) {

    // If IT IS A DIRECTORY
    if (inode_isdir(inode)) {
      struct inode_disk* curr_id = get_inode_disk(inode);

      // ensure dir contains no files and has no reliances
      if (curr_id->files_rem == 0 && inode->open_cnt == 1) {
        inode->open_cnt -= 1;

        // decrement parent file count and save back to disk
        parent_id->files_rem -= 1;
        cache_write(fs_device, parent_dir->inode->sector, parent_id);
        free(parent_id);

        // remove dir from parent dir
        bool success = dir_remove(parent_dir, name_part);
        dir_close(parent_dir); // not sure about closing this
        free(curr_id);
        return success;
      }
    }

    // IF IT IS A FILE
    else {
      // decrement parent file count and save back to disk
      parent_id->files_rem -= 1;
      cache_write(fs_device, parent_dir->inode->sector, parent_id);
      free(parent_id);

      bool success = dir_remove(parent_dir, name_part);
      dir_close(parent_dir); // not sure about closing this
      return success;
    }
  }

  dir_close(parent_dir);
  return false;
}

/* Syscall for change directory (chdir) */
bool filesys_chdir(const char* dir_name) {

  bool success = false;

  char name_part[NAME_MAX + 1];
  struct process* pcb = thread_current()->pcb;

  // dir should be the dir we are trying to change to
  struct dir* dir = NULL;

  // inode should be the inode of the dir we are trying to change to
  struct inode* inode = check_path(dir_name, 0, name_part, &dir);

  // ensure dir exists before proceeding with change
  if (dir != NULL && inode != NULL) {
    dir_close(pcb->cwd);
    pcb->cwd = dir;
    success = true;
  }

  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  // not sure about the third param for this call
  if (!dir_create(ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/') {
    src++;
  }
  if (*src == '\0') {
    return 0;
  }

  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX) {
      *dst++ = *src;
    } else {
      return -1;
    }
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* 
Check path helper function that returns the inode for given path.
- NOTE: we cannot use result_dir to check existence, since it is always set to some existing dir

Params:
  - path: represents the path we are checking for validity
  - want_parent:
    - 0: will store the directory at path in param result_dir
    - 1: will store the parent directory of the file or directory at path in param result_dir
  - desired_name:
    - preallocated buffer in which we store the name of the desired inode (last segment of path)
  - result_dir:
    - preallocated pointer to struct dir in which we store either the parent dir or the desired dir (if it exists) based on want_parent
      - we shouldn't have the case where want_parent == 0 and the desired dir does not exist (unsure about this)

Return value:
  - If it returns NULL, this means the path does not (yet) exist
  - Else, the inode it returns represents the desired inode (at the last segment of path) 
*/
struct inode* check_path(const char* path, int want_parent, char desired_name[NAME_MAX + 1],
                         struct dir** result_dir) {

  // edge case for empty path
  if (path[0] == '\0') {
    return false;
  }

  struct dir* dir = NULL;
  struct inode* inode = NULL;

  // if absolute path, use root dir
  if (path[0] == '/') {
    dir = dir_open_root();

    // edge case to set inode if path is the absolute path
    if (strlen(path) == 1) {
      inode = dir->inode;
    }
  }

  // else, use cwd
  else {
    struct dir* cwd = thread_current()->pcb->cwd;
    if (cwd == NULL) {
      dir = dir_open_root();
    } else {
      dir = dir_open(inode_open(cwd->inode->sector));
      //dir = dir_open(cwd->inode); // might have to create copy of cwd another way
      // dir = dir_reopen(t->pcb->cwd);
    }
  }

  bool is_file = false;
  bool exists = true;

  // while there are more parts of the path
  while (get_next_part(desired_name, &path) == 1) {

    // search dir entries for desired_name and set inode if exists
    if (!dir_lookup(dir, desired_name, &inode)) {
      exists = false;
      break;
    }

    // if the inode is a file, we cannot call lookup on it, so break
    if (!inode_isdir(inode)) {
      is_file = true;
      break;
    }

    // close dir and inode and open inode to continue looping
    dir_close(dir);
    dir = dir_open(inode);
  }

  // if we want the parent, set dir to the parent_dir
  if (want_parent && !is_file && exists) {
    struct inode* parent_inode = NULL;
    char parent_name[3] = {'.', '.', '\0'};
    if (!dir_lookup(dir, parent_name, &parent_inode)) {
      // SHOULD NEVER REACH HERE
      return false;
    }
    *result_dir = dir_open(parent_inode);
    //dir_close(dir);
  } else {
    *result_dir = dir;
  }
  return inode;
}
