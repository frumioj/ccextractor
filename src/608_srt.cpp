#include "ccextractor.h"


/* The timing here is not PTS based, but output based, i.e. user delay must be accounted for
   if there is any */
void write_stringz_as_srt (char *string, struct s_write *wb, LLONG ms_start, LLONG ms_end)
{
    unsigned h1,m1,s1,ms1;
    unsigned h2,m2,s2,ms2;

    mstotime (ms_start,&h1,&m1,&s1,&ms1);
    mstotime (ms_end-1,&h2,&m2,&s2,&ms2); // -1 To prevent overlapping with next line.
    char timeline[128];   
    wb->data608->srt_counter++;
    sprintf (timeline,"%u\r\n",wb->data608->srt_counter);
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) timeline);
    write (wb->fh, enc_buffer,enc_buffer_used);
    sprintf (timeline, "%02u:%02u:%02u,%03u --> %02u:%02u:%02u,%03u\r\n",
        h1,m1,s1,ms1, h2,m2,s2,ms2);
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) timeline);
    dbg_print(DMT_608, "\n- - - SRT caption - - -\n");
    dbg_print(DMT_608, "%s",timeline);
    
    write (wb->fh, enc_buffer,enc_buffer_used);
    int len=strlen (string);
    unsigned char *unescaped= (unsigned char *) malloc (len+1); 
    unsigned char *el = (unsigned char *) malloc (len*3+1); // Be generous
    if (el==NULL || unescaped==NULL)
        fatal (EXIT_NOT_ENOUGH_MEMORY, "In write_stringz_as_srt() - not enough memory.\n");
    int pos_r=0;
    int pos_w=0;
    // Scan for \n in the string and replace it with a 0
    while (pos_r<len)
    {
        if (string[pos_r]=='\\' && string[pos_r+1]=='n')
        {
            unescaped[pos_w]=0;
            pos_r+=2;            
        }
        else
        {
            unescaped[pos_w]=string[pos_r];
            pos_r++;
        }
        pos_w++;
    }
    unescaped[pos_w]=0;
    // Now read the unescaped string (now several string'z and write them)    
    unsigned char *begin=unescaped;
    while (begin<unescaped+len)
    {
        unsigned int u = encode_line (el, begin);
        if (encoding!=ENC_UNICODE)
        {
            dbg_print(DMT_608, "\r");
            dbg_print(DMT_608, "%s\n",subline);
        }
        write (wb->fh, el, u);
        write (wb->fh, encoded_crlf, encoded_crlf_length);
        begin+= strlen ((const char *) begin)+1;
    }

    dbg_print(DMT_608, "- - - - - - - - - - - -\r\n");
   
    write (wb->fh, encoded_crlf, encoded_crlf_length);
}

int write_cc_buffer_as_srt (struct eia608_screen *data, struct s_write *wb)
{
    unsigned h1,m1,s1,ms1;
    unsigned h2,m2,s2,ms2;
    int wrote_something = 0;
    LLONG ms_start= wb->data608->current_visible_start_ms;

	int empty_buf=1;
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
		{
			empty_buf=0;
			break;
		}
	}
	if (empty_buf) // Prevent writing empty screens. Not needed in .srt
		return 0;

    ms_start+=subs_delay;
    if (ms_start<0) // Drop screens that because of subs_delay start too early
        return 0;

	LLONG ms_end = get_visible_end()+subs_delay;		
    mstotime (ms_start,&h1,&m1,&s1,&ms1);
    mstotime (ms_end-1,&h2,&m2,&s2,&ms2); // -1 To prevent overlapping with next line.
    char timeline[128];   
    wb->data608->srt_counter++;
    sprintf (timeline,"%u\r\n",wb->data608->srt_counter);
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) timeline);
    write (wb->fh, enc_buffer,enc_buffer_used);
    sprintf (timeline, "%02u:%02u:%02u,%03u --> %02u:%02u:%02u,%03u\r\n",
        h1,m1,s1,ms1, h2,m2,s2,ms2);
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) timeline);

    dbg_print(DMT_608, "\n- - - SRT caption - - -\n");
    dbg_print(DMT_608, "%s",timeline);

    write (wb->fh, enc_buffer,enc_buffer_used);		
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
        {		
            if (sentence_cap)
            {
                capitalize (i,data);
                correct_case(i,data);
            }
            int length = get_decoder_line_encoded (subline, i, data);
            if (encoding!=ENC_UNICODE)
            {
                dbg_print(DMT_608, "\r");
                dbg_print(DMT_608, "%s\n",subline);
            }
            write (wb->fh, subline, length);
            write (wb->fh, encoded_crlf, encoded_crlf_length);
            wrote_something=1;
            // fprintf (wb->fh,encoded_crlf);
        }
    }
    dbg_print(DMT_608, "- - - - - - - - - - - -\r\n");
    
    // fprintf (wb->fh, encoded_crlf);
    write (wb->fh, encoded_crlf, encoded_crlf_length);
    return wrote_something;
}
