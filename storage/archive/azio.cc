/*
  azio is a modified version of gzio. It  makes use of mysys and removes mallocs.
    -Brian Aker
*/

/* gzio.c -- IO on .gz files
 * Copyright (C) 1995-2005 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 *
 */

/* @(#) $Id$ */

#include "azio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

static int const az_magic[3] = {0xfe, 0x03, 0x01}; /* az magic header */

static unsigned int azwrite(azio_stream *s, void *buf, unsigned int len);
static int azrewind (azio_stream *s);
static unsigned int azio_enable_aio(azio_stream *s);
static int do_flush(azio_stream *file, int flush);
static int    get_byte(azio_stream *s);
static void   check_header(azio_stream *s);
static int write_header(azio_stream *s);
static int    destroy(azio_stream *s);
static void putLong(azio_stream *s, uLong x);
static uLong  getLong(azio_stream *s);
static void read_header(azio_stream *s, unsigned char *buffer);
static void get_block(azio_stream *s);
#ifdef AZIO_AIO
static void do_aio_cleanup(azio_stream *s);
#endif

extern "C"
pthread_handler_t run_task(void *p)
{
  int fd;
  char *buffer;
  size_t offset;
  azio_stream *s= (azio_stream *)p;  

  my_thread_init();

  while (1)
  {
    pthread_mutex_lock(&s->container.thresh_mutex);
    while (s->container.ready == AZ_THREAD_FINISHED)
    {
      pthread_cond_wait(&s->container.threshhold, &s->container.thresh_mutex);
    }
    offset= s->container.offset;
    fd= s->container.fd;
    buffer= (char *)s->container.buffer;
    pthread_mutex_unlock(&s->container.thresh_mutex);

    if (s->container.ready == AZ_THREAD_DEAD)
      break;

    s->container.read_size= pread((int)fd, (void *)buffer,
                                  (size_t)AZ_BUFSIZE_READ, (off_t)offset);

    pthread_mutex_lock(&s->container.thresh_mutex);
    s->container.ready= AZ_THREAD_FINISHED;
    pthread_mutex_unlock(&s->container.thresh_mutex);
  }

  my_thread_end();

  return 0;
}

static void azio_kill(azio_stream *s)
{
  pthread_mutex_lock(&s->container.thresh_mutex);
  s->container.ready= AZ_THREAD_DEAD; 
  pthread_mutex_unlock(&s->container.thresh_mutex);

  pthread_cond_signal(&s->container.threshhold);
  pthread_join(s->container.mainthread, NULL);
}

static size_t azio_return(azio_stream *s)
{
  return s->container.read_size;
}

/*
  Worried about spin?
  Don't be. In tests it never has spun more then 1 times.
*/

static az_thread_type azio_ready(azio_stream *s)
{
  az_thread_type temp;

  while (1)
  {
    pthread_mutex_lock(&s->container.thresh_mutex);
    temp= s->container.ready;
    pthread_mutex_unlock(&s->container.thresh_mutex);

    if (temp == AZ_THREAD_FINISHED || temp == AZ_THREAD_DEAD)
      break;
  }

  return temp;
}

static int azio_start(azio_stream *s)
{
  int rc= 0;
  pthread_attr_t attr;          /* Thread attributes */

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  s->container.ready= AZ_THREAD_FINISHED; 

  /* If we don't create a thread, signal the caller */
  if (pthread_create(&s->container.mainthread, &attr, run_task,
                     (void *)s) != 0)
    rc= 1;

  pthread_attr_destroy(&attr);

  return rc;
}

static int azio_read(azio_stream *s)
{
  pthread_mutex_lock(&s->container.thresh_mutex);
  s->container.ready= AZ_THREAD_ACTIVE; 
  pthread_mutex_unlock(&s->container.thresh_mutex);
  pthread_cond_broadcast(&s->container.threshhold);

  return 0;
}

