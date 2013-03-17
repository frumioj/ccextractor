#include "ccextractor.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "708.h" 

// IMPORTED TRASH INFO, REMOVE
extern long num_nal_unit_type_7;
extern long num_vcl_hrd;
extern long num_nal_hrd;
extern long num_jump_in_frames;
extern long num_unexpected_sei_length;

/* General video information */
unsigned current_hor_size = 0;
unsigned current_vert_size = 0;
unsigned current_aspect_ratio = 0;
unsigned current_frame_rate = 4; // Assume standard fps
double current_fps = framerates_values[current_frame_rate];
LLONG current_pts = 0;
LLONG result; // Number of bytes read/skipped in last read operation
int end_of_file=0; // End of file?


const unsigned char DO_NOTHING[] = {0x80, 0x80};
LLONG inbuf = 0; // Number of bytes loaded in buffer 
int bufferdatatype = PES; // Can be RAW, PES, H264 or Hauppage

int current_tref = 0; // Store temporal reference of current frame
int current_picture_coding_type = RESET_OR_UNKNOWN;

// Remember if the last header was valid. Used to suppress too much output
// and the expected unrecognized first header for TiVo files.
int strangeheader=0;

int non_compliant_DVD = 0; // Found extra captions in DVDs?

unsigned char *filebuffer;
LLONG filebuffer_start; // Position of buffer start relative to file
int filebuffer_pos; // Position of pointer relative to buffer start
int bytesinbuffer; // Number of bytes we actually have on buffer

LLONG process_raw_with_field (void);


// Program stream specific data grabber
LLONG ps_getmoredata(void)
{
    int enough = 0;
    int payload_read = 0;

    static unsigned vpesnum=0;

    unsigned char nextheader[512]; // Next header in PS
    int falsepack=0;

    // Read and return the next video PES payload
    do 
    {
        if (BUFSIZE-inbuf<500) 
        {
            mprint("Less than 500 left\n");
            enough=1; // Stop when less than 500 bytes are left in buffer
        }
        else 
        {
            buffered_read(nextheader,6);
            past+=result;
            if (result!=6) 
            {
                // Consider this the end of the show.
                end_of_file=1;
                break;
            }

            // Search for a header that is not a picture header (nextheader[3]!=0x00)
            while ( !(nextheader[0]==0x00 && nextheader[1]==0x00
                      && nextheader[2]==0x01 && nextheader[3]!=0x00) ) 
            {
                if( !strangeheader )
                {
                    mprint ("\nNot a recognized header. Searching for next header.\n");
                    dump (DMT_GENERIC_NOTICES, nextheader,6,0,0);
                    // Only print the message once per loop / unrecognized header
                    strangeheader = 1;
                }

                unsigned char *newheader;
                // The amount of bytes read into nextheader by the buffered_read above
                int hlen = 6;
                // Find first 0x00
                // If there is a 00 in the first element we need to advance
                // one step as clearly bytes 1,2,3 are wrong
                newheader = (unsigned char *) memchr (nextheader+1, 0, hlen-1);
                if (newheader != NULL )
                {
                    int atpos = newheader-nextheader;

                    memmove (nextheader,newheader,(size_t)(hlen-atpos)); 
                    buffered_read(nextheader+(hlen-atpos),atpos);
                    past+=result;
                    if (result!=atpos) 
                    {
                        end_of_file=1;
                        break;
                    }
                }
                else
                {
                    buffered_read(nextheader,hlen);
                    past+=result;
                    if (result!=hlen) 
                    {
                        end_of_file=1;
                        break;
                    }
                }
            }
            if (end_of_file)
            {
                // No more headers
                break;
            }
            // Found 00-00-01 in nextheader, assume a regular header
            strangeheader=0;

            // PACK header
            if ( nextheader[3]==0xBA) 
            {
                dbg_print(DMT_VERBOSE, "PACK header\n");
                buffered_read(nextheader+6,8);
                past+=result;
                if (result!=8) 
                {
                    // Consider this the end of the show.
                    end_of_file=1;
                    break;
                }

                if ( (nextheader[4]&0xC4)!=0x44 || !(nextheader[6]&0x04)
                    || !(nextheader[8]&0x04) || !(nextheader[9]&0x01)
                    || (nextheader[12]&0x03)!=0x03 ) 
                {
                    // broken pack header
                    falsepack=1;
                }
                // We don't need SCR/SCR_ext
                int stufflen=nextheader[13]&0x07;

                if (falsepack) 
                {
                    mprint ("Warning: Defective Pack header\n");
                }

                // If not defect, load stuffing
                buffered_skip ((int) stufflen);
                past+=stufflen;
                // fake a result value as something was skipped
                result=1;
                continue;
            }
            // Some PES stream
            else if (nextheader[3]>=0xBB && nextheader[3]<=0xDF) 
            {
                // System header
                // nextheader[3]==0xBB
                // 0xBD Private 1
                // 0xBE PAdding
                // 0xBF Private 2
                // 0xC0-0DF audio

                unsigned headerlen=nextheader[4]<<8 | nextheader[5];

                dbg_print(DMT_VERBOSE, "non Video PES (type 0x%2X) - len %u\n",
                           nextheader[3], headerlen);

                // The 15000 here is quite arbitrary, the longest packages I
                // know of are 12302 bytes (Private 1 data in RTL recording).
                if ( headerlen > 15000 )
                {
                    mprint("Suspicious non Video PES (type 0x%2X) - len %u\n",
                           nextheader[3], headerlen);
                    mprint("Do not skip over, search for next.\n");
                    headerlen = 2;
                }

                // Skip over it
                buffered_skip ((int) headerlen);
                past+=headerlen;
                // fake a result value as something was skipped
                result=1;

                continue;
            }
            // Read the next video PES
            else if ((nextheader[3]&0xf0)==0xe0) 
            {
                int hlen; // Dummy variable, unused
                int peslen = read_video_pes_header(nextheader, &hlen, 0);
                if (peslen < 0)
                {
                    end_of_file=1;
                    break;
                }

                vpesnum++;
                dbg_print(DMT_VERBOSE, "PES video packet #%u\n", vpesnum);


                int want = (int) ((BUFSIZE-inbuf)>peslen ? peslen : (BUFSIZE-inbuf));

                if (want != peslen) {
                    fatal(EXIT_BUFFER_FULL, "Oh Oh, PES longer than remaining buffer space\n");
                }
                if (want == 0) // Found package with header but without payload
                {
                    continue;
                }

                buffered_read (buffer+inbuf,want);
                past=past+result;
                if (result>0) {
                    payload_read+=(int) result;
                }
                inbuf+=result;

                if (result!=want) { // Not complete - EOF
                    end_of_file=1;
                    break;
                }
                enough = 1; // We got one PES

            } else {
                // If we are here this is an unknown header type
                mprint("Unknown header %02X\n", nextheader[3]);
                strangeheader=1;
            }
        }
    } 
    while (result!=0 && !enough && BUFSIZE!=inbuf);

    dbg_print(DMT_VERBOSE, "PES data read: %d\n", payload_read);

    return payload_read;
}


