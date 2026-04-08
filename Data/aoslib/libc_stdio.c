#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

struct _FILE {
    int fd;
    int error;
    int eof;
};

static struct _FILE _stdin_struct  = { 0, 0, 0 }; // 0 = STDIN
static struct _FILE _stdout_struct = { 1, 0, 0 }; // 1 = STDOUT
static struct _FILE _stderr_struct = { 2, 0, 0 }; // 2 = STDERR

FILE *stdin  = &_stdin_struct;
FILE *stdout = &_stdout_struct;
FILE *stderr = &_stderr_struct;

FILE *fopen(const char *filename, const char *mode) {
    if (!filename || !mode) return NULL;

    int flags = 0;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        if (mode[1] == '+') flags = O_RDWR;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        return NULL;
    }

    int fd = open(filename, flags);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(struct _FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
    
    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    
    ssize_t res = read(stream->fd, ptr, size * nmemb);
    if (res < 0) {
        stream->error = 1;
        return 0;
    }
    if (res == 0) {
        stream->eof = 1;
        return 0;
    }
    return (size_t)res / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    
    ssize_t res = write(stream->fd, ptr, size * nmemb);
    if (res < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)res / size;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    if (stream == stdin || stream == stdout || stream == stderr) return 0;
    
    int res = close(stream->fd);
    free(stream);
    return (res < 0) ? EOF : 0;
}

int fflush(FILE *stream) {
    // В базовой версии libc мы не используем кэширование в user-space (буферы),
    // каждый fwrite сразу делает системный вызов write.
    // Поэтому очистка буфера (fflush) ничего не делает.
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    off_t res = lseek(stream->fd, offset, whence);
    if (res < 0) return -1;
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int fputc(int c, FILE *stream) {
    unsigned char uc = (unsigned char)c;
    if (fwrite(&uc, 1, 1, stream) != 1) return EOF;
    return uc;
}

int fgetc(FILE *stream) {
    unsigned char uc;
    if (fread(&uc, 1, 1, stream) != 1) return EOF;
    return uc;
}

int puts(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    if (fwrite(s, 1, len, stdout) != len) return EOF;
    if (fwrite("\n", 1, 1, stdout) != 1) return EOF;
    return 0;
}

int fputs(const char *s, FILE *stream) {
    size_t len = 0;
    while (s[len]) len++;
    if (fwrite(s, 1, len, stream) != len) return EOF;
    return 0;
}