/* Code for the buffer data structure.  */

#include "cvs.h"
#include "buffer.h"

/* OS/2 doesn't have EIO.  FIXME: this whole notion of turning
   a different error into EIO strikes me as pretty dubious.  */
#if !defined (EIO)
#define EIO EBADPOS
#endif

/* Linked list of available buffer_data structures.  */
static struct buffer_data *free_buffer_data = NULL;

/* Local functions.  */
static void buf_default_memory_error(struct buffer *);
static void allocate_buffer_datas(void);
static struct buffer_data *get_buffer_data(void);

/* Initialize a buffer structure.  */

struct buffer *
buf_initialize (int (*input) (void *, char *, int, int, int *), int (*output) (void *, const char *, int, int *), int (*flush) (void *), int (*block) (void *, int), int (*shutdown) (void *), void (*memory) (struct buffer *), void *closure)
{
    struct buffer *buf;

    buf = (struct buffer *) xmalloc (sizeof (struct buffer));
    buf->data = NULL;
    buf->last = NULL;
    buf->nonblocking = 0;
    buf->input = input;
    buf->output = output;
    buf->flush = flush;
    buf->block = block;
    buf->shutdown = shutdown;
    buf->memory_error = memory ? memory : buf_default_memory_error;
    buf->closure = closure;
    return buf;
}

/* Free a buffer structure.  */

void
buf_free (struct buffer *buf)
{
    if (buf && buf->data != NULL)
    {
	if(buf->last)
	buf->last->next = free_buffer_data;
	free_buffer_data = buf->data;
    }
    xfree (buf);
}

/* Initialize a buffer structure which is not to be used for I/O.  */

struct buffer *
buf_nonio_initialize (void (*memory) (struct buffer *))
{
    return (buf_initialize
	    ((int (*)(void *, char *, int, int, int *)) NULL,
	     (int (*)(void *, const char *, int, int *)) NULL,
	     (int (*)(void *)) NULL,
	     (int (*)(void *, int)) NULL,
	     (int (*)(void *)) NULL,
	     memory,
	     (void *) NULL));
}

/* Default memory error handler.  */

static void
buf_default_memory_error (struct buffer *buf)
{
    error (1, 0, "out of memory in buffer.c");
}

/* Allocate more buffer_data structures.  */

static void
allocate_buffer_datas (void)
{
    struct buffer_data *alc;
    char *space;
    int i;

    /* Allocate buffer_data structures in blocks of 16.  */
#define ALLOC_COUNT (16)

    alc = ((struct buffer_data *)
	   xmalloc (ALLOC_COUNT * sizeof (struct buffer_data)));
    space = (char *) xmalloc (ALLOC_COUNT * BUFFER_DATA_SIZE);
    if (alc == NULL || space == NULL)
		return;
	memset(alc,0,sizeof(struct buffer_data)); /* TH 8/12/99 Clear memory */
    for (i = 0; i < ALLOC_COUNT; i++, alc++, space += BUFFER_DATA_SIZE)
    {
	alc->next = free_buffer_data;
	free_buffer_data = alc;
	alc->text = space;
    }
}

/* Get a new buffer_data structure.  */

static struct buffer_data *
get_buffer_data (void)
{
    struct buffer_data *ret;

    if (free_buffer_data == NULL)
    {
	allocate_buffer_datas ();
	if (free_buffer_data == NULL)
	    return NULL;
    }

    ret = free_buffer_data;
    free_buffer_data = ret->next;
	ret->next=NULL;
	ret->size=0;
    memset (ret->text, '\0', BUFFER_DATA_SIZE);
	ret->bufp=ret->text;
    return ret;
}

/* See whether a buffer is empty.  */

int
buf_empty_p (struct buffer *buf)
{
    struct buffer_data *data;

    for (data = buf->data; data != NULL; data = data->next)
	if (data->size > 0)
	    return 0;
    return 1;
}

#ifdef SERVER_FLOWCONTROL
/*
 * Count how much data is stored in the buffer..
 * Note that each buffer is a malloc'ed chunk BUFFER_DATA_SIZE.
 */

int
buf_count_mem (struct buffer *buf)
{
    struct buffer_data *data;
    int mem = 0;

    for (data = buf->data; data != NULL; data = data->next)
	mem += BUFFER_DATA_SIZE;

    return mem;
}
#endif /* SERVER_FLOWCONTROL */

/* Add data DATA of length LEN to BUF.  */

void
buf_output (struct buffer *buf, const char *data, int len)
{
    if (buf->data != NULL
	&& (((buf->last->text + BUFFER_DATA_SIZE)
	     - (buf->last->bufp + buf->last->size))
	    >= len))
    {
	memcpy (buf->last->bufp + buf->last->size, data, len);
	buf->last->size += len;
	return;
    }

    while (1)
    {
	struct buffer_data *newdata;

	newdata = get_buffer_data ();
	if (newdata == NULL)
	{
	    (*buf->memory_error) (buf);
	    return;
	}

	if (buf->data == NULL)
	    buf->data = newdata;
	else
	    buf->last->next = newdata;
	newdata->next = NULL;
	buf->last = newdata;

	newdata->bufp = newdata->text;

	if (len <= BUFFER_DATA_SIZE)
	{
	    newdata->size = len;
	    memcpy (newdata->text, data, len);
	    return;
	}

	newdata->size = BUFFER_DATA_SIZE;
	memcpy (newdata->text, data, BUFFER_DATA_SIZE);

	data += BUFFER_DATA_SIZE;
	len -= BUFFER_DATA_SIZE;
    }

    /*NOTREACHED*/
}

