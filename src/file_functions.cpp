#include "ccextractor.h"

#define FILEBUFFERSIZE 1024*1024*16 // 16 Mbytes no less. Minimize number of real read calls()

LLONG getfilesize (int in)
{
    LLONG current=LSEEK (in, 0, SEEK_CUR);
    LLONG length = LSEEK (in,0,SEEK_END);    
    LSEEK (in,current,SEEK_SET);
    return length;
}

LLONG gettotalfilessize (void) // -1 if one or more files failed to open
{
    LLONG ts=0;
    int h;
    for (int i=0;i<num_input_files;i++)
    {
		if (0 == strcmp(inputfile[i],"-")) // Skip stdin
			continue; 
#ifdef _WIN32
        h=OPEN (inputfile[i],O_RDONLY | O_BINARY); 
#else
        h=OPEN (inputfile[i],O_RDONLY); 
#endif
        if (h==-1)
        {
            mprint ("\rUnable to open %s\r\n",inputfile[i]);
            return -1;
        }
        if (!live_stream)
            ts+=getfilesize (h);
        close (h);
    }
    return ts;
}

void prepare_for_new_file (void)
{
    // Init per file variables
    min_pts=0x01FFFFFFFFLL; // 33 bit
    sync_pts=0;
    pts_set = 0;
    // inputsize=0; Now responsibility of switch_to_next_file()
    last_reported_progress=-1;
    stat_numuserheaders = 0;
    stat_dvdccheaders = 0;
    stat_scte20ccheaders = 0;
    stat_replay5000headers = 0;
    stat_replay4000headers = 0;
    stat_dishheaders = 0;
    stat_hdtv = 0;
    stat_divicom = 0;
    total_frames_count = 0;
    total_pulldownfields = 0;
    total_pulldownframes = 0;
    cc_stats[0]=0; cc_stats[1]=0; cc_stats[2]=0; cc_stats[3]=0;
    false_pict_header=0;
    frames_since_last_gop=0;
    frames_since_ref_time=0;
    gop_time.inited=0;
    first_gop_time.inited=0;
    gop_rollover=0;
    printed_gop.inited=0;
    saw_caption_block=0;
    past=0;
    pts_big_change=0;
    startbytes_pos=0;
    startbytes_avail=0;
    init_file_buffer();
    anchor_hdcc(-1);
    firstcall = 1;
}

/* Close input file if there is one and let the GUI know */
void close_input_file (void)
{
    if (infd!=-1 && !input_is_stdin)
    {
		close (infd);
        infd=-1;        
        activity_input_file_closed();        
    }
}


/* Close current file and open next one in list -if any- */
/* bytesinbuffer is the number of bytes read (in some buffer) that haven't been added
to 'past' yet. We provide this number to switch_to_next_file() so a final sanity check
can be done */

int switch_to_next_file (LLONG bytesinbuffer)
{
	if (current_file==-1 || !binary_concat)
		memset (PIDs_seen,0,65536*sizeof (int));

	if (input_is_stdin)
	{
		if (infd!=-1) // Means we had already processed stdin. So we're done.
			return 0;
		infd=0;
		mprint ("\n\r-----------------------------------------------------------------\n");
		mprint ("\rReading from standard input\n");
		return 1;
	}
    /* Close current and make sure things are still sane */
    if (infd!=-1)
    {
        close_input_file ();
        if (inputsize>0 && ((past+bytesinbuffer) < inputsize) && !processed_enough)
        {
            mprint("\n\n\n\nATTENTION!!!!!!\n");
            mprint("In switch_to_next_file(): Processing of %s %d ended prematurely %lld < %lld, please send bug report.\n\n",
                   inputfile[current_file], current_file, past, inputsize);
        }
        if (binary_concat)
        {
            total_past+=inputsize;
            past=0; // Reset always or at the end we'll have double the size
        }
    }
    for (;;)
    {        
        current_file++;
        if (current_file>=num_input_files)
            break;

        // The following \n keeps the progress percentage from being overwritten.
        mprint ("\n\r-----------------------------------------------------------------\n");
		mprint ("\rOpening file: %s\n", inputfile[current_file]);
#ifdef _WIN32
		infd=OPEN (inputfile[current_file],O_RDONLY | O_BINARY); 
#else
        infd=OPEN (inputfile[current_file],O_RDONLY); 
#endif
        if (infd == -1)        
            mprint ("\rWarning: Unable to open input file [%s]\n", inputfile[current_file]);
        else
        {
            activity_input_file_open (inputfile[current_file]);
            if (!live_stream)
            {
                inputsize = getfilesize (infd);
                if (!binary_concat)
                    total_inputsize=inputsize;
            }
            return 1; // Succeeded
        }
    }
    return 0; 
}

void position_sanity_check ()
{
#ifdef SANITY_CHECK
    if (in!=-1)
    {
        LLONG realpos=LSEEK (in,0,SEEK_CUR);
        if (realpos!=past-filebuffer_pos+bytesinbuffer)
        {
            fatal (EXIT_BUG_BUG, "Position desync, THIS IS A BUG. Real pos =%lld, past=%lld.\n",realpos,past);
        }
    } 
#endif
}


int init_file_buffer(void)
{
    filebuffer_start=0;
    filebuffer_pos=0;    
    if (filebuffer==NULL)
    {
        filebuffer=(unsigned char *) malloc (FILEBUFFERSIZE);
        bytesinbuffer=0;
    }
    if (filebuffer==NULL) 
    {
        fatal (EXIT_NOT_ENOUGH_MEMORY, "Not enough memory\n");        
    }
    return 0;
}