// Returns number of bytes read, or zero for EOF
LLONG general_getmoredata(void)
{
    int bytesread = 0;
    int want;

    do 
    {		
        want = (int) (BUFSIZE-inbuf);
        buffered_read (buffer+inbuf,want); // This is a macro.		
        // 'result' HAS the number of bytes read
        past=past+result;
        inbuf+=result;
        bytesread+=(int) result;
    } while (result!=0 && result!=want);
    return bytesread;
}

// Raw file process
void raw_loop ()
{
    LLONG got;
    LLONG processed;
    
    current_pts = 90; // Pick a valid PTS time
    pts_set = 1;
    set_fts(); // Now set the FTS related variables
    dbg_print(DMT_VIDES, "PTS: %s (%8u)",
               print_mstime(current_pts/(MPEG_CLOCK_FREQ/1000)),
               unsigned(current_pts));
    dbg_print(DMT_VIDES, "  FTS: %s\n", print_mstime(get_fts()));

    do
    {
        inbuf=0;

        got=general_getmoredata();

        if (got == 0) // Shortcircuit if we got nothing to process
            break;

        processed=process_raw();		

        int ccblocks = cb_field1;
        current_pts += cb_field1*1001/30*(MPEG_CLOCK_FREQ/1000);
        set_fts(); // Now set the FTS related variables including fts_max

        dbg_print(DMT_VIDES, "PTS: %s (%8u)",
               print_mstime(current_pts/(MPEG_CLOCK_FREQ/1000)),
               unsigned(current_pts));
        dbg_print(DMT_VIDES, "  FTS: %s incl. %d CB\n",
               print_mstime(get_fts()), ccblocks);
      
        if (processed<got)
        {
            mprint ("BUG BUG\n");			
        }
    }
    while (inbuf);
}

/* Process inbuf bytes in buffer holding raw caption data (three byte packets, the first being the field).
 * The number of processed bytes is returned. */
LLONG process_raw_with_field (void)
{
    unsigned char data[3];
    data[0]=0x04; // Field 1
    current_field=1;

    for (unsigned long i=0; i<inbuf; i=i+3)
    {
        if ( !saw_caption_block && *(buffer+i)==0xff && *(buffer+i+1)==0xff)
        {
            // Skip broadcast header 
        }
        else
        {
			data[0]=buffer[i];
            data[1]=buffer[i+1];
            data[2]=buffer[i+2];

            // do_cb increases the cb_field1 counter so that get_fts()
            // is correct.
            do_cb(data);
        }
    }
    return inbuf;
}