/* Add a '\0' terminated string to BUF.  */

void
buf_output0 (struct buffer *buf, const char *string)
{
    buf_output (buf, string, strlen (string));
}

/* Add a single character to BUF.  */

void
buf_append_char (struct buffer *buf, int ch)
{
    if (buf->data != NULL
	&& (buf->last->text + BUFFER_DATA_SIZE
	    != buf->last->bufp + buf->last->size))
    {
	*(buf->last->bufp + buf->last->size) = ch;
	++buf->last->size;
    }
    else
    {
	char b;

	b = ch;
	buf_output (buf, &b, 1);
    }
}

/*
 * Send all the output we've been saving up.  Returns 0 for success or
 * errno code.  If the buffer has been set to be nonblocking, this
 * will just write until the write would block.
 */

int
buf_send_output (struct buffer *buf)
{
    if (buf->output == NULL)
	abort ();

    while (buf->data != NULL)
    {
	struct buffer_data *data;

	data = buf->data;

	if (data->size > 0)
	{
	    int status, nbytes;

	    status = (*buf->output) (buf->closure, data->bufp, data->size,
				     &nbytes);
	    if (status != 0)
	    {
		/* Some sort of error.  Discard the data, and return.  */

		if(buf->last)
		buf->last->next = free_buffer_data;
		free_buffer_data = buf->data;
		buf->data = NULL;
		buf->last = NULL;

	        return status;
	    }

	    if (nbytes != data->size)
	    {
		/* Not all the data was written out.  This is only
                   permitted in nonblocking mode.  Adjust the buffer,
                   and return.  */
//		assert (buf->nonblocking);

		data->size -= nbytes;
		data->bufp += nbytes;

		return 0;
	    }
	}

	buf->data = data->next;
	data->next = free_buffer_data;
	free_buffer_data = data;
    }

    buf->last = NULL;

    return 0;
}

/*
 * Flush any data queued up in the buffer.  If BLOCK is nonzero, then
 * if the buffer is in nonblocking mode, put it into blocking mode for
 * the duration of the flush.  This returns 0 on success, or an error
 * code.
 */

int
buf_flush (struct buffer *buf, int block)
{
    int nonblocking;
    int status;

    if (buf->flush == NULL)
        abort ();

    nonblocking = buf->nonblocking;
    if (nonblocking && block)
    {
        status = set_block (buf);
	if (status != 0)
	    return status;
    }

    status = buf_send_output (buf);
    if (status == 0)
        status = (*buf->flush) (buf->closure);

    if (nonblocking && block)
    {
        int blockstat;

        blockstat = set_nonblock (buf);
	if (status == 0)
	    status = blockstat;
    }

    return status;
}

/*
 * Set buffer BUF to nonblocking I/O.  Returns 0 for success or errno
 * code.
 */

int
set_nonblock (struct buffer *buf)
{
    int status;

    if (buf->nonblocking)
	return 0;
    if (buf->block == NULL)
        abort ();
    status = (*buf->block) (buf->closure, 0);
    if (status != 0)
	return status;
    buf->nonblocking = 1;
    return 0;
}

/*
 * Set buffer BUF to blocking I/O.  Returns 0 for success or errno
 * code.
 */

int
set_block (struct buffer *buf)
{
    int status;

    if (! buf->nonblocking)
	return 0;
    if (buf->block == NULL)
        abort ();
    status = (*buf->block) (buf->closure, 1);
    if (status != 0)
	return status;
    buf->nonblocking = 0;
    return 0;
}

/*
 * Send a character count and some output.  Returns errno code or 0 for
 * success.
 *
 * Sending the count in binary is OK since this is only used on a pipe
 * within the same system.
 */

int
buf_send_counted (struct buffer *buf)
{
    int size;
    struct buffer_data *data;

    size = 0;
    for (data = buf->data; data != NULL; data = data->next)
	size += data->size;

	if(!size)
		return 0;

    data = get_buffer_data ();
    if (data == NULL)
    {
	(*buf->memory_error) (buf);
	return ENOMEM;
    }

    data->next = buf->data;
    buf->data = data;
    if (buf->last == NULL)
	buf->last = data;

    data->bufp = data->text;

    data->size = sizeof (int);

    *((int *) data->text) = size;

    return buf_send_output (buf);
}

/*
 * Send a special count.  COUNT should be negative.  It will be
 * handled speciallyi by buf_copy_counted.  This function returns 0 or
 * an errno code.
 *
 * Sending the count in binary is OK since this is only used on a pipe
 * within the same system.
 */

int
buf_send_special_count (struct buffer *buf, int count)
{
    struct buffer_data *data;

    data = get_buffer_data ();
    if (data == NULL)
    {
	(*buf->memory_error) (buf);
	return ENOMEM;
    }

    data->next = buf->data;
    buf->data = data;
    if (buf->last == NULL)
	buf->last = data;

    data->bufp = data->text;
    data->size = sizeof (int);

    *((int *) data->text) = count;

    return buf_send_output (buf);
}

/* Append a list of buffer_data structures to an buffer.  */

void
buf_append_data (struct buffer *buf, struct buffer_data *data, struct buffer_data *last)
{
    if (data != NULL)
    {
	if (buf->data == NULL)
	    buf->data = data;
	else
	    buf->last->next = data;
	buf->last = last;
    }
}

/* Append the data on one buffer to another.  This removes the data
   from the source buffer.  */

void
buf_append_buffer (struct buffer *to, struct buffer *from)
{
    buf_append_data (to, from->data, from->last);
    from->data = NULL;
    from->last = NULL;
}

