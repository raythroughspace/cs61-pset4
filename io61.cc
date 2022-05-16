#include "io61.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>
#include <sys/mman.h>

// io61.c
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
    int mode;
    unsigned char* file_data; //mmap return pointer
    size_t file_size;
    size_t mmap_pos; //If file is mmaped, it is the offset of next char to read/write (all other off_sets are no longer used)
    static constexpr off_t bufsize = 4096; // block size for this cache
    unsigned char cbuf[bufsize];
    // These “tags” are addresses—file offsets—that describe the cache’s contents.
    off_t tag;      // file offset of first byte in cache (0 when file is opened)
    off_t end_tag;  // file offset one past last valid byte in cache
    off_t pos_tag;  // file offset of next char to read in cache
};


// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.

io61_file* io61_fdopen(int fd, int mode) {
    io61_file* f = new io61_file;
    f->fd = fd;
    f->mode = mode;
    f->pos_tag = f->end_tag = f->tag = f->mmap_pos = 0;
    f->file_size = io61_filesize(f);
    if (f->file_size != (size_t) -1){
        int prot = f->mode == O_RDONLY ? PROT_READ : PROT_WRITE;
        f->file_data = (unsigned char*) mmap(nullptr, f->file_size, prot, MAP_SHARED, f->fd, 0);
    }
    else{
        f->file_data = (unsigned char*) MAP_FAILED;
    }
    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file* f) {
    if (f->mode == O_WRONLY && f->file_data == (unsigned char*) MAP_FAILED){
        io61_flush(f);
    }
    if (f->file_data != (unsigned char*) MAP_FAILED){
         munmap(f->file_data, f->file_size);
    }
    int r = close(f->fd);
    delete f;
    return r;
}

int io61_fill(io61_file* f) {
    // Fill the read cache with new data, starting from file offset `end_tag`.
    // Only called for read caches.

    // Check invariants.
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);

    // Reset the cache to empty.
    f->tag = f->pos_tag = f->end_tag;

    ssize_t r = read(f->fd, f->cbuf, f->bufsize);
    if (r>=0){
        f->end_tag+= r;
    }
    return r;
    // Recheck invariants (good practice!).
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);
}

// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    unsigned char buf[1];
    if (f->file_data != (unsigned char*) MAP_FAILED){
        if (f->mmap_pos >= f->file_size){
            return -1;
        }
        buf[0] = f->file_data[f->mmap_pos];
        ++f->mmap_pos;
        return buf[0];
    }
    if (f->end_tag == f->pos_tag){
        int r = io61_fill(f);
        if (f->end_tag == f->pos_tag || r < 0){ //EOF or error
            return -1;
        }
    }
    buf[0] = f->cbuf[f->pos_tag - f->tag];
    ++f->pos_tag;
    return buf[0];
}

// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file* f, unsigned char* buf, size_t sz) {
    if (f->file_data != (unsigned char*) MAP_FAILED){
        if (f->mmap_pos >= f->file_size){
            return -1;
        }
        if (sz + f->mmap_pos > f->file_size){
            sz = f->file_size - f->mmap_pos; //short read
        }
        memcpy(buf, &f->file_data[f->mmap_pos], sz);
        f->mmap_pos += sz;
        return sz;
    }
    // Check invariants.
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);

    size_t pos = 0;
    while (pos < sz){
        if (f->end_tag == f->pos_tag){
            int r = io61_fill(f);
            if (f->end_tag == f->pos_tag){
                break;
            }
            if (r < 0){ //read error
                return -1;
            }
        }
        size_t to_read = f->end_tag - f->pos_tag;
        if (sz - pos <= to_read){
            to_read = sz - pos;
        }
        memcpy(&buf[pos], &f->cbuf[f->pos_tag - f->tag], to_read);
        pos+= to_read;
        f->pos_tag += to_read;
    }
    return pos;
    // Note: This function never returns -1 because `io61_readc`
    // does not distinguish between error and end-of-file.
    // Your final version should return -1 if a system call indicates
    // an error.
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    if (f->file_data != (unsigned char*) MAP_FAILED){
        if (f->mmap_pos >= f->file_size){
            return -1;
        }
        f->file_data[f->mmap_pos] = (unsigned char) ch;
        ++f->mmap_pos;
        return 0;
    }
    // Check invariants.
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);

    // Write cache invariant.
    assert(f->pos_tag == f->end_tag);

    unsigned char buf[1];
    buf[0] = ch;
    if (f->end_tag- f->tag == f->bufsize){
        int r = io61_flush(f);
        if (r< 0){
            return -1; //write error
        }
    }
    f->cbuf[f->pos_tag - f->tag] = ch;
    ++f->pos_tag;
    f->end_tag = f->pos_tag;
    return 0;
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const unsigned char* buf, size_t sz) {
    if (f->file_data != (unsigned char*) MAP_FAILED){
        if (f->mmap_pos >= f->file_size){
            return -1; //nothing to write
        }
        if (sz + f->mmap_pos > f->file_size){
            sz = f->file_size - f->mmap_pos; //short read
        }
        memcpy(&f->file_data[f->mmap_pos],buf, sz);
        f->mmap_pos += sz;
        return sz;
    }
    // Check invariants.
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);

    // Write cache invariant.
    assert(f->pos_tag == f->end_tag);

    size_t pos = 0;
    while (pos < sz){
        if (f->end_tag- f->tag == f->bufsize){
            int r = io61_flush(f);
            if (r< 0){
                return -1; //write error
            }
        }
        size_t to_write = f->bufsize - (f->end_tag - f->tag);
        if (sz - pos <= to_write) {
            to_write = sz - pos;
        }
        memcpy(&f->cbuf[f->pos_tag - f->tag], &buf[pos], to_write);
        pos += to_write;
        f->pos_tag += to_write;
        f->end_tag = f->pos_tag;
    }
    return pos;
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    // Check invariants.
    assert(f->tag <= f->pos_tag && f->pos_tag <= f->end_tag);
    assert(f->end_tag - f->pos_tag <= f->bufsize);

    // Write cache invariant.
    assert(f->pos_tag == f->end_tag);

    ssize_t r = write(f->fd, f->cbuf, f->pos_tag - f->tag);
    if (r>=0){
        f->tag += r;
    }
    return r;
}

// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    if (f->file_data != (unsigned char*) MAP_FAILED){
        if (pos < 0 || pos >= f->file_size){
            return -1;
        }
        f->mmap_pos = pos;
        return 0;
    }
    if (f->mode == O_WRONLY){
        io61_flush(f);
        off_t r = lseek(f->fd, (off_t) pos, SEEK_SET);
        if (r != (off_t) pos) {
            return -1;
        }
        f->tag = pos - (pos % f->bufsize);
        f->pos_tag = pos;
        f->end_tag = f->pos_tag;
    }
    else{ //Read only
        if (pos >= f->tag && pos < f->end_tag){ //new position is already in cache
            f->pos_tag = pos;
            off_t r = lseek(f->fd, (off_t) pos, SEEK_SET);
            if (r != (off_t) pos) {
                return -1;
            }
        }
        else {
            size_t tag = pos - (pos % f->bufsize); //tag is aligned to bufsize before call to read in io61_fill
            off_t r = lseek(f->fd, (off_t) tag, SEEK_SET);
            if (r != (off_t) tag) {
                return -1;
            }
            f->tag = tag;
            f->pos_tag = f->end_tag = f->tag;
            io61_fill(f);
            if (f->pos_tag < pos){ //cache does not contain char at pos
                return -1;
            }
            f->pos_tag = pos;
        }
    }
    return 0;

}

// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `!filename`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != nullptr` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename) {
        fd = open(filename, mode, 0666);
    } else if ((mode & O_ACCMODE) == O_RDONLY) {
        fd = STDIN_FILENO;
    } else {
        fd = STDOUT_FILENO;
    }
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}
