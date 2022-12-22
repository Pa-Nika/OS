#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>

#define COPE_BUF_SIZE 4096
#define NOT_READY 1
#define RETRY_TIME_SEC 1
#define EMPTY NULL
#define STATUS_FAILURE (-1)
#define STATUS_SUCCESS 0

#define IS_STR_EMPTY(STR) ((STR) == EMPTY | (STR)[0] == '\0')
#define IS_PTR_EMPTY(PTR) ((PTR) == EMPTY)
#define IS_STRS_EQUAL(STR1, STR2) (strcmp(STR1, STR2) == 0)

pthread_attr_t attr;
char *destinationPath;

typedef struct {
    char *srcPath;
    char *destPath;
    mode_t mode;
} copyInfo;

enum typeFile {
    type_DIRECTORY,
    type_REGULAR_FILE,
    type_OTHER
};

void *smartMalloc(size_t size) {
    while (NOT_READY) {
        errno = 0;
        void *ptr = malloc(size);
        if (!IS_PTR_EMPTY(ptr)) {
            return ptr;
        }
        if (errno != EAGAIN) {
            fprintf(stderr, "Problem with malloc");
            return EMPTY;
        }
//            sched_yield();
        usleep(100);
    }
}

int smartCreateThread(void *param, void* (*function)(void*)) {
    int status;
    pthread_t thread;
    while (NOT_READY) {
        status = pthread_create(&thread, NULL, function, param);
        if (status == STATUS_SUCCESS) {
//            pthread_detach(thread);
            return STATUS_SUCCESS;
        }
        if (status != EAGAIN) {
            fprintf(stderr, "smartCreateThread. Create thread isn't possible");
            return STATUS_FAILURE;
        }
//            sched_yield();
        usleep(100);
    }
}


copyInfo *createCopyInfo(char *srcPath, char *destPath, mode_t mode) {
    copyInfo *copy = (copyInfo *)smartMalloc(sizeof(copyInfo));
    if (copy == NULL) {
        perror("Error in malloc");
        return copy;
    }
    copy->srcPath = srcPath;
    copy->destPath = destPath;
    copy->mode = mode;
    return copy;
}

void destroyResources() {
    errno = pthread_attr_destroy(&attr);
    if (errno != 0) {
        perror("Error in destroy attr");
    }
}

void freeResourses(copyInfo *info) {
    if (info != NULL) {
        free(info->srcPath);
        free(info->destPath);
        free(info);
    }
}

int initializeStartResources(char** srcBuf, char** destBuf, size_t srcPathLen, size_t destPathLen) {
    *srcBuf = (char *)smartMalloc(srcPathLen * sizeof(char) + 1);
    if (srcBuf == NULL) {
        perror("Error in malloc");
        return -1;
    }
    *destBuf = (char *)smartMalloc(destPathLen * sizeof(char) + 1);
    if (destBuf == NULL) {
        perror("Error in malloc");
        return -1;
    }

    errno = pthread_attr_init(&attr);
    if (errno != 0) {
        perror("Error in attr init");
        return -1;
    }
    errno = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (errno != 0) {
        perror("Error in set detach state");
        destroyResources();
        return -1;
    }
    destinationPath = (char *)smartMalloc(sizeof(char) * destPathLen);
    if (destinationPath == NULL) {
        return -1;
    }
    return 0;
}

int makeDir(copyInfo *info) {
    int status;
    bool limitDirOpen = false;
    while (true) {
        if (limitDirOpen == true) {
//            sched_yield();
            usleep(100);
        }
        status = mkdir(info->destPath, info->mode);
        if (status == 0) {
            return 0;
        }
        limitDirOpen = true;
    }
}

DIR *openDir(const char *dirName) {
    bool limitDirOpen = false;
    while (true) {
        if (limitDirOpen == true) {
//            sched_yield();
            usleep(100);
        }
        DIR *dir = opendir(dirName);
        if (dir != NULL) {
            return dir;
        }
        if (errno != EMFILE)
            return dir;
        limitDirOpen = true;
    }
}

int readDir(DIR *dir, struct dirent *entry, struct dirent **result) {
    errno = readdir_r(dir, entry, result);
    return errno;
}

bool equateString(char *path, char *unsuitablePath) {
    return strcmp(path, unsuitablePath) == 0;
}