/* ===========================================================================
  Opens a gzip (.gz) file for reading or writing. The mode parameter
  is as in fopen ("rb" or "wb"). The file is given either by file descriptor
  or path name (if fd == -1).
  az_open returns NULL if the file could not be opened or if there was
  insufficient memory to allocate the (de)compression state; errno
  can be checked to distinguish the two cases (if errno is zero, the
  zlib error is Z_MEM_ERROR).
*/
int azopen(azio_stream *s, const char *path, int Flags, az_method method)
{
  int err;
  int level = Z_DEFAULT_COMPRESSION ; /* compression level */
  int strategy = Z_DEFAULT_STRATEGY; /* compression strategy */
  File fd= -1;

  memset(s, 0, sizeof(azio_stream));

  s->stream.zalloc = (alloc_func)0;
  s->stream.zfree = (free_func)0;
  s->stream.opaque = (voidpf)0;


  s->container.offset= 0;
  s->container.buffer= (void *)s->buffer1;
  s->container.ready= AZ_THREAD_FINISHED;

  s->inbuf= s->buffer1;
  s->stream.next_in = s->inbuf;
  s->stream.next_out = s->outbuf;
  s->z_err = Z_OK;
  s->back = EOF;
  s->crc = crc32(0L, Z_NULL, 0);
  s->mode = 'r';
  s->version = (unsigned char)az_magic[1]; /* this needs to be a define to version */
  s->version = (unsigned char)az_magic[2]; /* minor version */
  s->method= method;

  /*
    We do our own version of append by nature. 
    We must always have write access to take card of the header.
  */
  assert(Flags | O_APPEND);
  assert(Flags | O_WRONLY);

  if (Flags & O_RDWR) 
    s->mode = 'w';

  if (s->mode == 'w') 
  {
    err = deflateInit2(&(s->stream), level,
                       Z_DEFLATED, -MAX_WBITS, 8, strategy);
    /* windowBits is passed < 0 to suppress zlib header */

    s->stream.next_out = s->outbuf;
    if (err != Z_OK)
    {
      destroy(s);
      return Z_NULL;
    }
  } else {
    /* Threads are only used when we are running with azio */
    s->stream.next_in  = s->inbuf;

    err = inflateInit2(&(s->stream), -MAX_WBITS);
    /* windowBits is passed < 0 to tell that there is no zlib header.
     * Note that in this case inflate *requires* an extra "dummy" byte
     * after the compressed stream in order to complete decompression and
     * return Z_STREAM_END. Here the gzip CRC32 ensures that 4 bytes are
     * present after the compressed stream.
   */
    if (err != Z_OK)
    {
      destroy(s);
      return Z_NULL;
    }
  }
  s->stream.avail_out = AZ_BUFSIZE_WRITE;

  errno = 0;
  s->file = fd < 0 ? my_open(path, Flags, MYF(0)) : fd;
#ifdef AZIO_AIO
  s->container.fd= s->file;
#endif

  if (s->file < 0 ) 
  {
    destroy(s);
    return Z_NULL;
  }

  if (Flags & O_CREAT || Flags & O_TRUNC) 
  {
    s->rows= 0;
    s->forced_flushes= 0;
    s->shortest_row= 0;
    s->longest_row= 0;
    s->auto_increment= 0;
    s->check_point= 0;
    s->comment_start_pos= 0;
    s->comment_length= 0;
    s->frm_start_pos= 0;
    s->frm_length= 0;
    s->dirty= 1; /* We create the file dirty */
    s->start = AZHEADER_SIZE + AZMETA_BUFFER_SIZE;
    if(write_header(s))
      return Z_NULL;
    s->pos= (size_t)my_seek(s->file, 0, MY_SEEK_END, MYF(0));
  }
  else if (s->mode == 'w') 
  {
    unsigned char buffer[AZHEADER_SIZE + AZMETA_BUFFER_SIZE];
    const ssize_t read_size= AZHEADER_SIZE + AZMETA_BUFFER_SIZE;
    if(pread(s->file, buffer, read_size, 0) < read_size)
      return Z_NULL;
    read_header(s, buffer);
    s->pos= (size_t)my_seek(s->file, 0, MY_SEEK_END, MYF(0));
  }
  else
  {
    check_header(s); /* skip the .az header */
  }

  switch (s->method)
  {
  case AZ_METHOD_AIO:
    azio_enable_aio(s);
    break;
  case AZ_METHOD_BLOCK:
  case AZ_METHOD_MAX:
    break;
  }

  return 1;
}


