#include "ccextractor.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif


void init_write (struct s_write *wb, int field)
{
    wb->fh=-1;
    wb->filename=NULL;	
    wb->data608=(struct eia608 *) malloc (sizeof (struct eia608));
	wb->bytes_processed_608=0; 
	wb->my_field=field;
    init_eia608 (wb->data608);
} 

void writeraw (const unsigned char *data, int length, struct s_write *wb)
{
    write (wb->fh,data,length);
}

void writedata (const unsigned char *data, int length, struct s_write *wb)
{
    // Don't do anything for empty data
    if (data==NULL)
        return;

    if (write_format==OF_RAW)
	{
		if (wb)
			writeraw (data,length,wb);
	}
    else if (write_format==OF_SMPTETT || 
             write_format==OF_SAMI ||
             write_format==OF_SRT ||
             write_format==OF_TRANSCRIPT ||
			 write_format==OF_NULL)
        process608 (data,length,wb);
    else
        fatal(EXIT_BUG_BUG, "Should not be reached!");
}

void flushbuffer (struct s_write *wb, int closefile)
{
    if (closefile && wb!=NULL && wb->fh!=-1 && !cc_to_stdout)	
        close (wb->fh);	
}

void printdata (const unsigned char *data1, int length1,
                const unsigned char *data2, int length2)
{
    /* these are only used by DVD raw mode: */
    static int loopcount = 1; /* loop 1: 5 elements, loop 2: 8 elements, 
                                 loop 3: 11 elements, rest: 15 elements */
    static int datacount = 0; /* counts within loop */

    if (rawmode==0) /* Broadcast (raw/srt/sami) */
    {
        if (length1 && extract!=2)
        {
            writedata (data1,length1,&wbout1);
        }
        if (length2)
		{
			if (extract!=1) 
				writedata (data2,length2,&wbout2);
			else // User doesn't want field 2 data, but we want XDS.
				writedata (data2,length2,NULL);
        }
    }
    else /* DVD */
    {
        if (datacount==0)
        {
            writedata (DVD_HEADER,sizeof (DVD_HEADER),&wbout1);
            if (loopcount==1)
                writedata (lc1,sizeof (lc1),&wbout1);
            if (loopcount==2)
                writedata (lc2,sizeof (lc2),&wbout1);
            if (loopcount==3)
            {
                writedata (lc3,sizeof (lc3),&wbout1);
                writedata (data2,length2,&wbout1);
            }
            if (loopcount>3)
            {
                writedata (lc4,sizeof (lc4),&wbout1);
                writedata (data2,length2,&wbout1);
            }
        }
        datacount++;
        writedata (lc5,sizeof (lc5), &wbout1);
        writedata (data1,length1,&wbout1);
        if (((loopcount == 1) && (datacount < 5)) || ((loopcount == 2) && 
            (datacount < 8)) || (( loopcount == 3) && (datacount < 11)) || 
            ((loopcount > 3) && (datacount < 15)))
        {
            writedata (lc6,sizeof (lc6), &wbout1);
            writedata (data2,length2,&wbout1);
        }
        else
        {
            if (loopcount==1)
            {
                writedata (lc6,sizeof (lc6), &wbout1);
                writedata (data2,length2,&wbout1);
            }
            loopcount++;
            datacount=0;
        }
    }
}

/* Buffer data with the same FTS and write when a new FTS or data==NULL
 * is encountered */
void writercwtdata (const unsigned char *data)
{
    static LLONG prevfts = -1;
    LLONG currfts = fts_now + fts_global;
    static uint16_t cbcount = 0;
    static int cbempty=0;
    static unsigned char cbbuffer[0xFFFF*3]; // TODO: use malloc
    static unsigned char cbheader[8+2];

    if ( (prevfts != currfts && prevfts != -1)
         || data == NULL
         || cbcount == 0xFFFF)
    {
        // Remove trailing empty or 608 padding caption blocks
        if ( cbcount != 0xFFFF)
        {
            unsigned char cc_valid;
            unsigned char cc_type;
            int storecbcount=cbcount;

            for( int cb = cbcount-1; cb >= 0 ; cb-- )
            {
                cc_valid = (*(cbbuffer+3*cb) & 4) >>2;
                cc_type = *(cbbuffer+3*cb) & 3;

                // The -fullbin option disables pruning of 608 padding blocks
                if ( (cc_valid && cc_type <= 1 // Only skip NTSC padding packets
                      && !fullbin // Unless we want to keep them
                      && *(cbbuffer+3*cb+1)==0x80
                      && *(cbbuffer+3*cb+2)==0x80)
                     || !(cc_valid || cc_type==3) ) // or unused packets
                {
                    cbcount--;
                }
                else
                {
                    cb = -1;
                }
            }
            dbg_print(DMT_CBRAW, "%s Write %d RCWT blocks - skipped %d padding / %d unused blocks.\n",
                       print_mstime(prevfts), cbcount, storecbcount - cbcount, cbempty);
        }

        // New FTS, write data header
        // RCWT data header (10 bytes):
        //byte(s)   value   description
        //0-7       FTS     int64_t number with current FTS
        //8-9       blocks  Number of 3 byte data blocks with the same FTS that are
        //                  following this header
        memcpy(cbheader,&prevfts,8);
        memcpy(cbheader+8,&cbcount,2);

        if (cbcount > 0)
        {
            writeraw(cbheader,10,&wbout1);
            writeraw(cbbuffer,3*cbcount, &wbout1);
        }
        cbcount = 0;
        cbempty = 0;
    }

    if ( data )
    {
        // Store the data while the FTS is unchanged 

        unsigned char cc_valid = (*data & 4) >> 2;
        unsigned char cc_type = *data & 3;
        // Store only non-empty packets
        if (cc_valid || cc_type==3)
        {
            // Store in buffer until we know how many blocks come with
            // this FTS.
            memcpy(cbbuffer+cbcount*3, data, 3);
            cbcount++;
        }
        else
        {
            cbempty++;
        }
    }
    else
    {
        // Write a padding block for field 1 and 2 data if this is the final
        // call to this function.  This forces the RCWT file to have the
        // same length as the source video file.

        // currfts currently holds the end time, subtract one block length
        // so that the FTS corresponds to the time before the last block is
        // written
        currfts -= 1001/30;

        memcpy(cbheader,&currfts,8);
        cbcount = 2;
        memcpy(cbheader+8,&cbcount,2);

        memcpy(cbbuffer, "\x04\x80\x80", 3); // Field 1 padding
        memcpy(cbbuffer+3, "\x05\x80\x80", 3); // Field 2 padding

        writeraw(cbheader,10,&wbout1);
        writeraw(cbbuffer,3*cbcount, &wbout1);

        cbcount = 0;
        cbempty = 0;

        dbg_print(DMT_CBRAW, "%s Write final padding RCWT blocks.\n",
                   print_mstime(currfts));
    }

    prevfts = currfts;
}
