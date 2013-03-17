#include "ccextractor.h"

// Encodes a generic string. Note that since we use the encoders for closed caption
// data, text would have to be encoded as CCs... so using special characters here
// it's a bad idea. 
unsigned encode_line (unsigned char *buffer, unsigned char *text)
{ 
    unsigned bytes=0;
    while (*text)
    {		
        switch (encoding)
        {
            case ENC_UTF_8:
            case ENC_LATIN_1:
                *buffer=*text;
                bytes++;
                buffer++;
                break;
            case ENC_UNICODE:				
                *buffer=*text;				
                *(buffer+1)=0;
                bytes+=2;				
                buffer+=2;
                break;
        }		
        text++;
    }
    return bytes;
}

#define ISSEPARATOR(c) (c==' ' || c==0x89 || ispunct(c) \
    || c==0x99) // This is the apostrofe. We get it here in CC encoding, not ASCII


void correct_case (int line_num, struct eia608_screen *data)
{
    int i=0;
    while (i<spell_words)
    {
        char *c=(char *) data->characters[line_num];
        size_t len=strlen (spell_correct[i]);
        while ((c=strstr (c,spell_lower[i]))!=NULL)
        {
            // Make sure it's a whole word (start of line or
            // preceded by space, and end of line or followed by
            // space)
            unsigned char prev;
            if (c==(char *) data->characters[line_num]) // Beginning of line...
                prev=' '; // ...Pretend we had a blank before
            else
                prev=*(c-1);             
            unsigned char next;
            if (c-(char *) data->characters[line_num]+len==CC608_SCREEN_WIDTH) // End of line...
                next=' '; // ... pretend we have a blank later
            else
                next=*(c+len);			
            if ( ISSEPARATOR(prev) && ISSEPARATOR(next))
            {
                memcpy (c,spell_correct[i],len);
            }
            c++;
        }
        i++;
    }
}

void capitalize (int line_num, struct eia608_screen *data)
{	
    for (int i=0;i<CC608_SCREEN_WIDTH;i++)
    {
        switch (data->characters[line_num][i])
        {
            case ' ': 
            case 0x89: // This is a transparent space
            case '-':
                break; 
            case '.': // Fallthrough
            case '?': // Fallthrough
            case '!':
            case ':':
                new_sentence=1;
                break;
            default:
                if (new_sentence)			
                    data->characters[line_num][i]=cctoupper (data->characters[line_num][i]);
                else
                    data->characters[line_num][i]=cctolower (data->characters[line_num][i]);
                new_sentence=0;
                break;
        }
    }
}

void find_limit_characters (unsigned char *line, int *first_non_blank, int *last_non_blank)
{
    *last_non_blank=-1;
    *first_non_blank=-1;
    for (int i=0;i<CC608_SCREEN_WIDTH;i++)
    {
        unsigned char c=line[i];
        if (c!=' ' && c!=0x89)
        {
            if (*first_non_blank==-1)
                *first_non_blank=i;
            *last_non_blank=i;
        }
    }
}

unsigned get_decoder_line_basic (unsigned char *buffer, int line_num, struct eia608_screen *data)
{
    unsigned char *line = data->characters[line_num];
    int last_non_blank=-1;
    int first_non_blank=-1;
    unsigned char *orig=buffer; // Keep for debugging
    find_limit_characters (line, &first_non_blank, &last_non_blank);

    if (first_non_blank==-1)
    {
        *buffer=0;
        return 0;
    }

    int bytes=0;
    for (int i=first_non_blank;i<=last_non_blank;i++)
    {
        char c=line[i];
        switch (encoding)
        {
            case ENC_UTF_8:
                bytes=get_char_in_utf_8 (buffer,c);
                break;
            case ENC_LATIN_1:
                get_char_in_latin_1 (buffer,c);
                bytes=1;
                break;
            case ENC_UNICODE:
                get_char_in_unicode (buffer,c);
                bytes=2;				
                break;
        }
        buffer+=bytes;
    }
    *buffer=0;
    return (unsigned) (buffer-orig); // Return length
}

unsigned get_decoder_line_encoded_for_gui (unsigned char *buffer, int line_num, struct eia608_screen *data)
{
    unsigned char *line = data->characters[line_num];	
    unsigned char *orig=buffer; // Keep for debugging
    int first=0, last=31;
    find_limit_characters(line,&first,&last);
    for (int i=first;i<=last;i++)
    {	
        get_char_in_latin_1 (buffer,line[i]);
        buffer++;
    }
    *buffer=0;
    return (unsigned) (buffer-orig); // Return length

}