int write_header(azio_stream *s)
{
  char buffer[AZHEADER_SIZE + AZMETA_BUFFER_SIZE];
  char *ptr= buffer;

  s->block_size= AZ_BUFSIZE_WRITE;
  s->version = (unsigned char)az_magic[1];
  s->minor_version = (unsigned char)az_magic[2];


  /* Write a very simple .az header: */
  memset(buffer, 0, AZHEADER_SIZE + AZMETA_BUFFER_SIZE);
  *(ptr + AZ_MAGIC_POS)= az_magic[0];
  *(ptr + AZ_VERSION_POS)= (unsigned char)s->version;
  *(ptr + AZ_MINOR_VERSION_POS)= (unsigned char)s->minor_version;
  *(ptr + AZ_BLOCK_POS)= (unsigned char)(s->block_size/1024); /* Reserved for block size */
  *(ptr + AZ_STRATEGY_POS)= (unsigned char)Z_DEFAULT_STRATEGY; /* Compression Type */

  int4store(ptr + AZ_FRM_POS, s->frm_start_pos); /* FRM Block */
  int4store(ptr + AZ_FRM_LENGTH_POS, s->frm_length); /* FRM Block */
  int4store(ptr + AZ_COMMENT_POS, s->comment_start_pos); /* COMMENT Block */
  int4store(ptr + AZ_COMMENT_LENGTH_POS, s->comment_length); /* COMMENT Block */
  int4store(ptr + AZ_META_POS, 0); /* Meta Block */
  int4store(ptr + AZ_META_LENGTH_POS, 0); /* Meta Block */
  int8store(ptr + AZ_START_POS, (uint64_t)s->start); /* Start of Data Block Index Block */
  int8store(ptr + AZ_ROW_POS, (uint64_t)s->rows); /* Start of Data Block Index Block */
  int8store(ptr + AZ_FLUSH_POS, (uint64_t)s->forced_flushes); /* Start of Data Block Index Block */
  int8store(ptr + AZ_CHECK_POS, (uint64_t)s->check_point); /* Start of Data Block Index Block */
  int8store(ptr + AZ_AUTOINCREMENT_POS, (uint64_t)s->auto_increment); /* Start of Data Block Index Block */
  int4store(ptr+ AZ_LONGEST_POS , s->longest_row); /* Longest row */
  int4store(ptr+ AZ_SHORTEST_POS, s->shortest_row); /* Shorest row */
  int4store(ptr+ AZ_FRM_POS, 
            AZHEADER_SIZE + AZMETA_BUFFER_SIZE); /* FRM position */
  *(ptr + AZ_DIRTY_POS)= (unsigned char)s->dirty; /* Start of Data Block Index Block */

  /* Always begin at the begining, and end there as well */
  const ssize_t write_size= AZHEADER_SIZE + AZMETA_BUFFER_SIZE;
  if(pwrite(s->file, (unsigned char*) buffer, write_size, 0)!=write_size)
    return -1;

  return 0;
}

/* ===========================================================================
  Read a byte from a azio_stream; update next_in and avail_in. Return EOF
  for end of file.
  IN assertion: the stream s has been sucessfully opened for reading.
*/
int get_byte(azio_stream *s)
{
  if (s->z_eof) return EOF;
  if (s->stream.avail_in == 0) 
  {
    errno = 0;
    if (s->stream.avail_in == 0) 
    {
      s->z_eof = 1;
      return EOF;
    }
    else if (s->stream.avail_in == (uInt) -1)
    {
      s->z_eof= 1;
      s->z_err= Z_ERRNO;
      return EOF;
    }
    s->stream.next_in = s->inbuf;
  }
  s->stream.avail_in--;
  return *(s->stream.next_in)++;
}