/*
 * Copy the contents of file F into buffer_data structures.  We can't
 * copy directly into an buffer, because we want to handle failure and
 * succeess differently.  Returns 0 on success, or -2 if out of
 * memory, or a status code on error.  Since the caller happens to
 * know the size of the file, it is passed in as SIZE.  On success,
 * this function sets *RETP and *LASTP, which may be passed to
 * buf_append_data.
 */

int
buf_read_file (FILE *f, long int size, struct buffer_data **retp, struct buffer_data **lastp)
{
    int status;

    *retp = NULL;
    *lastp = NULL;

    while (size > 0)
    {
	struct buffer_data *data;
	int get;

	data = get_buffer_data ();
	if (data == NULL)
	{
	    status = -2;
	    goto error_return;
	}

	if (*retp == NULL)
	    *retp = data;
	else
	    (*lastp)->next = data;
	data->next = NULL;
	*lastp = data;

	data->bufp = data->text;
	data->size = 0;

	if (size > BUFFER_DATA_SIZE)
	    get = BUFFER_DATA_SIZE;
	else
	    get = size;

	errno = EIO;
	if (fread (data->text, get, 1, f) != 1)
	{
	    status = errno;
	    goto error_return;
	}

	data->size += get;
	size -= get;
    }

    return 0;

  error_return:
    if (*retp != NULL)
    {
	(*lastp)->next = free_buffer_data;
	free_buffer_data = *retp;
    }
    return status;
}

/*
 * Copy the contents of file F into buffer_data structures.  We can't
 * copy directly into an buffer, because we want to handle failure and
 * succeess differently.  Returns 0 on success, or -2 if out of
 * memory, or a status code on error.  On success, this function sets
 * *RETP and *LASTP, which may be passed to buf_append_data.
 */

int
buf_read_file_to_eof (FILE *f, struct buffer_data **retp, struct buffer_data **lastp)
{
    int status;

    *retp = NULL;
    *lastp = NULL;

    while (!feof (f))
    {
	struct buffer_data *data;
	int get, nread;

	data = get_buffer_data ();
	if (data == NULL)
	{
	    status = -2;
	    goto error_return;
	}

	if (*retp == NULL)
	    *retp = data;
	else
	    (*lastp)->next = data;
	data->next = NULL;
	*lastp = data;

	data->bufp = data->text;
	data->size = 0;

	get = BUFFER_DATA_SIZE;

	errno = EIO;
	nread = fread (data->text, 1, get, f);
	if (nread == 0 && !feof (f))
	{
	    status = errno;
	    goto error_return;
	}

	data->size = nread;
    }

    return 0;

  error_return:
    if (*retp != NULL)
    {
	(*lastp)->next = free_buffer_data;
	free_buffer_data = *retp;
    }
    return status;
}

/* Return the number of bytes in a chain of buffer_data structures.  */

int
buf_chain_length (struct buffer_data *buf)
{
    int size = 0;
    while (buf)
    {
	size += buf->size;
	buf = buf->next;
    }
    return size;
}

/* Return the number of bytes in a buffer.  */

int
buf_length (struct buffer *buf)
{
    return buf_chain_length (buf->data);
}

/*
 * Read an arbitrary amount of data into an input buffer.  The buffer
 * will be in nonblocking mode, and we just grab what we can.  Return
 * 0 on success, or -1 on end of file, or -2 if out of memory, or an
 * error code.  If COUNTP is not NULL, *COUNTP is set to the number of
 * bytes read.
 */

int
buf_input_data (struct buffer *buf, int *countp)
{
    if (buf->input == NULL)
	abort ();

    if (countp != NULL)
	*countp = 0;

    while (1)
    {
	int get;
	int status, nbytes;

	if (buf->data == NULL
	    || (buf->last->bufp + buf->last->size
		== buf->last->text + BUFFER_DATA_SIZE))
	{
	    struct buffer_data *data;

	    data = get_buffer_data ();
	    if (data == NULL)
	    {
		(*buf->memory_error) (buf);
		return -2;
	    }

	    if (buf->data == NULL)
		buf->data = data;
	    else
		buf->last->next = data;
	    data->next = NULL;
	    buf->last = data;

	    data->bufp = data->text;
	    data->size = 0;
	}

	get = ((buf->last->text + BUFFER_DATA_SIZE)
	       - (buf->last->bufp + buf->last->size));

	status = (*buf->input) (buf->closure,
				buf->last->bufp + buf->last->size,
				0, get, &nbytes);
	if (status != 0)
	    return status;

	buf->last->size += nbytes;
	if (countp != NULL)
	    *countp += nbytes;

	if (nbytes < get)
	{
	    /* If we did not fill the buffer, then presumably we read
               all the available data.  */
	    return 0;
	}
    }

    /*NOTREACHED*/
}

/*
 * Read a line (characters up to a \n) from an input buffer.  (We
 * use \n rather than \n for the benefit of non Unix clients for
 * which \n means something else).  This returns 0 on success, or -1
 * on end of file, or -2 if out of memory, or an error code.  If it
 * succeeds, it sets *LINE to an allocated buffer holding the contents
 * of the line.  The trailing \n is not included in the buffer.  If
 * LENP is not NULL, then *LENP is set to the number of bytes read;
 * strlen may not work, because there may be embedded null bytes.
 */