/* Process inbuf bytes in buffer holding raw caption data (two byte packets).
 * The number of processed bytes is returned. */
LLONG process_raw (void)
{
    unsigned char data[3];
    data[0]=0x04; // Field 1
    current_field=1;

    for (unsigned long i=0; i<inbuf; i=i+2)
    {
        if ( !saw_caption_block && *(buffer+i)==0xff && *(buffer+i+1)==0xff)
        {
            // Skip broadcast header 
        }
        else
        {
            data[1]=buffer[i];
            data[2]=buffer[i+1];

            // do_cb increases the cb_field1 counter so that get_fts()
            // is correct.
            do_cb(data);
        }
    }
    return inbuf;
}


void general_loop(void)
{
    LLONG overlap=0;    
    LLONG pos = 0; /* Current position in buffer */    
    inbuf = 0; // No data yet

    end_of_file = 0;
    current_picture_coding_type = 0;

    while (!end_of_file && !processed_enough) 
    {
        /* Get rid of the bytes we already processed */        
        overlap=inbuf-pos; 
        if ( pos != 0 ) {
            // Only when needed as memmove has been seen crashing
            // for dest==source and n >0
            memmove (buffer,buffer+pos,(size_t) (inbuf-pos)); 
            inbuf-=pos;
        }
        pos = 0;

        // GET MORE DATA IN BUFFER
        LLONG i;
        position_sanity_check();
        switch (stream_mode)
        {
            case SM_ELEMENTARY_OR_NOT_FOUND:
                i = general_getmoredata();
                break;
            case SM_TRANSPORT:
                i = ts_getmoredata();
                break;
            case SM_PROGRAM:
                i = ps_getmoredata();
                break;
            case SM_ASF:
                i = asf_getmoredata();
                break;
            default:
                fatal(EXIT_BUG_BUG, "Impossible stream_mode");
        }

        position_sanity_check();
        if (fh_out_elementarystream!=NULL)
            fwrite (buffer+overlap,1,(size_t) (inbuf-overlap),fh_out_elementarystream);

        if (i==0)
        {
            end_of_file = 1;
            memset (buffer+inbuf, 0, (size_t) (BUFSIZE-inbuf)); /* Clear buffer at the end */			
        }

        if (inbuf == 0)
        {
            /* Done: Get outta here */
            break;
        }

        LLONG got; // Means 'consumed' from buffer actually

        static LLONG last_pts = 0x01FFFFFFFFLL;

		if (hauppauge_mode)
		{
			got = process_raw_with_field();
			if (pts_set)
				set_fts(); // Try to fix timing from TS data
		}
        else if (bufferdatatype == PES)
        {
          printf ("Is PES\n") ;
          got = process_m2v (buffer, inbuf);
        }
		else if (bufferdatatype == TELETEXT)
		{
			// Dispatch to Petr Kutalek 's telxcc.
			tlt_process_pes_packet (buffer, (uint16_t) inbuf);
			got = inbuf; 
		}
        else if (bufferdatatype == RAW) // Raw two byte 608 data from DVR-MS/ASF
        {
            // The asf_getmoredata() loop sets current_pts when possible
            if (pts_set == 0)
            {
                mprint("DVR-MS/ASF file without useful time stamps - count blocks.\n");
                // Otherwise rely on counting blocks
                current_pts = 12345; // Pick a valid PTS time
                pts_set = 1;
            }

            if (current_pts != last_pts)
            {
                // Only initialize the FTS values and reset the cb
                // counters when the PTS is different. This happens frequently
                // with ASF files.

                if (min_pts==0x01FFFFFFFFLL)
                {
                    // First call
                    fts_at_gop_start = 0;
                }
                else
                    fts_at_gop_start = get_fts();

                frames_since_ref_time = 0;
                set_fts();

                last_pts = current_pts;
            }

            dbg_print(DMT_VIDES, "PTS: %s (%8u)",
                   print_mstime(current_pts/(MPEG_CLOCK_FREQ/1000)),
                   unsigned(current_pts));
            dbg_print(DMT_VIDES, "  FTS: %s\n", print_mstime(get_fts()));

            got = process_raw();
        }
        else if (bufferdatatype == H264) // H.264 data from TS file
        {
          printf("Is h264\n") ;
            got = process_avc(buffer, inbuf);
        }
        else
            fatal(EXIT_BUG_BUG, "Unknown data type!");

        if (got>inbuf)
        {
            mprint ("BUG BUG\n");			
        }
        pos+=got;

        if (live_stream)
        {
            int cur_sec = (int) (get_fts() / 1000);
            int th=cur_sec/10;
            if (last_reported_progress!=th)
            {
                activity_progress (-1,cur_sec/60, cur_sec%60);
                last_reported_progress = th;
            }
        }
        else
        {
            if (total_inputsize>255) // Less than 255 leads to division by zero below.
            {
                int progress = (int) ((((total_past+past)>>8)*100)/(total_inputsize>>8));
                if (last_reported_progress != progress)
                {
                    int cur_sec = (int) (get_fts() / 1000);
                    activity_progress(progress, cur_sec/60, cur_sec%60);                    
                    last_reported_progress = progress;
                }
            }
        }
        position_sanity_check();
    }
    // Flush remaining HD captions
    if (has_ccdata_buffered)
      {
        printf("flushing HD captions\n" );
        process_hdcc();
      }

    if (total_past!=total_inputsize && binary_concat && !processed_enough)
    {
        mprint("\n\n\n\nATTENTION!!!!!!\n");
        mprint("Processing of %s %d ended prematurely %lld < %lld, please send bug report.\n\n",
           inputfile[current_file], current_file, past, inputsize);
    }
	mprint ("\nNumber of NAL_type_7: %ld\n",num_nal_unit_type_7);
	mprint ("Number of VCL_HRD: %ld\n",num_vcl_hrd);
	mprint ("Number of NAL HRD: %ld\n",num_nal_hrd);
	mprint ("Number of jump-in-frames: %ld\n",num_jump_in_frames);
	mprint ("Number of num_unexpected_sei_length: %ld", num_unexpected_sei_length);
}