/* ===========================================================================
  Check the gzip header of a azio_stream opened for reading.
  IN assertion: the stream s has already been created sucessfully;
  s->stream.avail_in is zero for the first time, but may be non-zero
  for concatenated .gz files.
*/
void check_header(azio_stream *s)
{
  uInt len;

  /* Assure two bytes in the buffer so we can peek ahead -- handle case
    where first byte of header is at the end of the buffer after the last
    gzip segment */
  len = s->stream.avail_in;
  if (len < 2) {
    if (len) s->inbuf[0] = s->stream.next_in[0];
    errno = 0;
    len = (uInt)pread(s->file, (unsigned char *)s->inbuf + len, AZ_BUFSIZE_READ >> len, s->pos);
    s->pos+= len;
    if (len == (uInt)-1) s->z_err = Z_ERRNO;
    s->stream.avail_in += len;
    s->stream.next_in = s->inbuf;
  }

  /* Now we check the actual header */
  if ( s->stream.next_in[0] == az_magic[0]  && s->stream.next_in[1] == az_magic[1])
  {
    unsigned char buffer[AZHEADER_SIZE + AZMETA_BUFFER_SIZE];

    for (len = 0; len < (AZHEADER_SIZE + AZMETA_BUFFER_SIZE); len++)
      buffer[len]= get_byte(s);
    s->z_err = s->z_eof ? Z_DATA_ERROR : Z_OK;
    read_header(s, buffer);
    for (; len < s->start; len++)
      get_byte(s);
  }
  else
  {
    s->z_err = Z_OK;

    return;
  }
}

void read_header(azio_stream *s, unsigned char *buffer)
{
  if (buffer[0] == az_magic[0]  && buffer[1] == az_magic[1])
  {
    s->version= (unsigned int)buffer[AZ_VERSION_POS];
    s->minor_version= (unsigned int)buffer[AZ_MINOR_VERSION_POS];
    s->block_size= 1024 * buffer[AZ_BLOCK_POS];
    s->start= (size_t)uint8korr(buffer + AZ_START_POS);
    s->rows= (uint64_t)uint8korr(buffer + AZ_ROW_POS);
    s->check_point= (uint64_t)uint8korr(buffer + AZ_CHECK_POS);
    s->forced_flushes= (uint64_t)uint8korr(buffer + AZ_FLUSH_POS);
    s->auto_increment= (uint64_t)uint8korr(buffer + AZ_AUTOINCREMENT_POS);
    s->longest_row= (unsigned int)uint4korr(buffer + AZ_LONGEST_POS);
    s->shortest_row= (unsigned int)uint4korr(buffer + AZ_SHORTEST_POS);
    s->frm_start_pos= (unsigned int)uint4korr(buffer + AZ_FRM_POS);
    s->frm_length= (unsigned int)uint4korr(buffer + AZ_FRM_LENGTH_POS);
    s->comment_start_pos= (unsigned int)uint4korr(buffer + AZ_COMMENT_POS);
    s->comment_length= (unsigned int)uint4korr(buffer + AZ_COMMENT_LENGTH_POS);
    s->dirty= (unsigned int)buffer[AZ_DIRTY_POS];
  }
  else
  {
    s->z_err = Z_OK;
    return;
  }
}

/* ===========================================================================
 * Cleanup then free the given azio_stream. Return a zlib error code.
 Try freeing in the reverse order of allocations.
 */
int destroy (azio_stream *s)
{
  int err = Z_OK;

  if (s->stream.state != NULL) 
  {
    if (s->mode == 'w') 
    {
      err = deflateEnd(&(s->stream));
      my_sync(s->file, MYF(0));
    }
    else if (s->mode == 'r') 
      err = inflateEnd(&(s->stream));
  }

  do_aio_cleanup(s);

  if (s->file > 0 && my_close(s->file, MYF(0))) 
      err = Z_ERRNO;

  s->file= -1;

  if (s->z_err < 0) err = s->z_err;

  return err;
}