int
buf_read_line (struct buffer *buf, char **line, int *lenp)
{
    if (buf->input == NULL)
        abort ();

    *line = NULL;

    while (1)
    {
	int len, finallen = 0;
	struct buffer_data *data;
	char *nl;

	/* See if there is a newline in BUF.  */
	len = 0;
	for (data = buf->data; data != NULL; data = data->next)
	{
	    nl = (char*)memchr (data->bufp, '\n', data->size);
	    if (nl != NULL)
	    {
	        finallen = nl - data->bufp;
	        len += finallen;
		break;
	    }
	    len += data->size;
	}

	/* If we found a newline, copy the line into a memory buffer,
           and remove it from BUF.  */
	if (data != NULL)
	{
	    char *p;
	    struct buffer_data *nldata;

	    p = (char*)xmalloc (len + 1);
	    if (p == NULL)
		return -2;
	    *line = p;

	    nldata = data;
	    data = buf->data;
	    while (data != nldata)
	    {
		struct buffer_data *next;

		memcpy (p, data->bufp, data->size);
		p += data->size;
		next = data->next;
		data->next = free_buffer_data;
		free_buffer_data = data;
		data = next;
	    }

	    memcpy (p, data->bufp, finallen);
	    p[finallen] = '\0';

	    data->size -= finallen + 1;
	    data->bufp = nl + 1;
	    buf->data = data;

	    if (lenp != NULL)
	        *lenp = len;

	    return 0;
	}

	/* Read more data until we get a newline.  */
	while (1)
	{
	    int size, status, nbytes;
	    char *mem;

	    if (buf->data == NULL
		|| (buf->last->bufp + buf->last->size
		    == buf->last->text + BUFFER_DATA_SIZE))
	    {
		data = get_buffer_data ();
		if (data == NULL)
		{
		    (*buf->memory_error) (buf);
		    return -2;
		}

		if (buf->data == NULL)
		    buf->data = data;
		else
		    buf->last->next = data;
		data->next = NULL;
		buf->last = data;

		data->bufp = data->text;
		data->size = 0;
	    }

	    mem = buf->last->bufp + buf->last->size;
	    size = (buf->last->text + BUFFER_DATA_SIZE) - mem;

	    /* We need to read at least 1 byte.  We can handle up to
               SIZE bytes.  This will only be efficient if the
               underlying communication stream does its own buffering,
               or is clever about getting more than 1 byte at a time.  */
	    status = (*buf->input) (buf->closure, mem, 1, size, &nbytes);
	    if (status != 0)
		return status;

	    buf->last->size += nbytes;

	    /* Optimize slightly to avoid an unnecessary call to
               memchr.  */
	    if (nbytes == 1)
	    {
		if (*mem == '\n')
		    break;
	    }
	    else
	    {
		if (memchr (mem, '\n', nbytes) != NULL)
		    break;
	    }
	}
    }
}

/*
 * Extract data from the input buffer BUF.  This will read up to WANT
 * bytes from the buffer.  It will set *RETDATA to point at the bytes,
 * and set *GOT to the number of bytes to be found there.  Any buffer
 * call which uses BUF may change the contents of the buffer at *DATA,
 * so the data should be fully processed before any further calls are
 * made.  This returns 0 on success, or -1 on end of file, or -2 if
 * out of memory, or an error code.
 */

int
buf_read_data (struct buffer *buf, int want, char **retdata, int *got)
{
    if (buf->input == NULL)
	abort ();

    while (buf->data != NULL && buf->data->size == 0)
    {
	struct buffer_data *next;

	next = buf->data->next;
	buf->data->next = free_buffer_data;
	free_buffer_data = buf->data;
	buf->data = next;
	if (next == NULL)
	    buf->last = NULL;
    }

    if (buf->data == NULL)
    {
	struct buffer_data *data;
	int get, status, nbytes;

	data = get_buffer_data ();
	if (data == NULL)
	{
	    (*buf->memory_error) (buf);
	    return -2;
	}

	buf->data = data;
	buf->last = data;
	data->next = NULL;
	data->bufp = data->text;
	data->size = 0;

	if (want < BUFFER_DATA_SIZE)
	    get = want;
	else
	    get = BUFFER_DATA_SIZE;
	status = (*buf->input) (buf->closure, data->bufp, get,
				BUFFER_DATA_SIZE, &nbytes);
	if (status != 0)
	    return status;

	data->size = nbytes;
    }

    *retdata = buf->data->bufp;
    if (want < buf->data->size)
    {
        *got = want;
	buf->data->size -= want;
	buf->data->bufp += want;
    }
    else
    {
        *got = buf->data->size;
	buf->data->size = 0;
    }

    return 0;
}

/*
 * Copy lines from an input buffer to an output buffer.  This copies
 * all complete lines (characters up to a newline) from INBUF to
 * OUTBUF.  Each line in OUTBUF is preceded by the character COMMAND
 * and a space.
 */

