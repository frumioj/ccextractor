#include "ccextractor.h"

void params_dump(void) 
{
    // Display parsed parameters
    mprint ("Input: ");
    for (int i=0;i<num_input_files;i++)
        mprint ("%s%s",inputfile[i],i==(num_input_files-1)?"":",");

    mprint ("\n");
    mprint ("[Raw Mode: %s] ", rawmode ? "DVD" : "Broadcast");
    mprint ("[Extract: %d] ", extract);
    mprint ("[Stream mode: ");
    switch (auto_stream)
    {
        case SM_ELEMENTARY_OR_NOT_FOUND:
            mprint ("Elementary");
            break;
        case SM_TRANSPORT:
            mprint ("Transport");
            break;
        case SM_PROGRAM:
            mprint ("Program");
            break;
        case SM_ASF:
            mprint ("DVR-MS");
            break;
        case SM_MCPOODLESRAW:
            mprint ("McPoodle's raw");
            break;
        case SM_AUTODETECT:
            mprint ("Autodetect");
            break;
		case SM_RCWT:
			mprint ("BIN");
			break;
		case SM_MP4:
			mprint ("MP4");
			break;
        default:
            fatal (EXIT_BUG_BUG, "BUG: Unknown stream mode.\n");
            break;
    }
    mprint ("]\n");
	mprint ("[Program : ");
	if (ts_forced_program_selected != 0)
		mprint ("%u ]",ts_forced_program);
	else
		mprint ("Auto ]");
	mprint (" [Hauppage mode: %s]",hauppauge_mode?"Yes":"No");
	
    mprint (" [Use MythTV code: ");
    switch (auto_myth)
    {
        case 0:
            mprint ("Disabled");
            break;
        case 1:
            mprint ("Forced - Overrides stream mode setting");
            break;
        case 2:
            mprint ("Auto");
            break;
    }
    mprint ("]");
    if (wtvconvertfix)
    {
        mprint (" [Windows 7 wtv to dvr-ms conversion fix: Enabled]");
    }
    mprint ("\n");

    mprint ("[Timing mode: %s] ", use_gop_as_pts ? "GOP": "default");
    mprint ("[Debug: %s] ", (debug_mask & DMT_VERBOSE) ? "Yes": "No");
    mprint ("[Buffer input: %s]\n", buffer_input ? "Yes": "No");
    mprint ("[Use pic_order_cnt_lsb for H.264: %s] ", usepicorder ? "Yes": "No");
    mprint ("[Print CC decoder traces: %s]\n", (debug_mask & DMT_608) ? "Yes": "No");
    mprint ("[Target format: %s] ",extension);    
    mprint ("[Encoding: ");
    switch (encoding)
    {
        case ENC_UNICODE:
            mprint ("Unicode");
            break;
        case ENC_UTF_8:
            mprint ("UTF-8");
            break;
        case ENC_LATIN_1:
            mprint ("Latin-1");
            break;
    }
    mprint ("] ");
    mprint ("[Delay: %lld] ",subs_delay);    

    mprint ("[Trim lines: %s]\n",trim_subs?"Yes":"No");
    mprint ("[Add font color data: %s] ", nofontcolor? "No" : "Yes");
	mprint ("[Add font typesetting: %s]\n", notypesetting? "No" : "Yes");
    mprint ("[Convert case: ");
    if (sentence_cap_file!=NULL)
        mprint ("Yes, using %s", sentence_cap_file);
    else
    {
        mprint ("%s",sentence_cap?"Yes, but only built-in words":"No");
    }
    mprint ("]");
    mprint (" [Video-edit join: %s]", binary_concat?"No":"Yes");
    mprint ("\n[Extraction start time: ");
    if (extraction_start.set==0)
        mprint ("not set (from start)");
    else
        mprint ("%02d:%02d:%02d", extraction_start.hh,
        extraction_start.mm,extraction_start.ss);
    mprint ("]\n");
    mprint ("[Extraction end time: ");
    if (extraction_end.set==0)
        mprint ("not set (to end)");
    else
        mprint ("%02d:%02d:%02d", extraction_end.hh,extraction_end.mm,
        extraction_end.ss);
    mprint ("]\n");
    mprint ("[Live stream: ");
    if (live_stream==0)
        mprint ("No");
    else
    {
        if (live_stream<1)
            mprint ("Yes, no timeout");
        else
            mprint ("Yes, timeout: %d seconds",live_stream);
    }
    mprint ("] [Clock frequency: %d]\n",MPEG_CLOCK_FREQ);
	mprint ("Teletext page: ");
	if (tlt_config.page)
		mprint ("%d]\n",tlt_config.page);
	else
		mprint ("Autodetect]\n");
    mprint ("Start credits text: [%s]\n", start_credits_text?start_credits_text:"None");
    if (start_credits_text)
    {
        mprint ("Start credits time: Insert between [%ld] and [%ld] seconds\n",
            (long) (startcreditsnotbefore.time_in_ms/1000), 
            (long) (startcreditsnotafter.time_in_ms/1000)
            );
        mprint ("                    Display for at least [%ld] and at most [%ld] seconds\n",
            (long) (startcreditsforatleast.time_in_ms/1000), 
            (long) (startcreditsforatmost.time_in_ms/1000)
            );
    }
    if (end_credits_text)
    {
        mprint ("End credits text: [%s]\n", end_credits_text?end_credits_text:"None");
        mprint ("                    Display for at least [%ld] and at most [%ld] seconds\n",
            (long) (endcreditsforatleast.time_in_ms/1000), 
            (long) (endcreditsforatmost.time_in_ms/1000)
            );
    }

}