unsigned get_decoder_line_encoded (unsigned char *buffer, int line_num, struct eia608_screen *data)
{
    int col = COL_WHITE;
    int underlined = 0;
    int italics = 0;	

    unsigned char *line = data->characters[line_num];	
    unsigned char *orig=buffer; // Keep for debugging
    int first=0, last=31;
    if (trim_subs)
        find_limit_characters(line,&first,&last);
    for (int i=first;i<=last;i++)
    {	
        // Handle color
        int its_col = data->colors[line_num][i];
        if (its_col != col  && !nofontcolor)
        {
            if (col!=COL_WHITE) // We need to close the previous font tag
            {
                buffer+= encode_line (buffer,(unsigned char *) "</font>");
            }
            // Add new font tag
            buffer+=encode_line (buffer, (unsigned char*) color_text[its_col][1]);
            if (its_col==COL_USERDEFINED)
            {
                // The previous sentence doesn't copy the whole 
                // <font> tag, just up to the quote before the color
                buffer+=encode_line (buffer, (unsigned char*) usercolor_rgb);
                buffer+=encode_line (buffer, (unsigned char*) "\">");
            }			

            col = its_col;
        }
        // Handle underlined
        int is_underlined = data->fonts[line_num][i] & FONT_UNDERLINED;
        if (is_underlined && underlined==0 && !notypesetting) // Open underline
        {
            buffer+=encode_line (buffer, (unsigned char *) "<u>");
        }
        if (is_underlined==0 && underlined && !notypesetting) // Close underline
        {
            buffer+=encode_line (buffer, (unsigned char *) "</u>");
        } 
        underlined=is_underlined;
        // Handle italics
        int has_ita = data->fonts[line_num][i] & FONT_ITALICS;		
        if (has_ita && italics==0 && !notypesetting) // Open italics
        {
            buffer+=encode_line (buffer, (unsigned char *) "<i>");
        }
        if (has_ita==0 && italics && !notypesetting) // Close italics
        {
            buffer+=encode_line (buffer, (unsigned char *) "</i>");
        } 
        italics=has_ita;
        int bytes=0;
        switch (encoding)
        {
            case ENC_UTF_8:
                bytes=get_char_in_utf_8 (buffer,line[i]);
                break;
            case ENC_LATIN_1:
                get_char_in_latin_1 (buffer,line[i]);
                bytes=1;
                break;
            case ENC_UNICODE:
                get_char_in_unicode (buffer,line[i]);
                bytes=2;				
                break;
        }
        buffer+=bytes;        
    }
    if (italics && !notypesetting)
    {
        buffer+=encode_line (buffer, (unsigned char *) "</i>");
    }
    if (underlined && !notypesetting)
    {
        buffer+=encode_line (buffer, (unsigned char *) "</u>");
    }
    if (col != COL_WHITE && !nofontcolor)
    {
        buffer+=encode_line (buffer, (unsigned char *) "</font>");
    }
    *buffer=0;
    return (unsigned) (buffer-orig); // Return length
}


void delete_all_lines_but_current (struct eia608_screen *data, int row)
{
    for (int i=0;i<15;i++)
    {
        if (i!=row)
        {
            memset(data->characters[i],' ',CC608_SCREEN_WIDTH);
            data->characters[i][CC608_SCREEN_WIDTH]=0;		
            memset (data->colors[i],default_color,CC608_SCREEN_WIDTH+1); 
            memset (data->fonts[i],FONT_REGULAR,CC608_SCREEN_WIDTH+1); 
            data->row_used[i]=0;        
        }
    }
}

void mstotime (LLONG milli, unsigned *hours, unsigned *minutes,
               unsigned *seconds, unsigned *ms)
{
    // LLONG milli = (LLONG) ((ccblock*1000)/29.97);
    *ms=(unsigned) (milli%1000); // milliseconds
    milli=(milli-*ms)/1000;  // Remainder, in seconds
    *seconds = (int) (milli%60);
    milli=(milli-*seconds)/60; // Remainder, in minutes
    *minutes = (int) (milli%60);
    milli=(milli-*minutes)/60; // Remainder, in hours
    *hours=(int) milli;
}

void fprintf_encoded (FILE *fh, const char *string)
{
    GUARANTEE(strlen (string)*3);
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) string);
    fwrite (enc_buffer,enc_buffer_used,1,fh);
}