void
buf_copy_lines (struct buffer *outbuf, struct buffer *inbuf, int command)
{
    while (1)
    {
	struct buffer_data *data;
	struct buffer_data *nldata;
	char *nl;
	int len;

	/* See if there is a newline in INBUF.  */
	nldata = NULL;
	nl = NULL;
	for (data = inbuf->data; data != NULL; data = data->next)
	{
	    nl = (char*)memchr (data->bufp, '\n', data->size);
	    if (nl != NULL)
	    {
		nldata = data;
		break;
	    }
	}

	if (nldata == NULL)
	{
	    /* There are no more lines in INBUF.  */
	    return;
	}

	/* Put in the command.  */
	buf_append_char (outbuf, command);
	buf_append_char (outbuf, ' ');

	if (inbuf->data != nldata)
	{
	    /*
	     * Simply move over all the buffers up to the one containing
	     * the newline.
	     */
	    for (data = inbuf->data; data->next != nldata; data = data->next)
		;
	    data->next = NULL;
	    buf_append_data (outbuf, inbuf->data, data);
	    inbuf->data = nldata;
	}

	/*
	 * If the newline is at the very end of the buffer, just move
	 * the buffer onto OUTBUF.  Otherwise we must copy the data.
	 */
	len = nl + 1 - nldata->bufp;
	if (len == nldata->size)
	{
	    inbuf->data = nldata->next;
	    if (inbuf->data == NULL)
		inbuf->last = NULL;

	    nldata->next = NULL;
	    buf_append_data (outbuf, nldata, nldata);
	}
	else
	{
	    buf_output (outbuf, nldata->bufp, len);
	    nldata->bufp += len;
	    nldata->size -= len;
	}
    }
}

/*
 * Copy counted data from one buffer to another.  The count is an
 * integer, host size, host byte order (it is only used across a
 * pipe).  If there is enough data, it should be moved over.  If there
 * is not enough data, it should remain on the original buffer.  A
 * negative count is a special case.  if one is seen, *SPECIAL is set
 * to the (negative) count value and no additional data is gathered
 * from the buffer; normally *SPECIAL is set to 0.  This function
 * returns the number of bytes it needs to see in order to actually
 * copy something over.
 */

int
buf_copy_counted (struct buffer *outbuf, struct buffer *inbuf, int *special)
{
    *special = 0;

    while (1)
    {
	struct buffer_data *data;
	int need;
	union
	{
	    char intbuf[sizeof (int)];
	    int i;
	} u;
	char *intp;
	int count;
	struct buffer_data *start;
	int startoff;
	struct buffer_data *stop;
	int stopwant;

	/* See if we have enough bytes to figure out the count.  */
	need = sizeof (int);
	intp = u.intbuf;
	for (data = inbuf->data; data != NULL; data = data->next)
	{
	    if (data->size >= need)
	    {
		memcpy (intp, data->bufp, need);
		break;
	    }
	    memcpy (intp, data->bufp, data->size);
	    intp += data->size;
	    need -= data->size;
	}
	if (data == NULL)
	{
	    /* We don't have enough bytes to form an integer.  */
	    return need;
	}

	count = u.i;
	start = data;
	startoff = need;

	if (count < 0)
	{
	    /* A negative COUNT is a special case meaning that we
               don't need any further information.  */
	    stop = start;
	    stopwant = 0;
	}
	else
	{
	    /*
	     * We have an integer in COUNT.  We have gotten all the
	     * data from INBUF in all buffers before START, and we
	     * have gotten STARTOFF bytes from START.  See if we have
	     * enough bytes remaining in INBUF.
	     */
	    need = count - (start->size - startoff);
	    if (need <= 0)
	    {
		stop = start;
		stopwant = count;
	    }
	    else
	    {
		for (data = start->next; data != NULL; data = data->next)
		{
		    if (need <= data->size)
			break;
		    need -= data->size;
		}
		if (data == NULL)
		{
		    /* We don't have enough bytes.  */
		    return need;
		}
		stop = data;
		stopwant = need;
	    }
	}

	/*
	 * We have enough bytes.  Free any buffers in INBUF before
	 * START, and remove STARTOFF bytes from START, so that we can
	 * forget about STARTOFF.
	 */
	start->bufp += startoff;
	start->size -= startoff;

	if (start->size == 0)
	    start = start->next;

	if (stop->size == stopwant)
	{
	    stop = stop->next;
	    stopwant = 0;
	}

	while (inbuf->data != start)
	{
	    data = inbuf->data;
	    inbuf->data = data->next;
	    data->next = free_buffer_data;
	    free_buffer_data = data;
	}

	/* If COUNT is negative, set *SPECIAL and get out now.  */
	if (count < 0)
	{
	    *special = count;
	    return 0;
	}

	/*
	 * We want to copy over the bytes from START through STOP.  We
	 * only want STOPWANT bytes from STOP.
	 */

	if (start != stop)
	{
	    /* Attach the buffers from START through STOP to OUTBUF.  */
	    for (data = start; data->next != stop; data = data->next)
		;
	    inbuf->data = stop;
	    data->next = NULL;
	    buf_append_data (outbuf, start, data);
	}

	if (stopwant > 0)
	{
	    buf_output (outbuf, stop->bufp, stopwant);
	    stop->bufp += stopwant;
	    stop->size -= stopwant;
	}
    }

    /*NOTREACHED*/
}

/* Shut down a buffer.  This returns 0 on success, or an errno code.  */

int
buf_shutdown (struct buffer *buf)
{
    if (buf->shutdown)
		return (*buf->shutdown) (buf->closure);
    return 0;
}

/* The simplest type of buffer is one built on top of a stdio FILE.
   For simplicity, and because it is all that is required, we do not
   implement setting this type of buffer into nonblocking mode.  The
   closure field is just a FILE *.  */

static int stdio_buffer_input(void *, char *, int, int, int *);
static int stdio_buffer_output(void *, const char *, int, int *);
static int stdio_buffer_flush(void *);

/* Initialize a buffer built on a stdio FILE.  */

struct buffer *
stdio_buffer_initialize (FILE *fp, int input, void (*memory) (struct buffer *))
{
    return buf_initialize (input ? stdio_buffer_input : NULL,
			   input ? NULL : stdio_buffer_output,
			   input ? NULL : stdio_buffer_flush,
			   (int (*) (void *, int)) NULL,
			   (int (*) (void *)) NULL,
			   memory,
			   (void *) fp);
}