char *appendPath(char *dir, char *newName, size_t maxLength) {
    char *path = (char *)smartMalloc(maxLength * sizeof(char));
    if (path == NULL) {
        perror("Error in malloc");
        return path;
    }
    strcpy(path, dir);
    size_t pathLen = strlen(path);
    if (pathLen >= maxLength) {
        perror("The maximum path length has been reached\n");
        return NULL;
    }
    path[pathLen] = '/';
    ++pathLen;
    path[pathLen] = '\0';
    path = strncat(path, newName, maxLength - pathLen);
    return path;
}

int findType(mode_t mode) {
    if (S_ISDIR(mode)) {
        return type_DIRECTORY;
    }
    if (S_ISREG(mode)) {
        return type_REGULAR_FILE;
    }
    return type_OTHER;
}

int createThreadForDir(copyInfo *info);

int createThreadForFile(copyInfo *info);

int checkFile(copyInfo *info) {
    int type = findType(info->mode);
    int retCreate;
    switch (type) {
        case type_DIRECTORY:
            retCreate = createThreadForDir(info);
            if (retCreate != 0) {
                return -1;
            }
            break;
        case type_REGULAR_FILE:
            retCreate = createThreadForFile(info);
            if (retCreate != 0) {
                return -1;
            }
            break;
        case type_OTHER:
            return -2;
    }
    return 0;
}

void closeDir(DIR *dir) {
    bool isMemLimited = false;
    int status;
    while (true) {
        if (isMemLimited) {
//            sched_yield();
            usleep(100);
        }

        status = closedir(dir);
        if (status == 0) {
            return;
        }
        if (status != EAGAIN) {
            fprintf(stderr, "Error in close dir");
//            return -1;
        }
        isMemLimited = true;
    }
}

int createNewPath(char* srcNext, char* destNext, copyInfo* infoNext, copyInfo* info, size_t maxPathLength, struct dirent* entry) {
    struct stat structStat;
    srcNext = appendPath(info->srcPath, entry->d_name, maxPathLength);
    if (srcNext == NULL) {
        perror("Error in append Path src");
        return -1;
    }
    destNext = appendPath(info->destPath, entry->d_name, maxPathLength);
    if (destNext == NULL) {
        perror("Error in append Path dest");
        return -1;
    }
    if (lstat(srcNext, &structStat) != 0) {
        perror("Error in stat");
        return -1;
    }
    infoNext = createCopyInfo(srcNext, destNext, structStat.st_mode);
    if (info == NULL) {
        perror("Error in copy info");
        return -1;
    }
    int retCheck = checkFile(infoNext);
    if (retCheck != 0) {
        return retCheck;
    }
}

int copyDir(copyInfo *info) {
    int ret;
    size_t maxPathLength = (size_t)pathconf(info->srcPath, _PC_PATH_MAX);
    ret = makeDir(info);
    if (ret != 0) {
        perror("Error in make dir");
        return -1;
    }
    DIR *dir = openDir(info->srcPath);
    if (dir == NULL) {
        perror("Error in open dir");
        return -1;
    }
    size_t entryLen = offsetof(struct dirent, d_name) + pathconf(info->srcPath, _PC_NAME_MAX) + 1;
    struct dirent *entry = (struct dirent *)smartMalloc(entryLen);
    struct dirent *result;
    if (entry == NULL) {
        perror("Error in malloc\n");
        closeDir(dir);
        return -1;
    }
    while ((ret = readDir(dir, entry, &result)) == 0) {
        if (result == NULL) {
            break;
        }
        if (equateString(entry->d_name, ".") || equateString(entry->d_name, "..") ||
            equateString(info->srcPath, destinationPath)) {
            continue;
        }
        char *srcNext, *destNext;
        copyInfo* infoNext;
        ret = createNewPath(srcNext, destNext, infoNext, info, maxPathLength, entry);
        if (ret == -1) {
//            perror("Error in create new path\n");
            continue;
        }
    }
    free(entry);
    closeDir(dir);
    return ret;
}

void *copyDirInThread(void *arg) {
    copyInfo *info = (copyInfo *)arg;
    int err = copyDir(info);
    if (err == -1) {
        fprintf(stderr, "Error in this files (DIR THREAD) : %s %s\n", info->srcPath, info->destPath);
    }
    freeResourses(info);
    pthread_exit(0);
}

