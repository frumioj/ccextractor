#include "ccextractor.h"

// Produces minimally-compliant SMPTE Timed Text (W3C TTML)
// format-compatible output 

// See http://www.w3.org/TR/ttaf1-dfxp/ and
// https://www.smpte.org/sites/default/files/st2052-1-2010.pdf

// Copyright (C) 2012 John Kemp

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

void write_stringz_as_smptett (char *string, struct s_write *wb, LLONG ms_start, LLONG ms_end)
{
    unsigned h1,m1,s1,ms1;
    unsigned h2,m2,s2,ms2;

    mstotime (ms_start,&h1,&m1,&s1,&ms1);
    mstotime (ms_end-1,&h2,&m2,&s2,&ms2);

    sprintf ((char *) str,"<p begin=\"%02u:%02u:%02u,%03u\" end=\"%02u:%02u:%02u,%03u\">\r\n",h1,m1,s1,ms1, h2,m2,s2,ms2);
    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    write (wb->fh, enc_buffer,enc_buffer_used);		
    int len=strlen (string);
    unsigned char *unescaped= (unsigned char *) malloc (len+1); 
    unsigned char *el = (unsigned char *) malloc (len*3+1); // Be generous
    if (el==NULL || unescaped==NULL)
        fatal (EXIT_NOT_ENOUGH_MEMORY, "In write_stringz_as_sami() - not enough memory.\n");
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
        //write (wb->fh, encoded_br, encoded_br_length);
        
        write (wb->fh, encoded_crlf, encoded_crlf_length);        
        begin+= strlen ((const char *) begin)+1;
    }

    sprintf ((char *) str,"</p>\n");
    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    write (wb->fh, enc_buffer,enc_buffer_used);
    sprintf ((char *) str,"<p begin=\"%02u:%02u:%02u,%03u\">\n\n",h2,m2,s2,ms2);
    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    write (wb->fh, enc_buffer,enc_buffer_used);
    sprintf ((char *) str,"</p>\n");
}



int write_cc_buffer_as_smptett (struct eia608_screen *data, struct s_write *wb)
{
    unsigned h1,m1,s1,ms1;
    unsigned h2,m2,s2,ms2;
    int wrote_something=0;
    LLONG startms = wb->data608->current_visible_start_ms;

    startms+=subs_delay;
    if (startms<0) // Drop screens that because of subs_delay start too early
        return 0; 

    LLONG endms   = get_visible_end()+subs_delay;
    endms--; // To prevent overlapping with next line.
    mstotime (startms,&h1,&m1,&s1,&ms1);
    mstotime (endms-1,&h2,&m2,&s2,&ms2);

    sprintf ((char *) str,"<p begin=\"%02u:%02u:%02u,%03u\" end=\"%02u:%02u:%02u,%03u\">\n",h1,m1,s1,ms1, h2,m2,s2,ms2);

    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    write (wb->fh, enc_buffer,enc_buffer_used);
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
        {				
            int length = get_decoder_line_encoded (subline, i, data);
            if (encoding!=ENC_UNICODE)
            {
                dbg_print(DMT_608, "\r");
                dbg_print(DMT_608, "%s\n",subline);
            }
            write (wb->fh, subline, length);            
            wrote_something=1;
            //if (i!=14)            
              //write (wb->fh, encoded_br, encoded_br_length);            
            write (wb->fh,encoded_crlf, encoded_crlf_length);
        }
    }
    sprintf ((char *) str,"</p>\n");
    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    write (wb->fh, enc_buffer,enc_buffer_used);

    if (encoding!=ENC_UNICODE)
    {
        dbg_print(DMT_608, "\r%s\n", str);
    }
    enc_buffer_used=encode_line (enc_buffer,(unsigned char *) str);
    //write (wb->fh, enc_buffer,enc_buffer_used);

    return wrote_something;
}