/* ===========================================================================
  Reads the given number of uncompressed bytes from the compressed file.
  azread returns the number of bytes actually read (0 for end of file).
*/
unsigned int azread_internal( azio_stream *s, voidp buf, unsigned int len, int *error)
{
  Bytef *start = (Bytef*)buf; /* starting point for crc computation */
  Byte  *next_out; /* == stream.next_out but not forced far (for MSDOS) */
  *error= 0;

  if (s->mode != 'r')
  { 
    *error= Z_STREAM_ERROR;
    return 0;
  }

  if (s->z_err == Z_DATA_ERROR || s->z_err == Z_ERRNO)
  { 
    *error= s->z_err;
    return 0;
  }

  if (s->z_err == Z_STREAM_END)  /* EOF */
  { 
    return 0;
  }

  next_out = (Byte*)buf;
  s->stream.next_out = (Bytef*)buf;
  s->stream.avail_out = len;

  if (s->stream.avail_out && s->back != EOF) {
    *next_out++ = s->back;
    s->stream.next_out++;
    s->stream.avail_out--;
    s->back = EOF;
    s->out++;
    start++;
    if (s->last) {
      s->z_err = Z_STREAM_END;
      { 
        return 1;
      }
    }
  }

  while (s->stream.avail_out != 0) {

    if (s->stream.avail_in == 0 && !s->z_eof) {

      errno = 0;
      get_block(s);
      if (s->stream.avail_in == 0) 
      {
        s->z_eof = 1;
      }
      s->stream.next_in = (Bytef *)s->inbuf;
    }
    s->in += s->stream.avail_in;
    s->out += s->stream.avail_out;
    s->z_err = inflate(&(s->stream), Z_NO_FLUSH);
    s->in -= s->stream.avail_in;
    s->out -= s->stream.avail_out;

    if (s->z_err == Z_STREAM_END) {
      /* Check CRC and original size */
      s->crc = crc32(s->crc, start, (uInt)(s->stream.next_out - start));
      start = s->stream.next_out;

      if (getLong(s) != s->crc) {
        s->z_err = Z_DATA_ERROR;
      } else {
        (void)getLong(s);
        /* The uncompressed length returned by above getlong() may be
         * different from s->out in case of concatenated .gz files.
         * Check for such files:
       */
        check_header(s);
        if (s->z_err == Z_OK) 
        {
          inflateReset(&(s->stream));
          s->crc = crc32(0L, Z_NULL, 0);
        }
      }
    }
    if (s->z_err != Z_OK || s->z_eof) break;
  }
  s->crc = crc32(s->crc, start, (uInt)(s->stream.next_out - start));

  if (len == s->stream.avail_out &&
      (s->z_err == Z_DATA_ERROR || s->z_err == Z_ERRNO))
  {
    *error= s->z_err;

    return 0;
  }

  return (len - s->stream.avail_out);
}

/* ===========================================================================
  Experimental Interface. We abstract out a concecpt of rows 
*/
size_t azwrite_row(azio_stream *s, void *buf, unsigned int len)
{
  size_t length;
  /* First we write length */
  length= azwrite(s, &len, sizeof(unsigned int));

  if (length != sizeof(unsigned int))
    return length;

  /* Now we write the actual data */
  length= (size_t)azwrite(s, buf, len);

  if (length > 0)
    s->rows++;

  if (len > s->longest_row)
    s->longest_row= len;

  if (len < s->shortest_row || !(s->shortest_row))
    s->shortest_row= len;

  return length;
}

size_t azread_row(azio_stream *s, int *error)
{
  unsigned int row_length; /* Currently we are limited to this size for rows */
  char buffer[sizeof(unsigned int)];
  char *new_ptr;
  size_t read;

  read= azread_internal(s, buffer, sizeof(unsigned int), error);
  
  /* On error the return value will be zero as well */
  if (read == 0)
    return read;
  memcpy(&row_length, buffer, sizeof(unsigned int));

  new_ptr= (char *)realloc(s->row_ptr, (sizeof(char) * row_length));

  if (!new_ptr)
    return SIZE_MAX;

  s->row_ptr= new_ptr;

  /* TODO We should now adjust the length... */
  read= azread_internal(s, (voidp)s->row_ptr, row_length, error);

  return read;
}


/* ===========================================================================
  Writes the given number of uncompressed bytes into the compressed file.
  azwrite returns the number of bytes actually written (0 in case of error).
*/
static unsigned int azwrite(azio_stream *s, void *buf, unsigned int len)
{
  s->stream.next_in = (Bytef*)buf;
  s->stream.avail_in = len;

  while (s->stream.avail_in != 0) 
  {
    if (s->stream.avail_out == 0) 
    {

      s->stream.next_out = s->outbuf;
      if (pwrite(s->file, (unsigned char *)s->outbuf, AZ_BUFSIZE_WRITE, s->pos) != AZ_BUFSIZE_WRITE) 
      {
        s->z_err = Z_ERRNO;
        break;
      }
      s->pos+= AZ_BUFSIZE_WRITE;
      s->stream.avail_out = AZ_BUFSIZE_WRITE;
    }
    s->in += s->stream.avail_in;
    s->out += s->stream.avail_out;
    s->z_err = deflate(&(s->stream), Z_NO_FLUSH);
    s->in -= s->stream.avail_in;
    s->out -= s->stream.avail_out;
    if (s->z_err != Z_OK) break;
  }
  s->crc = crc32(s->crc, (const Bytef *)buf, len);

  return (unsigned int)(len - s->stream.avail_in);
}