// Raw caption with FTS file process
void rcwt_loop( void )
{
    // As BUFSIZE is a macro this is just a reminder
    if (BUFSIZE < (3*0xFFFF + 10))
        fatal (EXIT_BUG_BUG, "BUFSIZE too small for RCWT caption block.\n");

    // Generic buffer to hold some data
    static unsigned char *parsebuf = (unsigned char*)malloc(1024);
    static long parsebufsize = 1024;

    LLONG currfts;
    uint16_t cbcount = 0;

    int bread = 0; // Bytes read

    buffered_read(parsebuf,11);
    past+=result;
    bread+=(int) result;
    if (result!=11)
    {
        mprint("Premature end of file!\n");
        end_of_file=1;
        return;
    }

    // Expecting RCWT header
    if( !memcmp(parsebuf, "\xCC\xCC\xED", 3 ) )
    {
		dbg_print(DMT_PARSE, "\nRCWT header\n");
        dbg_print(DMT_PARSE, "File created by %02X version %02X%02X\nFile format revision: %02X%02X\n",
               parsebuf[3], parsebuf[4], parsebuf[5],
               parsebuf[6], parsebuf[7]);
        
    }
    else
    {
        fatal(EXIT_MISSING_RCWT_HEADER, "Missing RCWT header. Abort.\n");
    }

    // Initialize first time. As RCWT files come with the correct FTS the
    // initial (minimal) time needs to be set to 0.
    current_pts = 0;
    pts_set=1;
    set_fts(); // Now set the FTS related variables

    // Loop until no more data is found
    while(1)
    {
        // Read the data header
        buffered_read(parsebuf,10);
        past+=result;
        bread+=(int) result;

        if (result!=10)
        {
            if (result!=0)
                mprint("Premature end of file!\n");

            // We are done
            end_of_file=1;
            break;
        }
        currfts = *((LLONG*)(parsebuf));
        cbcount = *((uint16_t*)(parsebuf+8));

        dbg_print(DMT_PARSE, "RCWT data header FTS: %s  blocks: %u\n",
               print_mstime(currfts), cbcount);

        if ( cbcount > 0 )
        {
            if ( cbcount*3 > parsebufsize) {
                parsebuf = (unsigned char*)realloc(parsebuf, cbcount*3);
                if (!parsebuf)
                    fatal(EXIT_NOT_ENOUGH_MEMORY, "Out of memory");
                parsebufsize = cbcount*3;
            }
            buffered_read(parsebuf,cbcount*3);
            past+=result;
            bread+=(int) result;
            if (result!=cbcount*3)
            {
                mprint("Premature end of file!\n");
                end_of_file=1;
                break;
            }

            // Process the data
            current_pts = currfts*(MPEG_CLOCK_FREQ/1000);
            if (pts_set==0)
                pts_set=1;
            set_fts(); // Now set the FTS related variables

            dbg_print(DMT_VIDES, "PTS: %s (%8u)",
                   print_mstime(current_pts/(MPEG_CLOCK_FREQ/1000)),
                   unsigned(current_pts));
            dbg_print(DMT_VIDES, "  FTS: %s\n", print_mstime(get_fts()));

            for (int j=0; j<cbcount*3; j=j+3)
            {
                do_cb(parsebuf+j);
            }
        }
    } // end while(1)

    dbg_print(DMT_PARSE, "Processed %d bytes\n", bread);
}