/* The buffer input function for a buffer built on a stdio FILE.  */

static int
stdio_buffer_input (void *closure, char *data, int need, int size, int *got)
{
    FILE *fp = (FILE *) closure;
    int nbytes;

    /* Since stdio does its own buffering, we don't worry about
       getting more bytes than we need.  */

    if (need == 0 || need == 1)
    {
        int ch;

	ch = getc (fp);

	if (ch == EOF)
	{
	    if (feof (fp))
		return -1;
	    else if (errno == 0)
		return EIO;
	    else
		return errno;
	}

	*data = ch;
	*got = 1;
	return 0;
    }

    nbytes = fread (data, 1, need, fp);

    if (nbytes == 0)
    {
	*got = 0;
	if (feof (fp))
	    return -1;
	else if (errno == 0)
	    return EIO;
	else
	    return errno;
    }

    *got = nbytes;

    return 0;
}

/* The buffer output function for a buffer built on a stdio FILE.  */

static int
stdio_buffer_output (void *closure, const char *data, int have, int *wrote)
{
    FILE *fp = (FILE *) closure;

    *wrote = 0;

    while (have > 0)
    {
	int nbytes;

	nbytes = fwrite (data, 1, have, fp);

	if (nbytes != have)
	{
	    if (errno == 0)
		return EIO;
	    else
		return errno;
	}

	*wrote += nbytes;
	have -= nbytes;
	data += nbytes;
    }

    return 0;
}

/* The buffer flush function for a buffer built on a stdio FILE.  */

static int
stdio_buffer_flush (void *closure)
{
    FILE *fp = (FILE *) closure;

    if (fflush (fp) != 0)
    {
	if (errno == 0)
	    return EIO;
	else
	    return errno;
    }

    return 0;
}

/* This structure is the closure field of a packetizing buffer.  */

struct packetizing_buffer
{
    /* The underlying buffer.  */
    struct buffer *buf;
    /* The input translation function.  Exactly one of inpfn and outfn
       will be NULL.  The input translation function should
       untranslate the data in INPUT, storing the result in OUTPUT.
       SIZE is the amount of data in INPUT, and is also the size of
       OUTPUT.  This should return 0 on success, or an errno code.  */
    int (*inpfn) (void *fnclosure, const char *input, char *output,
			int size);
    /* The output translation function.  This should translate the
       data in INPUT, storing the result in OUTPUT.  The first two
       bytes in INPUT will be the size of the data, and so will SIZE.
       This should set *TRANSLATED to the amount of translated data in
       OUTPUT.  OUTPUT is large enough to hold SIZE + PACKET_SLOP
       bytes.  This should return 0 on success, or an errno code.  */
    int (*outfn) (void *fnclosure, const char *input, char *output,
			int size, int *translated);
    /* A closure for the translation function.  */
    void *fnclosure;
    /* For an input buffer, we may have to buffer up data here.  */
    /* This is non-zero if the buffered data has been translated.
       Otherwise, the buffered data has not been translated, and starts
       with the two byte packet size.  */
    int translated;
    /* The amount of buffered data.  */
    int holdsize;
    /* The buffer allocated to hold the data.  */
    char *holdbuf;
    /* The size of holdbuf.  */
    int holdbufsize;
    /* If translated is set, we need another data pointer to track
       where we are in holdbuf.  If translated is clear, then this
       pointer is not used.  */
    char *holddata;
};

static int packetizing_buffer_input(void *, char *, int, int, int *);
static int packetizing_buffer_output(void *, const char *, int, int *);
static int nonpacketizing_buffer_output(void *, const char *, int, int *);
static int packetizing_buffer_flush(void *);
static int packetizing_buffer_block(void *, int);
static int packetizing_buffer_shutdown(void *);

/* Create a packetizing buffer.  */

struct buffer *
packetizing_buffer_initialize (struct buffer *buf, int (*inpfn) (void *, const char *, char *, int), int (*outfn) (void *, const char *, char *, int, int *), void *fnclosure, void (*memory) (struct buffer *))
{
    struct packetizing_buffer *pb;

    pb = (struct packetizing_buffer *) xmalloc (sizeof *pb);
    memset (pb, 0, sizeof *pb);

    pb->buf = buf;
    pb->inpfn = inpfn;
    pb->outfn = outfn;
    pb->fnclosure = fnclosure;

    if (inpfn != NULL)
    {
	/* Add PACKET_SLOP to handle larger translated packets, and
           add 2 for the count.  This buffer is increased if
           necessary.  */
	pb->holdbufsize = BUFFER_DATA_SIZE + PACKET_SLOP + 2;
	pb->holdbuf = (char*)xmalloc (pb->holdbufsize);
    }

    return buf_initialize (inpfn != NULL ? packetizing_buffer_input : NULL,
			   outfn != NULL ? packetizing_buffer_output : NULL,
			   outfn != NULL ? packetizing_buffer_flush : NULL,
			   packetizing_buffer_block,
			   packetizing_buffer_shutdown,
			   memory,
			   pb);
}

/* Create a nonpacketizing buffer - same as above except it's output only
   and doesn't wrap its output params */