void buffered_seek (int offset)
{
    position_sanity_check();
    if (offset<0)
    {
        filebuffer_pos+=offset;
        if (filebuffer_pos<0)
        {
            // We got into the start buffer (hopefully)
            startbytes_pos+=filebuffer_pos;
            filebuffer_pos=0;
            if (startbytes_pos<0)
            {
                fatal (EXIT_BUG_BUG, "PANIC: Attempt to seek before buffer start, this is a bug!");
            }
        }
    }
    else
    {
        buffered_read_opt (NULL, offset);
        position_sanity_check();
    }
}

void sleepandchecktimeout (time_t start)
{
    if (input_is_stdin)
    {
		// CFS: Not 100% sure about this. Fine for files, not so sure what happens if stdin is 
		// real time input from hardware.
        sleep_secs (1);
        live_stream=0;
        return;
    }

    if (live_stream==-1) // Just sleep, no timeout to check
    {
        sleep_secs (1);
        return;
    }
    if (time(NULL)>start+live_stream) // More than live_stream seconds elapsed. No more live
        live_stream=0; 
    else
        sleep_secs(1);
}

/* This is the function to call when we need bytes and there's none in the buffers.
   If live_stream!=0 the function will not return at a EOF */

LLONG buffered_read_opt (unsigned char *buffer, unsigned int bytes)
{
    LLONG copied=0;
    position_sanity_check();
    time_t seconds=0;
    if (live_stream>0) 
        time (&seconds); 
    if (buffer_input || filebuffer_pos<bytesinbuffer)
    {            
        // Needs to return data from filebuffer_start+pos to filebuffer_start+pos+bytes-1;        
        int eof = (infd==-1); 

        while ((!eof || live_stream) && bytes)
        {   
            if (eof)
            {
                // No more data available inmediately, we sleep a while to give time
                // for the data to come up                
                sleepandchecktimeout (seconds);
            }
            size_t ready = bytesinbuffer-filebuffer_pos;        
            if (ready==0) // We really need to read more
            {
                if (!buffer_input)
                {
                    // We got in the buffering code because of the initial buffer for
                    // detection stuff. However we don't want more buffering so 
                    // we do the rest directly on the final buffer.
                    int i;
                    do
                    {
                        if (buffer!=NULL)
                        {
                            i=read (infd,buffer,bytes);
                            if( i == -1)
                                fatal (EXIT_READ_ERROR, "Error reading input file!\n");
                            buffer+=i;
                        }
                        else
                        {
                            LLONG op =LSEEK (infd,0,SEEK_CUR); // Get current pos
                            if (op+bytes<0) // Would mean moving beyond start of file: Not supported
                                return 0;
                            LLONG np =LSEEK (infd,bytes,SEEK_CUR); // Pos after moving
                            i=(int) (np-op);
                        }
                        if (i==0 && live_stream)
                        {
                            if (input_is_stdin)
                            {
                                live_stream = 0;
                                break;
                            }
                            else
                            {
                                 sleepandchecktimeout (seconds);
                            }
                        }
                        else
                        {
                            copied+=i;
                            bytes-=i;
                        }
                        
                    }
                    while ((i || live_stream || (binary_concat && switch_to_next_file(copied))) && bytes);
                    return copied;                                              
                }
                // Keep the last 8 bytes, so we have a guaranteed 
                // working seek (-8) - needed by mythtv.
                int keep = bytesinbuffer > 8 ? 8 : bytesinbuffer;
                memmove (filebuffer,filebuffer+(FILEBUFFERSIZE-keep),keep);

                int i=read (infd, filebuffer+keep,FILEBUFFERSIZE-keep);
                if( i == -1)
                    fatal (EXIT_READ_ERROR, "Error reading input file!\n");
                if (i==0)
                {
                    /* If live stream, don't try to switch - acknowledge eof here as it won't
                    cause a loop end */
                    if (live_stream || !(binary_concat && switch_to_next_file(copied)))
                        eof=1;
                }
                filebuffer_pos=keep;
                bytesinbuffer=(int) i+keep;
                ready=i;
            }
            int copy = (int) (ready>=bytes ? bytes:ready);
            if (copy)
            {
                if (buffer!=NULL)        
                {
                    memcpy (buffer, filebuffer+filebuffer_pos, copy); 
                    buffer+=copy;
                }
                filebuffer_pos+=copy;        
                bytes-=copy;
                copied+=copy;
            }
        }
        return copied;
    }
    else // Read without buffering    
    {
        
        if (buffer!=NULL)
        {
            int i;
            while (bytes>0 && infd!=-1 && 
                ((i=read(infd,buffer,bytes))!=0 || live_stream || (binary_concat && switch_to_next_file(copied))))
            {
                if( i == -1)
                    fatal (EXIT_READ_ERROR, "Error reading input file!\n");
                else if (i==0)
                    sleepandchecktimeout (seconds);
                else
                {
                    copied+=i;
                    bytes-=i;
                    buffer+=i;
                }
            }
            return copied;
        }
        // return fread(buffer,1,bytes,in);
        //return FSEEK (in,bytes,SEEK_CUR);
        while (bytes!=0 && infd!=-1)
        {
            LLONG op =LSEEK (infd,0,SEEK_CUR); // Get current pos
            if (op+bytes<0) // Would mean moving beyond start of file: Not supported
                return 0;
            LLONG np =LSEEK (infd,bytes,SEEK_CUR); // Pos after moving
            copied=copied+(np-op);
            bytes=bytes-(unsigned int) copied;
            if (copied==0)
            {
                if (live_stream)
                    sleepandchecktimeout (seconds);
                else
                {
                    if (binary_concat)
                        switch_to_next_file(0);
                    else
                        break;
                }
            }
        }
        return copied;
    }
}