void write_cc_buffer_to_gui (struct eia608_screen *data, struct s_write *wb)
{
    unsigned h1,m1,s1,ms1;
    unsigned h2,m2,s2,ms2;    
	int with_data=0;

	for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
			with_data=1;
    }
	if (!with_data)
		return;

    LLONG ms_start= wb->data608->current_visible_start_ms;

    ms_start+=subs_delay;
    if (ms_start<0) // Drop screens that because of subs_delay start too early
        return;
    int time_reported=0;    
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
        {
            fprintf (stderr, "###SUBTITLE#");
            if (!time_reported)
            {
                LLONG ms_end = get_fts()+subs_delay;		
                mstotime (ms_start,&h1,&m1,&s1,&ms1);
                mstotime (ms_end-1,&h2,&m2,&s2,&ms2); // -1 To prevent overlapping with next line.
                // Note, only MM:SS here as we need to save space in the preview window
                fprintf (stderr, "%02u:%02u#%02u:%02u#",
                    h1*60+m1,s1, h2*60+m2,s2);
                time_reported=1;
            }
            else
                fprintf (stderr, "##");
            
            // We don't capitalize here because whatever function that was used
            // before to write to file already took care of it.
            int length = get_decoder_line_encoded_for_gui (subline, i, data);
            fwrite (subline, 1, length, stderr);
            fwrite ("\n",1,1,stderr);
        }
    }
    fflush (stderr);
}

void try_to_add_end_credits (struct s_write *wb)
{
    if (wb->fh==-1)
        return;
    LLONG window=get_fts()-last_displayed_subs_ms-1;
    if (window<endcreditsforatleast.time_in_ms) // Won't happen, window is too short
        return;
    LLONG length=endcreditsforatmost.time_in_ms > window ? 
        window : endcreditsforatmost.time_in_ms;

    LLONG st=get_fts()-length-1;
    LLONG end=get_fts();

    switch (write_format)
    {
        case OF_SRT:
            write_stringz_as_srt(end_credits_text,wb,st,end);
            break;
        case OF_SAMI:
            write_stringz_as_sami(end_credits_text,wb,st,end);
            break;
    case OF_SMPTETT:
      write_stringz_as_smptett(end_credits_text,wb,st,end);
      break ;
        default:
            // Do nothing for the rest
            break;
    }    
}

void try_to_add_start_credits (struct s_write *wb)
{
    LLONG l = wb->data608->current_visible_start_ms+subs_delay;
    // We have a windows from last_displayed_subs_ms to l - we need to see if it fits

    if (l<startcreditsnotbefore.time_in_ms) // Too early
        return;

    if (last_displayed_subs_ms+1 > startcreditsnotafter.time_in_ms) // Too late
        return;

    LLONG st = startcreditsnotbefore.time_in_ms>(last_displayed_subs_ms+1) ?
        startcreditsnotbefore.time_in_ms : (last_displayed_subs_ms+1); // When would credits actually start

    LLONG end = startcreditsnotafter.time_in_ms<(l-1) ?
        startcreditsnotafter.time_in_ms : (l-1); 

    LLONG window = end-st; // Allowable time in MS

    if (startcreditsforatleast.time_in_ms>window) // Window is too short
        return;

    LLONG length=startcreditsforatmost.time_in_ms > window ? 
        window : startcreditsforatmost.time_in_ms;

    dbg_print(DMT_VERBOSE, "Last subs: %lld   Current position: %lld\n",
        last_displayed_subs_ms, l); 
    dbg_print(DMT_VERBOSE, "Not before: %lld   Not after: %lld\n",
        startcreditsnotbefore.time_in_ms, startcreditsnotafter.time_in_ms);
    dbg_print(DMT_VERBOSE, "Start of window: %lld   End of window: %lld\n",st,end);

    if (window>length+2) 
    {
        // Center in time window
        LLONG pad=window-length; 
        st+=(pad/2);
    }
    end=st+length;
    switch (write_format)
    {
        case OF_SRT:
            write_stringz_as_srt(start_credits_text,wb,st,end);
            break;
        case OF_SAMI:
            write_stringz_as_sami(start_credits_text,wb,st,end);
            break;
        case OF_SMPTETT:
            write_stringz_as_smptett(start_credits_text,wb,st,end);
            break;
        default:
            // Do nothing for the rest
            break;
    }
    startcredits_displayed=1;
    return;
    

}