struct buffer *
nonpacketizing_buffer_initialize (struct buffer *buf, int (*inpfn) (void *, const char *, char *, int), int (*outfn) (void *, const char *, char *, int, int *), void *fnclosure, void (*memory) (struct buffer *))
{
    struct packetizing_buffer *pb;

    pb = (struct packetizing_buffer *) xmalloc (sizeof *pb);
    memset (pb, 0, sizeof *pb);

    pb->buf = buf;
    pb->inpfn = NULL;
    pb->outfn = outfn;
    pb->fnclosure = fnclosure;

    return buf_initialize (NULL,
			   outfn != NULL ? nonpacketizing_buffer_output : NULL,
			   outfn != NULL ? packetizing_buffer_flush : NULL,
			   packetizing_buffer_block,
			   packetizing_buffer_shutdown,
			   memory,
			   pb);
}

void packetizing_buffer_set_wrap(struct buffer *buf, struct buffer *wrap)
{
	struct packetizing_buffer *pb = (struct packetizing_buffer *)buf->closure;

	pb->buf = wrap;
}


/* Input data from a packetizing buffer.  */

static int
packetizing_buffer_input (void *closure, char *data, int need, int size, int *got)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;

    *got = 0;

    if (pb->holdsize > 0 && pb->translated)
    {
	int copy;

	copy = pb->holdsize;

	if (copy > size)
	{
	    memcpy (data, pb->holddata, size);
	    pb->holdsize -= size;
	    pb->holddata += size;
	    *got = size;
	    return 0;
	}

	memcpy (data, pb->holddata, copy);
	pb->holdsize = 0;
	pb->translated = 0;

	data += copy;
	need -= copy;
	size -= copy;
	*got = copy;
    }

    while (need > 0 || *got == 0)
    {
	int get, status, nread, count, tcount;
	char *bytes;
	char stackoutbuf[BUFFER_DATA_SIZE + PACKET_SLOP];
	char *inbuf, *outbuf;

	/* If we don't already have the two byte count, get it.  */
	if (pb->holdsize < 2)
	{
	    get = 2 - pb->holdsize;
	    status = buf_read_data (pb->buf, get, &bytes, &nread);
	    if (status != 0)
	    {
		/* buf_read_data can return -2, but a buffer input
                   function is only supposed to return -1, 0, or an
                   error code.  */
		if (status == -2)
		    status = ENOMEM;
		return status;
	    }

	    if (nread == 0)
	    {
		/* The buffer is in nonblocking mode, and we didn't
                   manage to read anything.  */
		return 0;
	    }

		if (get == 1)
			pb->holdbuf[1] = bytes[0];
		else
		{
			pb->holdbuf[0] = bytes[0];
			if (nread < 2)
			{
				/* We only got one byte, but we needed two.  Stash
						   the byte we got, and try again.  */
				pb->holdsize = 1;
				continue;
			}
			pb->holdbuf[1] = bytes[1];
		}
		pb->holdsize = 2;
	}

	/* Read the packet.  */

	count = (((pb->holdbuf[0] & 0xff) << 8)
		 + (pb->holdbuf[1] & 0xff));

	if (count + 2 > pb->holdbufsize)
	{
	    char *n;

	    /* We didn't allocate enough space in the initialize
               function.  */

	    n = (char*)xrealloc(pb->holdbuf, count + 2);
	    if (n == NULL)
	    {
		(*pb->buf->memory_error) (pb->buf);
		return ENOMEM;
	    }
	    pb->holdbuf = n;
	    pb->holdbufsize = count + 2;
	}

	get = count - (pb->holdsize - 2);

	status = buf_read_data (pb->buf, get, &bytes, &nread);
	if (status != 0)
	{
	    /* buf_read_data can return -2, but a buffer input
               function is only supposed to return -1, 0, or an error
               code.  */
	    if (status == -2)
		status = ENOMEM;
	    return status;
	}

	if (nread == 0)
	{
	    /* We did not get any data.  Presumably the buffer is in
               nonblocking mode.  */
	    return 0;
	}

	if (nread < get)
	{
	    /* We did not get all the data we need to fill the packet.
               buf_read_data does not promise to return all the bytes
               requested, so we must try again.  */
	    memcpy (pb->holdbuf + pb->holdsize, bytes, nread);
	    pb->holdsize += nread;
	    continue;
	}

	/* We have a complete untranslated packet of COUNT bytes.  */

	if (pb->holdsize == 2)
	{
	    /* We just read the entire packet (the 2 bytes in
               PB->HOLDBUF are the size).  Save a memcpy by
               translating directly from BYTES.  */
	    inbuf = bytes;
	}
	else
	{
	    /* We already had a partial packet in PB->HOLDBUF.  We
               need to copy the new data over to make the input
               contiguous.  */
	    memcpy (pb->holdbuf + pb->holdsize, bytes, nread);
	    inbuf = pb->holdbuf + 2;
	}

	if (count <= sizeof stackoutbuf)
	    outbuf = stackoutbuf;
	else
	{
	    outbuf = (char*)xmalloc (count);
	    if (outbuf == NULL)
	    {
		(*pb->buf->memory_error) (pb->buf);
		return ENOMEM;
	    }
	}

	status = (*pb->inpfn) (pb->fnclosure, inbuf, outbuf, count);
	if (status != 0)
	    return status;

	/* The first two bytes in the translated buffer are the real
           length of the translated data.  */
	tcount = ((outbuf[0] & 0xff) << 8) + (outbuf[1] & 0xff);

	if (tcount > count)
		error (1, 0, "Input failure: data packet recieved is too short");

	if (tcount > size)
	{
	    /* We have more data than the caller has provided space
               for.  We need to save some of it for the next call.  */

	    memcpy (data, outbuf + 2, size);
	    *got += size;

	    pb->holdsize = tcount - size;
	    memcpy (pb->holdbuf, outbuf + 2 + size, tcount - size);
	    pb->holddata = pb->holdbuf;
	    pb->translated = 1;

	    if (outbuf != stackoutbuf)
		xfree (outbuf);

	    return 0;
	}

	memcpy (data, outbuf + 2, tcount);

	if (outbuf != stackoutbuf)
	    xfree (outbuf);

	pb->holdsize = 0;

	data += tcount;
	need -= tcount;
	size -= tcount;
	*got += tcount;
    }

    return 0;
}