/* ===========================================================================
  Flushes all pending output into the compressed file. The parameter
  flush is as in the deflate() function.
*/
int do_flush (azio_stream *s, int flush)
{
  uInt len;
  int done = 0;
  size_t afterwrite_pos;

  if (s == NULL || s->mode != 'w') return Z_STREAM_ERROR;

  s->stream.avail_in = 0; /* should be zero already anyway */

  for (;;) 
  {
    len = AZ_BUFSIZE_WRITE - s->stream.avail_out;

    if (len != 0) 
    {
      if ((uInt)pwrite(s->file, (unsigned char *)s->outbuf, len, s->pos) != len) 
      {
        s->z_err = Z_ERRNO;
        assert(0);
        return Z_ERRNO;
      }
      s->pos+= len;
      s->check_point= s->pos;
      s->stream.next_out = s->outbuf;
      s->stream.avail_out = AZ_BUFSIZE_WRITE;
    }
    if (done) break;
    s->out += s->stream.avail_out;
    s->z_err = deflate(&(s->stream), flush);
    s->out -= s->stream.avail_out;

    /* Ignore the second of two consecutive flushes: */
    if (len == 0 && s->z_err == Z_BUF_ERROR) s->z_err = Z_OK;

    /* deflate has finished flushing only when it hasn't used up
     * all the available space in the output buffer:
   */
    done = (s->stream.avail_out != 0 || s->z_err == Z_STREAM_END);

    if (s->z_err != Z_OK && s->z_err != Z_STREAM_END) break;
  }

  if (flush == Z_FINISH)
    s->dirty= AZ_STATE_CLEAN; /* Mark it clean, we should be good now */
  else
    s->dirty= AZ_STATE_SAVED; /* Mark it clean, we should be good now */

  afterwrite_pos= (size_t)my_tell(s->file);
  if(write_header(s))
    return Z_ERRNO;

  return  s->z_err == Z_STREAM_END ? Z_OK : s->z_err;
}

static unsigned int azio_enable_aio(azio_stream *s)
{
  pthread_cond_init(&s->container.threshhold, NULL);
  pthread_mutex_init(&s->container.thresh_mutex, NULL);
  azio_start(s);

  return 0;
}

static void azio_disable_aio(azio_stream *s)
{
  azio_kill(s);

  pthread_mutex_destroy(&s->container.thresh_mutex);
  pthread_cond_destroy(&s->container.threshhold);

  s->method= AZ_METHOD_BLOCK;
}

int ZEXPORT azflush (azio_stream *s,int flush)
{
  int err;

  if (s->mode == 'r') 
  {
    unsigned char buffer[AZHEADER_SIZE + AZMETA_BUFFER_SIZE];
    const ssize_t read_size= AZHEADER_SIZE + AZMETA_BUFFER_SIZE;
    if(pread(s->file, (unsigned char*) buffer, read_size, 0)!=read_size)
      return Z_ERRNO;
    read_header(s, buffer); /* skip the .az header */
    azrewind(s);

    return Z_OK;
  }
  else
  {
    s->forced_flushes++;
    err= do_flush(s, flush);

    if (err) return err;
    my_sync(s->file, MYF(0));
    return  s->z_err == Z_STREAM_END ? Z_OK : s->z_err;
  }
}

/* ===========================================================================
  Initiazliaze for reading
*/
int azread_init(azio_stream *s)
{
  int returnable;

  /* This will reset any aio reads */
  returnable= azrewind(s);

  if (returnable == -1)
    return returnable;

  /* Put one in the chamber */
  if (s->method != AZ_METHOD_BLOCK)
  {
    do_aio_cleanup(s);
    s->container.offset= s->pos;
    s->container.buffer= (unsigned char *)s->buffer1;
    azio_read(s);
    s->aio_inited= 1;
  }


  return returnable;
}