int openFile(char *file) {
    bool fdLimit = false;
    while (true) {
        if (fdLimit) {
//            sched_yield();
            usleep(100);
        }
        int fd =open(file, O_RDONLY);
        if (fd >= 0) {
            return fd;
        }
        if (errno != EMFILE && errno != 0) {
            perror("Error in open");
            return -1;
        }
        fdLimit = true;
    }
}

int createFile(char *file, mode_t mode) {
    bool fdLimit = false;
    while (true) {
        if (fdLimit) {
//            sched_yield();
            usleep(100);
        }
        int fd = creat(file, mode);
        if (fd >= 0) {
            return fd;
        }
        if (errno != EMFILE && errno != 0) {
            perror("Error in open");
            return -1;
        }
        fdLimit = true;
    }
}

int copyFile(copyInfo *info) {
    int srcFd = openFile(info->srcPath);
    if (srcFd == -1) {
        perror("Error in open file");
        close(srcFd);
        return -1;
    }
    int destFd = createFile(info->destPath, info->mode);
    if (destFd == -1) {
        perror("Error in create file");
        return -1;
    }
    void *buffer = (void *)smartMalloc(COPE_BUF_SIZE);
    if (buffer == NULL) {
        perror("Error in malloc");
        return -1;
    }
    ssize_t readBytes;
    while ((readBytes = read(srcFd, buffer, COPE_BUF_SIZE)) > 0) {
        ssize_t writtenBytes = write(destFd, buffer, readBytes);
        if (writtenBytes < readBytes) {
            perror("Error in write");
            return -1;
        }
    }
    if (readBytes < 0) {
        perror("Error in read");
        return -1;
    }
    free(buffer);
    close(srcFd);
    close(destFd);
    return 0;
}

void *copyFileInThread(void *arg) {
    copyInfo *info = (copyInfo *)arg;
    int err = copyFile(info);
    if (err != 0) {
        fprintf(stderr, "Error in this files (FILE THREAD) : %s %s\n", info->srcPath, info->destPath);
    }
    freeResourses(info);
    pthread_exit(0);
}

int createThreadForDir(copyInfo *info) {
    bool isMemLimited = false;
    pthread_t thread;
    int status;
    while (true) {
        if (isMemLimited) {
//            sched_yield();
            usleep(100);
        }
        status = pthread_create(&thread, &attr, copyDirInThread, (void *)info);
        if (status == 0) {
            return 0;
        }
        if (status != EAGAIN) {
            fprintf(stderr, "Error in pthread create");
            return -1;
        }
        isMemLimited = true;
    }
}

int createThreadForFile(copyInfo *info) {
    bool isMemLimited = false;
    pthread_t thread;
    int status;
    while (true) {
        if (isMemLimited) {
//            sched_yield();
            usleep(100);
        }
        status = pthread_create(&thread, &attr, copyFileInThread, (void *)info);
        if (status == 0) {
            return 0;
        }
//        copyFileInThread((void *)info);
        if (status != EAGAIN) {
            fprintf(stderr, "Error in pthread create");
            return -1;
        }
        isMemLimited = true;
    }
}

int startCp_R(const char* src, const char* dest) {
    size_t srcPathLen = strlen(src);
    size_t destPathLen = strlen(dest);
    char* srcBuf;
    char* destBuf;
    int retInitRes = initializeStartResources(&srcBuf, &destBuf, srcPathLen, destPathLen);
    if (retInitRes != 0) {
        return -1;
    }
    if (atexit(destroyResources) != 0) {
        perror("Error in atexit\n");
        return -1;
    }
    strcpy(srcBuf, src);
    strcpy(destBuf, dest);
    strcpy(destinationPath, dest);
    struct stat structStat;
    if (lstat(srcBuf, &structStat) != 0) {
        perror("Error in stat\n");
        return -1;
    }
    copyInfo *copy = createCopyInfo(srcBuf, destBuf, structStat.st_mode);
    if (copy == NULL) {
        return -1;
    }
    int retCreate = createThreadForDir(copy);
    if (retCreate != 0) {
        freeResourses(copy);
        return -1;
    }
}

int main(int argc, const char **argv) {
    if (argc != 3) {
        printf("Args?\n");
        return 0;
    }

    int status = startCp_R(argv[1], argv[2]);
    if (status != 0) {
        return -1;
    }
    pthread_exit(0);
}