#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

void find(char *path, char *filename) {
  struct stat st;
  if (stat(path, &st)) {
    fprintf(2, "find: cannot stat %s\n", path);
    return;
  }
  if (st.type == T_FILE) {
    fprintf(2, "find: %s not a directory.\n", path);
    return;
  }

  char buf[512];  // buffer to iterate over dir
  if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
    printf("find: path too long\n");
    return;
  }
  strcpy(buf, path);
  char *p = buf + strlen(buf);
  *p++ = '/';

  int fd = open(path, O_RDONLY);
  struct dirent de;
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0) continue;
    if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
      continue;  // avoid iterating into . or ..
                 //
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;

    if (stat(buf, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }

    if (st.type == T_FILE) {
      if (strcmp(de.name, filename) == 0) printf("%s\n", buf);
    } else if (st.type == T_DIR) {
      find(buf, filename);
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("Wrong arg num! Usage: find <path> <name>\n");
    exit(-1);
  }
  char *path = argv[1];
  char *filename = argv[2];
  find(path, filename);
  exit(0);
}