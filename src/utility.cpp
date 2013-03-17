#include "ccextractor.h"

static char *text;
static int text_size=0;

/* Alloc text space */
void resize_text()
{
	text_size=(!text_size)?1024:text_size*2;
	if (text)
		free (text);
	text=(char *) malloc (text_size);
	if (!text)
		fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory for text buffer.");
	memset (text,0,text_size);
}

/* Write formatted message to stderr and then exit. */
void fatal(int exit_code, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (gui_mode_reports)
        fprintf(stderr,"###MESSAGE#");
    else
        fprintf(stderr, "\rError: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);    
    exit(exit_code);
}

/* printf() for fd instead of FILE*, since dprintf is not portable */
void fdprintf (int fd, const char *fmt, ...)
{
     /* Guess we need no more than 100 bytes. */
     int n, size = 100;
     char *p, *np;
     va_list ap;

     if ((p = (char *) malloc (size)) == NULL)
        return;

     while (1) 
	 {
        /* Try to print in the allocated space. */
        va_start(ap, fmt);
        n = vsnprintf (p, size, fmt, ap);
        va_end(ap);
        /* If that worked, return the string. */
        if (n > -1 && n < size)
		{
			write (fd, p, n);			
			free (p);
            return;
		}
        /* Else try again with more space. */
        if (n > -1)    /* glibc 2.1 */
           size = n+1; /* precisely what is needed */
        else           /* glibc 2.0 */
           size *= 2;  /* twice the old size */
        if ((np = (char *) realloc (p, size)) == NULL) 
		{
           free(p);
           return ;
        } else {
           p = np;
        }
     }
}


/* General output, replacement for printf so we can control globally where messages go.
    mprint => Message print */
void mprint (const char *fmt, ...)
{
	va_list args;
	if (!messages_target)
		return;    
	activity_header(); // Brag about writing it :-)
    va_start(args, fmt);	
	if (messages_target==MESSAGES_STDOUT)
	{
		vfprintf(stdout, fmt, args);
		fflush (stdout);
	}
	else
	{
		vfprintf(stderr, fmt, args);
		fflush (stderr);
	}
    va_end(args);
}

/* Shorten some debug output code. */
void dbg_print(LLONG mask, const char *fmt, ...)
{
	va_list args;
	if (!messages_target)
		return;
	LLONG t=temp_debug ? (debug_mask_on_debug | debug_mask) : debug_mask; // Mask override?

    if(mask & t)
	{
	    va_start(args, fmt);
		if (messages_target==MESSAGES_STDOUT)
		{
			vfprintf(stdout, fmt, args);
			fflush (stdout);
		}
		else
		{
			vfprintf(stderr, fmt, args);
			fflush (stderr);
		}
		va_end(args);
	}
}


/* Shorten some debug output code. */
void dvprint(const char *fmt, ...)
{
    va_list args;
	if (!messages_target)
		return;
	if(! (debug_mask & DMT_VIDES ))
		return;

    va_start(args, fmt);
	if (messages_target==MESSAGES_STDOUT)
	{
		vfprintf(stdout, fmt, args);
		fflush (stdout);
	}
	else
	{
		vfprintf(stderr, fmt, args);	
		fflush (stderr);
	}
    va_end(args);
}

void dump (LLONG mask, unsigned char *start, int l, unsigned long abs_start, unsigned clear_high_bit)
{
	LLONG t=temp_debug ? (debug_mask_on_debug | debug_mask) : debug_mask; // Mask override?
    if(! (mask & t))
		return;

    for (int x=0; x<l; x=x+16)
    {
        mprint ("%08ld | ",x+abs_start);
        for (int j=0; j<16; j++)
        {
            if (x+j<l)
                mprint ("%02X ",start[x+j]);
            else
                mprint ("   ");
        }
        mprint (" | ");
        for (int j=0; j<16; j++)
        {
            if (x+j<=l && start[x+j]>=' ')
				mprint ("%c",start[x+j] & (clear_high_bit?0x7F:0xFF)); // 0x7F < remove high bit, convenient for visual CC inspection
            else
                mprint (" ");
        }
        mprint ("\n");
    }
}

void init_boundary_time (boundary_time *bt)
{
    bt->hh=0;
    bt->mm=0;
    bt->ss=0;
    bt->set=0;
    bt->time_in_ms=0;
}


void sleep_secs (int secs)
{
#ifdef _WIN32
	Sleep(secs * 1000);
#else
	sleep(secs);
#endif
}