/* ===========================================================================
  Rewinds input file.
*/
int azrewind (azio_stream *s)
{
  if (s == NULL || s->mode != 'r') return -1;

#ifdef AZIO_AIO
  do_aio_cleanup(s);
#endif
  s->z_err = Z_OK;
  s->z_eof = 0;
  s->back = EOF;
  s->stream.avail_in = 0;
  s->stream.next_in = (Bytef *)s->inbuf;
  s->crc = crc32(0L, Z_NULL, 0);
  (void)inflateReset(&s->stream);
  s->in = 0;
  s->out = 0;
  s->aio_inited= 0; /* Reset the AIO reader */
  s->pos= s->start;
  return 0;
}

/* ===========================================================================
  Sets the starting position for the next azread or azwrite on the given
  compressed file. The offset represents a number of bytes in the
  azseek returns the resulting offset location as measured in bytes from
  the beginning of the uncompressed stream, or -1 in case of error.
  SEEK_END is not implemented, returns error.
  In this version of the library, azseek can be extremely slow.
*/
size_t azseek (azio_stream *s, size_t offset, int whence)
{

  if (s == NULL || whence == SEEK_END ||
      s->z_err == Z_ERRNO || s->z_err == Z_DATA_ERROR) {
    return SIZE_MAX;
  }

  if (s->mode == 'w') 
  {
    if (whence == SEEK_SET) 
      offset -= s->in;

    /* At this point, offset is the number of zero bytes to write. */
    /* There was a zmemzero here if inbuf was null -Brian */
    while (offset > 0)  
    {
      uInt size = AZ_BUFSIZE_READ;
      if (offset < AZ_BUFSIZE_READ) size = (uInt)offset;

      size = azwrite(s, s->inbuf, size);
      if (size == 0)
        return SIZE_MAX;

      offset -= size;
    }
    return s->in;
  }
  /* Rest of function is for reading only */

  /* compute absolute position */
  if (whence == SEEK_CUR) {
    offset += s->out;
  }

  /* For a negative seek, rewind and use positive seek */
  if (offset >= s->out) {
    offset -= s->out;
  } else if (azrewind(s)) {
    return SIZE_MAX;
  }
  /* offset is now the number of bytes to skip. */

  if (offset && s->back != EOF) {
    s->back = EOF;
    s->out++;
    offset--;
    if (s->last) s->z_err = Z_STREAM_END;
  }
  while (offset > 0)  {
    int error;
    unsigned int size = AZ_BUFSIZE_WRITE;
    if (offset < AZ_BUFSIZE_WRITE) size = (int)offset;

    size = azread_internal(s, s->outbuf, size, &error);
    if (error < 0) return SIZE_MAX;
    offset -= size;
  }
  return s->out;
}

/* ===========================================================================
  Returns the starting position for the next azread or azwrite on the
  given compressed file. This position represents a number of bytes in the
  uncompressed data stream.
*/
size_t ZEXPORT aztell (azio_stream *file)
{
  return azseek(file, 0L, SEEK_CUR);
}


/* ===========================================================================
  Outputs a long in LSB order to the given file
*/
void putLong (azio_stream *s, uLong x)
{
  int n;
  unsigned char buffer[1];

  for (n = 0; n < 4; n++) 
  {
    buffer[0]= (int)(x & 0xff);
    assert(pwrite(s->file, buffer, 1, s->pos)==1);
    s->pos++;
    x >>= 8;
  }
}

/* ===========================================================================
  Reads a long in LSB order from the given azio_stream. Sets z_err in case
  of error.
*/
uLong getLong (azio_stream *s)
{
  uLong x = (uLong)get_byte(s);
  int c;

  x += ((uLong)get_byte(s))<<8;
  x += ((uLong)get_byte(s))<<16;
  c = get_byte(s);
  if (c == EOF) s->z_err = Z_DATA_ERROR;
  x += ((uLong)c)<<24;
  return x;
}