/* Output data to a packetizing buffer.  */

static int
packetizing_buffer_output (void *closure, const char *data, int have, int *wrote)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;
    char inbuf[BUFFER_DATA_SIZE + 2];
    char stack_outbuf[BUFFER_DATA_SIZE + PACKET_SLOP + 4];
    struct buffer_data *outdata;
    char *outbuf;
    int size, status, translated;

    if (have > BUFFER_DATA_SIZE)
    {
	/* It would be easy to malloc a buffer, but I don't think this
           case can ever arise.  */
	abort ();
    }

	inbuf[0] = (have >> 8) & 0xff;
	inbuf[1] = have & 0xff;
	memcpy (inbuf + 2, data, have);

	size = have + 2;

    /* The output function is permitted to add up to PACKET_SLOP
       bytes, and we need 2 bytes for the size of the translated data.
       If we can guarantee that the result will fit in a buffer_data,
       we translate directly into one to avoid a memcpy in buf_output.  */
    if (size + PACKET_SLOP + 2 > BUFFER_DATA_SIZE)
		outbuf = stack_outbuf;
    else
    {
		outdata = get_buffer_data ();
		if (outdata == NULL)
		{
			(*pb->buf->memory_error) (pb->buf);
			return ENOMEM;
		}

		outdata->next = NULL;
		outdata->bufp = outdata->text;

		outbuf = outdata->text;
    }

    status = (*pb->outfn) (pb->fnclosure, inbuf, outbuf + 2, size,
			   &translated);
    if (status != 0)
		return status;

    /* The output function is permitted to add up to PACKET_SLOP
       bytes.  */
    if (translated > size + PACKET_SLOP)
		abort ();

    outbuf[0] = (translated >> 8) & 0xff;
    outbuf[1] = translated & 0xff;

    if (outbuf == stack_outbuf)
		buf_output (pb->buf, outbuf, translated + 2);
    else
    {
		outdata->size = translated + 2;
		buf_append_data (pb->buf, outdata, outdata);
    }

    *wrote = have;

    /* We will only be here because buf_send_output was called on the
       packetizing buffer.  That means that we should now call
       buf_send_output on the underlying buffer.  */
	return buf_send_output (pb->buf);
}

/* Output data to a nonpacketizing buffer.  */

static int
nonpacketizing_buffer_output (void *closure, const char *data, int have, int *wrote)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;
    char stack_outbuf[BUFFER_DATA_SIZE + PACKET_SLOP + 4];
    struct buffer_data *outdata;
    char *outbuf;
    int status, translated;

    if (have > BUFFER_DATA_SIZE)
    {
	/* It would be easy to malloc a buffer, but I don't think this
           case can ever arise.  */
	abort ();
    }

    /* The output function is permitted to add up to PACKET_SLOP
       bytes, and we need 2 bytes for the size of the translated data.
       If we can guarantee that the result will fit in a buffer_data,
       we translate directly into one to avoid a memcpy in buf_output.  */
    if (have + PACKET_SLOP + 2 > BUFFER_DATA_SIZE)
		outbuf = stack_outbuf;
    else
    {
		outdata = get_buffer_data ();
		if (outdata == NULL)
		{
			(*pb->buf->memory_error) (pb->buf);
			return ENOMEM;
		}

		outdata->next = NULL;
		outdata->bufp = outdata->text;

		outbuf = outdata->text;
    }

    status = (*pb->outfn) (pb->fnclosure, data, outbuf, have,
			   &translated);
    if (status != 0)
		return status;

    /* The output function is permitted to add up to PACKET_SLOP
       bytes.  */
    if (translated > have + PACKET_SLOP)
		abort ();

    if (outbuf == stack_outbuf)
		buf_output (pb->buf, outbuf, translated );
    else
    {
		outdata->size = translated;
		buf_append_data (pb->buf, outdata, outdata);
    }

    *wrote = have;

    /* We will only be here because buf_send_output was called on the
       packetizing buffer.  That means that we should now call
       buf_send_output on the underlying buffer.  */
	return buf_send_output (pb->buf);
}

/* Flush data to a packetizing buffer.  */

static int
packetizing_buffer_flush (void *closure)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;

    /* Flush the underlying buffer.  Note that if the original call to
       buf_flush passed 1 for the BLOCK argument, then the buffer will
       already have been set into blocking mode, so we should always
       pass 0 here.  */
    return buf_flush (pb->buf, 0);
}

/* The block routine for a packetizing buffer.  */

static int
packetizing_buffer_block (void *closure, int block)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;

    if (block)
	return set_block (pb->buf);
    else
	return set_nonblock (pb->buf);
}

/* Shut down a packetizing buffer.  */

static int
packetizing_buffer_shutdown (void *closure)
{
    struct packetizing_buffer *pb = (struct packetizing_buffer *) closure;

    return buf_shutdown (pb->buf);
}

void buf_md5(struct buffer *buf, CMD5Calc* md5)
{
	struct buffer_data *data = buf->data;
	while(data)
	{
		md5->Update(data->bufp,data->size);
		data=data->next;
	}
}