/* ===========================================================================
  Flushes all pending output if necessary, closes the compressed file
  and deallocates all the (de)compression state.
*/
int azclose (azio_stream *s)
{
  int returnable;

  if (s == NULL) return Z_STREAM_ERROR;
  
  if (s->file < 1) return Z_OK;

  if (s->mode == 'w') 
  {
    if (do_flush(s, Z_FINISH) != Z_OK)
      return destroy(s);

    putLong(s, s->crc);
    putLong(s, (uLong)(s->in & 0xffffffff));
    s->dirty= AZ_STATE_CLEAN;
    write_header(s);
  }

  returnable= destroy(s);

  switch (s->method)
  {
  case AZ_METHOD_AIO:
    azio_disable_aio(s);
    break;
  case AZ_METHOD_BLOCK:
  case AZ_METHOD_MAX:
    break;
  }

  /* If we allocated memory for row reading, now free it */
  if (s->row_ptr)
    free(s->row_ptr);

  return returnable;
}

/*
  Though this was added to support MySQL's FRM file, anything can be 
  stored in this location.
*/
int azwrite_frm(azio_stream *s, char *blob, unsigned int length)
{
  if (s->mode == 'r') 
    return 1;

  if (s->rows > 0) 
    return 1;

  s->frm_start_pos= (uint) s->start;
  s->frm_length= length;
  s->start+= length;

  if (pwrite(s->file, (unsigned char*) blob, s->frm_length, s->frm_start_pos) != (ssize_t)s->frm_length)
    return 1;

  write_header(s);
  s->pos= (size_t)my_seek(s->file, 0, MY_SEEK_END, MYF(0));

  return 0;
}

int azread_frm(azio_stream *s, char *blob)
{
  ssize_t r= pread(s->file, (unsigned char*) blob,
                   s->frm_length, s->frm_start_pos);
  if (r != (ssize_t)s->frm_length)
    return r;

  return 0;
}


/*
  Simple comment field
*/
int azwrite_comment(azio_stream *s, char *blob, unsigned int length)
{
  if (s->mode == 'r') 
    return 1;

  if (s->rows > 0) 
    return 1;

  s->comment_start_pos= (uint) s->start;
  s->comment_length= length;
  s->start+= length;

  ssize_t r= pwrite(s->file, (unsigned char*) blob,
                    s->comment_length, s->comment_start_pos);
  if (r != (ssize_t)s->comment_length)
    return r;

  write_header(s);
  s->pos= (size_t)my_seek(s->file, 0, MY_SEEK_END, MYF(0));

  return 0;
}

int azread_comment(azio_stream *s, char *blob)
{
  ssize_t r= pread(s->file, (unsigned char*) blob,
                   s->comment_length, s->comment_start_pos);
  if (r != (ssize_t)s->comment_length)
    return r;

  return 0;
}

#ifdef AZIO_AIO
static void do_aio_cleanup(azio_stream *s)
{
  if (s->method == AZ_METHOD_BLOCK)
    return;

  azio_ready(s);

}
#endif

/* 
  Normally all IO goes through azio_read(), but in case of error or non-support
  we make use of pread().
*/
static void get_block(azio_stream *s)
{
#ifdef AZIO_AIO
  if (s->method == AZ_METHOD_AIO && s->mode == 'r' 
      && s->pos < s->check_point
      && s->aio_inited)
  {
    azio_ready(s);
    s->stream.avail_in= (unsigned int)azio_return(s);
    if ((int)(s->stream.avail_in) == -1)
      goto use_pread;
    else if ((int)(s->stream.avail_in) == 0)
    {
      s->aio_inited= 0;
      return;
    }
    s->pos+= s->stream.avail_in;
    s->inbuf= (Byte *)s->container.buffer;
    /* We only aio_read when we know there is more data to be read */
    if (s->pos >= s->check_point)
    {
      s->aio_inited= 0;
      return;
    }
    s->container.buffer= (s->container.buffer == s->buffer2) ? s->buffer1 : s->buffer2;
    s->container.offset= s->pos;
    azio_read(s);
  }
  else
#endif
  {
#ifdef AZIO_AIO
use_pread:
#endif
    s->stream.avail_in = (uInt)pread(s->file, (unsigned char *)s->inbuf,
                                     AZ_BUFSIZE_READ, s->pos);
    s->pos+= s->stream.avail_in;
  }
}
