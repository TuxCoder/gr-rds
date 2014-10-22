/*
 * Copyright (C) 2014 Bastian Bloessl <bloessl@ccs-labs.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define dout debug && std::cout
#define lout log && std::cout

#include "parser_impl.h"
#include "constants.h"
#include "tmc_events.h"
#include <gnuradio/io_signature.h>
#include <math.h>

using namespace gr::rds;

parser::sptr
parser::make(bool log, bool debug) {
  return gnuradio::get_initial_sptr(new parser_impl(log, debug));
}

parser_impl::parser_impl(bool log, bool debug)
	: gr::block ("gr_rds_parser",
			gr::io_signature::make (0, 0, 0),
			gr::io_signature::make (0, 0, 0)),
	log(log),
	debug(debug)
{
	message_port_register_in(pmt::mp("in"));
	set_msg_handler(pmt::mp("in"), boost::bind(&parser_impl::parse, this, _1));
	message_port_register_out(pmt::mp("out"));
	reset();
}

parser_impl::~parser_impl() {
}

void parser_impl::reset() {
	gr::thread::scoped_lock lock(d_mutex);

	memset(radiotext, ' ', sizeof(radiotext));
	memset(program_service_name, ' ', sizeof(program_service_name));

	radiotext[64]                  = '\0';
	radiotext_AB_flag              = 0;
	traffic_program                = false;
	traffic_announcement           = false;
	music_speech                   = false;
	program_type                   = 0;
	pi_country_identification      = 0;
	pi_area_coverage               = 0;
	pi_program_reference_number    = 0;
	program_service_name[8]        = '\0';
	mono_stereo                    = false;
	artificial_head                = false;
	compressed                     = false;
	static_pty                     = false;
}

/* type 0 = PI
 * type 1 = PS
 * type 2 = PTY
 * type 3 = flagstring: TP, TA, MuSp, MoSt, AH, CMP, stPTY
 * type 4 = RadioText 
 * type 5 = ClockTime
 * type 6 = Alternative Frequencies */
void parser_impl::send_message(long msgtype, std::string msgtext) {
	pmt::pmt_t msg  = pmt::mp(msgtext);
	pmt::pmt_t type = pmt::from_long(msgtype);
	message_port_pub(pmt::mp("out"), pmt::make_tuple(type, msg));
        //std::cout << "RDS: " << msgtext << std::endl;
}

/* BASIC TUNING: see page 21 of the standard */
void parser_impl::decode_type0(unsigned int *group, bool version_code) {
	unsigned int af_code_1 = 0;
	unsigned int af_code_2 = 0;
	unsigned int  no_af    = 0;
	double af_1            = 0;
	double af_2            = 0;
	char flagstring[8]     = "0000000";
	
	traffic_program        = (group[1] >> 10) & 0x01;       // "TP"
	traffic_announcement   = (group[1] >>  4) & 0x01;       // "TA"
	music_speech           = (group[1] >>  3) & 0x01;       // "MuSp"

	bool decoder_control_bit      = (group[1] >> 2) & 0x01; // "DI"
	unsigned char segment_address =  group[1] & 0x03;       // "DI segment"

	program_service_name[segment_address * 2]     = (group[3] >> 8) & 0xff;
	program_service_name[segment_address * 2 + 1] =  group[3]       & 0xff;

	/* see page 41, table 9 of the standard */
	switch (segment_address) {
		case 0:
			mono_stereo=decoder_control_bit;
		break;
		case 1:
			artificial_head=decoder_control_bit;
		break;
		case 2:
			compressed=decoder_control_bit;
		break;
		case 3:
			static_pty=decoder_control_bit;
		break;
		default:
		break;
	}
	flagstring[0] = traffic_program        ? '1' : '0';
	flagstring[1] = traffic_announcement   ? '1' : '0';
	flagstring[2] = music_speech           ? '1' : '0';
	flagstring[3] = mono_stereo            ? '1' : '0';
	flagstring[4] = artificial_head        ? '1' : '0';
	flagstring[5] = compressed             ? '1' : '0';
	flagstring[6] = static_pty             ? '1' : '0';
	if(!version_code) {     // type 0A
		af_code_1 = (int)(group[2] >> 8) & 0xff;
		af_code_2 = (int) group[2]       & 0xff;

		if((af_1 = decode_af(af_code_1))) no_af += 1;
		if((af_2 = decode_af(af_code_2))) no_af += 2;
/* only AF1 => no_af==1, only AF2 => no_af==2, both AF1 and AF2 => no_af==3 */
		memset(af1_string, ' ', sizeof(af1_string));
		memset(af2_string, ' ', sizeof(af2_string));
		memset(af_string, ' ', sizeof(af_string));
		af1_string[9]=af2_string[9]=af_string[20]='\0';
		if (no_af) {
			if (af_1>80e3) sprintf(af1_string, "%2.2fMHz", af_1/1e3);
			else if ((af_1<2e3)&&(af_1>100)) sprintf(af1_string, "%ikHz", (int)af_1);
			if (af_2>80e3) sprintf(af2_string, "%2.2fMHz", af_2/1e3);
			else if ((af_2<2e3)&&(af_2>100)) sprintf(af2_string, "%ikHz", (int)af_2);
		}
		if (no_af==1) strcpy(af_string, af1_string);
		else if (no_af==2) strcpy(af_string, af2_string);
		else if (no_af==3){
			strcpy(af_string, af1_string);
			strcat(af_string, ", ");
			strcat(af_string, af2_string);
		}
	}

/* let's print out what we've got so far */
	std::cout << "==>" << program_service_name << "<== -" << (traffic_program?"TP":"  ") 
		<< '-' << (traffic_announcement?"TA":"  ") << '-' << (music_speech?"Music":"Speech") 
		<< '-' << (mono_stereo?"MONO":"STEREO") << " - AF:" << af_string << std::endl;

/* sending the messages to the queue */
	send_message(1,program_service_name);
	send_message(3,flagstring);
	send_message(6,af_string);
}

/* see page 41 in the standard
 * this is an implementation of AF method A */
double parser_impl::decode_af(unsigned int af_code) {
	static unsigned int number_of_freqs;
	double alt_frequency=0;				// in kHz
	static bool vhf_or_lfmf=0;			// 0 = vhf, 1 = lf/mf

/* in all the following cases the message either tells us
 * that there are no alternative frequencies, or it indicates
 * the number of AF to follow, which is not relevant at this
 * stage, since we're not actually re-tuning */
	if ((af_code == 0)||					// not to be used
		(af_code == 205)||				// filler code
		((af_code >= 206)&&(af_code <= 223))||		// not assigned
		(af_code == 224)||				// No AF exists
		(af_code >= 251)){				// not assigned
			number_of_freqs = 0;
			alt_frequency=0;
	}
	if ((af_code >= 225)&&(af_code <= 249)){	// VHF frequencies follow
		number_of_freqs = af_code - 224;
		alt_frequency=0;
		vhf_or_lfmf = 1;
	}
	if (af_code == 250) {				// an LF/MF frequency follows
		number_of_freqs = 1;
		alt_frequency=0;
		vhf_or_lfmf = 0;
	}

/* here we're actually decoding the alternative frequency */
	if ((af_code > 0) && (af_code < 205) && vhf_or_lfmf)
		alt_frequency = (double)(af_code+875)*100;		// VHF (87.6-107.9MHz)
	else if ((af_code > 0) && (af_code < 16) && !vhf_or_lfmf)
		alt_frequency = (double)((af_code-1)*9 + 153);		// LF (153-279kHz)
	else if ((af_code > 15) && (af_code < 136) && !vhf_or_lfmf)
		alt_frequency = (double)((af_code-16)*9 + 531);		// MF (531-1602kHz)

	return alt_frequency;						// in kHz
}

/* SLOW LABELLING: see page 23 in the standard 
 * for paging see page 90, Annex M in the standard (NOT IMPLEMENTED)
 * for extended country codes see page 69, Annex D.2 in the standard
 * for language codes see page 84, Annex J in the standard
 * for emergency warning systems (EWS) see page 53 in the standard */
void parser_impl::decode_type1(unsigned int *group, bool version_code){
	int ecc=0, paging=0;

	char country_code=(group[0]>>12)&0x0f;
	char radio_paging_codes=group[1]&0x1f;
	//bool linkage_actuator=(group[2]>>15)&0x1;
	int variant_code=(group[2]>>12)&0x7;
	unsigned int slow_labelling=group[2]&0xfff;
	int day=(int)((group[3]>>11)&0x1f);
	int hour=(int)((group[3]>>6)&0x1f);
	int minute=(int)(group[3]&0x3f);

	if (radio_paging_codes)
		printf("paging codes: %i ", (int)radio_paging_codes);
	if (day||hour||minute)
		printf("program item: %id, %i:%i ", day, hour, minute);

	if(!version_code){
		switch(variant_code){
			case 0:			// paging + ecc
				paging=(slow_labelling>>8)&0x0f;
				ecc=slow_labelling&0xff;
				if(paging) printf("paging:%x ", paging);
				if((ecc>223)&&(ecc<229))
					std::cout << "extended country code:" << 
						pi_country_codes[country_code-1][ecc-224] << std::endl;
				else printf("invalid extended country code:%i\n", ecc);
				break;
			case 1:			// TMC identification
				printf("TMC identification code received\n");
				break;
			case 2:			// Paging identification
				printf("Paging identification code received\n");
				break;
			case 3:			// language codes
				if (slow_labelling<44)
					std::cout << "language: " << language_codes[slow_labelling] << std::endl;
				else
					printf("language: invalid language code (%i)\n", slow_labelling);
				break;
			default:
				break;
		}
	}
}

/* RADIOTEXT: page 25 in the standard */
void parser_impl::decode_type2(unsigned int *group, bool version_code){
	unsigned char text_segment_address_code=group[1]&0x0f;

/* when the A/B flag is toggled, flush your current radiotext */
	if (radiotext_AB_flag!=((group[1]>>4)&0x01)) {
//		send_message(4,radiotext);
		for(int i=0; i<64; i++) radiotext[i]=' ';
		radiotext[64] = '\0';
	}
	radiotext_AB_flag=(group[1]>>4)&0x01;

	if (!version_code) {
		radiotext[text_segment_address_code*4]=(group[2]>>8)&0xff;
		radiotext[text_segment_address_code*4+1]=group[2]&0xff;
		radiotext[text_segment_address_code*4+2]=(group[3]>>8)&0xff;
		radiotext[text_segment_address_code*4+3]=group[3]&0xff;
	}
	else {
		radiotext[text_segment_address_code*2]=(group[3]>>8)&0xff;
		radiotext[text_segment_address_code*2+1]=group[3]&0xff;
	}
	printf("Radio Text %c: %s\n", (radiotext_AB_flag?'B':'A'), radiotext);
	send_message(4,radiotext);
}

/* AID: page 27 in the standard */
void parser_impl::decode_type3a(unsigned int *group){
	int application_group=(group[1]>>1)&0xf;
	int group_type=group[1]&0x1;
	int message=group[2];
	int aid=group[3];
	
	printf("aid group: %i%c - ", application_group, group_type?'B':'A');
	if((application_group==8)&&(group_type==false)){	// 8A
		int variant_code=(message>>14)&0x3;
		if(variant_code==0){
			int ltn=(message>>6)&0x3f;	// location table number
			bool afi=(message>>5)&0x1;	// alternative freq. indicator
			bool M=(message>>4)&0x1;	// mode of transmission
			bool I=(message>>3)&0x1;	// international
			bool N=(message>>2)&0x1;	// national
			bool R=(message>>1)&0x1;	// regional
			bool U=message&0x1;			// urban
			std::cout << "location table: " << ltn << " - "
				<< (afi?"AFI-ON":"AFI-OFF") << " - "
				<< (M?"enhanced mode":"basic mode") << " - "
				<< (I?"international ":"")
				<< (N?"national ":"")
				<< (R?"regional ":"")
				<< (U?"urban":"") 
				<< " aid: " << aid << std::endl;
		}
		else if(variant_code==1){
			int G=(message>>12)&0x3;	// gap
			int sid=(message>>6)&0x3f;	// service identifier
			int gap_no[4]={3,5,8,11};
			printf("gap:%i groups, SID:%02X\n", gap_no[G], sid);
		}
	}
	else printf("message: %04X - aid: %04X\n", message, aid);
}

/* CLOCKTIME: see page 28 of the standard, as well as annex G, page 81 */
void parser_impl::decode_type4a(unsigned int *group){
	unsigned int hours=((group[2]&0x1)<<4)|((group[3]>>12)&0x0f);
	unsigned int minutes=(group[3]>>6)&0x3f;
	double local_time_offset=((double)(group[3]&0x1f))/2;
	if((group[3]>>5)&0x1) local_time_offset *= -1;
	double modified_julian_date=((group[1]&0x03)<<15)|((group[2]>>1)&0x7fff);

/* MJD -> Y-M-D */
	unsigned int year=(int)((modified_julian_date-15078.2)/365.25);
	unsigned int month=(int)((modified_julian_date-14956.1-(int)(year*365.25))/30.6001);
	unsigned int day_of_month=modified_julian_date-14956-(int)(year*365.25)-(int)(month*30.6001);
	bool K=((month==14)||(month==15))?1:0;
	year+=K;
	month-=1+K*12;

// concatenate into a string, print and send message
	for (int i=0; i<32; i++) clocktime_string[i]=' ';
	clocktime_string[32]='\0';
	sprintf(clocktime_string, "%02i.%02i.%4i, %02i:%02i (%+.1fh)",
		(int)day_of_month, month, (1900+year), hours, minutes, local_time_offset);
	std::cout << "Clocktime: " << clocktime_string << std::endl;
	send_message(5,clocktime_string);
}

/* TMC: see page 32 of the standard
   initially defined in CEN standard ENV 12313-1
   superseded by ISO standard 14819:2003 */
void parser_impl::decode_type8a(unsigned int *group){
	bool T=(group[1]>>4)&0x1;		// 0 = user message, 1 = tuning info
	bool F=(group[1]>>3)&0x1;		// 0 = multi-group, 1 = single-group
	bool D=(group[2]>15)&0x1;		// 1 = diversion recommended
	static unsigned long int free_format[4];
	static int no_groups=0;

	if(T==true){	// tuning info
		printf("#tuning info# ");
		int variant=group[1]&0xf;
		if((variant>3)&&(variant<10)){
			printf("variant: %i - ", variant);
			printf("%04X %04X\n", group[2], group[3]);
		}
		else printf("invalid variant: %i\n", variant);
	}
	else if((F==true)||((F==false)&&(D==true))){		// single-group or 1st of multi-group
		unsigned int dp_ci=group[1]&0x7;		// duration & persistence or continuity index
		bool sign=(group[2]>>14)&0x1;			// event direction, 0 = +, 1 = -
		unsigned int extent=(group[2]>>11)&0x7;		// number of segments affected
		unsigned int event=group[2]&0x7ff; 		// event code, defined in ISO 14819-2
		unsigned int location=group[3];			// location code, defined in ISO 14819-3
		std::cout << "#user msg# " << (D?"diversion recommended, ":"");
		if(F)
			std::cout << "single-grp, duration:" << tmc_duration[dp_ci][0];
		else
			std::cout << "multi-grp, continuity index:" << dp_ci;
		int event_line=tmc_event_code_index[event][1];
		std::cout << ", extent:" << (sign?"-":"") << extent+1 << " segments"
			<< ", event" << event << ":" << tmc_events[event_line][1]
			<< ", location:" << location << std::endl;
	}
	else{	// 2nd or more of multi-group
		unsigned int ci=group[1]&0x7;			// countinuity index
		bool sg=(group[2]>>14)&0x1;			// second group
		unsigned int gsi=(group[2]>>12)&0x3;		// group sequence
		std::cout << "#user msg# multi-grp, continuity index:" << ci
			<< (sg?", second group":"") << ", gsi:" << gsi;
		printf(", free format: %03X %04X\n", (group[2]&0xfff), group[3]);
		// it's not clear if gsi=N-2 when gs=true
		if(sg)
			no_groups=gsi;
		free_format[gsi]=((group[2]&0xfff)<<12)|group[3];
		if (gsi==0)
			decode_optional_content(no_groups, free_format);
	}
}

void parser_impl::decode_optional_content(int no_groups, unsigned long int *free_format){
	int label=0;
	int content=0;
	int content_length=0;
	int ff_pointer;
	
	for (int i=no_groups;i==0;i--){
		ff_pointer=12+16;
		while(ff_pointer>0){
			ff_pointer-=4;
			label=(free_format[i]&&(0xf<<ff_pointer));
			content_length=optional_content_lengths[label];
			ff_pointer-=content_length;
			content=(free_format[i]&&((int)(pow(2, content_length)-1)<<ff_pointer));
			std::cout << "TMC optional content (" << label_descriptions[label]
				<< "):" << content << std::endl;
		}
	}
}

/* EON: see pages 38 and 46 in the standard */
void parser_impl::decode_type14(unsigned int *group, bool version_code){
	
	bool tp_on=(group[1]>>4)&0x01;
	char variant_code=group[1]&0x0f;
	unsigned int information=group[2];
	unsigned int pi_on=group[3];
	
	char pty_on=0;
	bool ta_on=0;
	static char ps_on[9]={' ',' ',' ',' ',' ',' ',' ',' ','\0'};
	double af_1=0, af_2=0;
	
	if (!version_code){
		switch (variant_code){
			case 0:			// PS(ON)
			case 1:			// PS(ON)
			case 2:			// PS(ON)
			case 3:			// PS(ON)
				ps_on[variant_code*2]=(information>>8)&0xff;
				ps_on[variant_code*2+1]=information&0xff;
				printf("PS(ON): ==>%8s<==", ps_on);
			break;
			case 4:			// AF
				af_1 = (double)(((information>>8)&0xff)+875)*100;
				af_2 = (double)((information&0xff)+875)*100;
				printf("AF:%3.2fMHz %3.2fMHz", af_1/1000, af_2/1000);
			break;
			case 5:			// mapped frequencies
			case 6:			// mapped frequencies
			case 7:			// mapped frequencies
			case 8:			// mapped frequencies
				af_1 = (double)(((information>>8)&0xff)+875)*100;
				af_2 = (double)((information&0xff)+875)*100;
				printf("TN:%3.2fMHz - ON:%3.2fMHz", af_1/1000, af_2/1000);
			break;
			case 9:			// mapped frequencies (AM)
				af_1 = (double)(((information>>8)&0xff)+875)*100;
				af_2 = (double)(((information&0xff)-16)*9 + 531);
				printf("TN:%3.2fMHz - ON:%ikHz", af_1/1000, (int)af_2);
			break;
			case 10:		// unallocated
			break;
			case 11:		// unallocated
			break;
			case 12:		// linkage information
				printf("Linkage information: %x%x", ((information>>8)&0xff), (information&0xff));
			break;
			case 13:		// PTY(ON), TA(ON)
				ta_on=information&0x01;
				pty_on=(information>>11)&0x1f;
				std::cout << "PTY(ON):" << pty_table[(int)pty_on];
				if (ta_on) printf(" - TA");
			break;
			case 14:		// PIN(ON)
				printf("PIN(ON):%x%x", ((information>>8)&0xff), (information&0xff));
			break;
			case 15:		// Reserved for broadcasters use
			break;
			default:
				printf("invalid variant code:%i", variant_code);
			break;
		}
	}
	if (pi_on){
		printf(" PI(ON):%i", pi_on);
		if (tp_on) printf("-TP-");
	}
	std::cout << std::endl;
}

/* FAST BASIC TUNING: see page 39 in the standard */
void parser_impl::decode_type15b(unsigned int *group){
/* here we get twice the first two blocks... nothing to be done */
	printf("\n");
}

void parser_impl::parse(pmt::pmt_t msg) {
	if(!pmt::is_blob(msg)) {
		lout << "wrong input message (no blob)" << std::endl;
	}
	if(pmt::blob_length(msg) != 4 * sizeof(unsigned long)) {
		lout << "input message has wrong size ("
			<< pmt::blob_length(msg) << ")" << std::endl;
	}
	unsigned int *group = (unsigned int*)pmt::blob_data(msg);
	unsigned int group_type=(unsigned int)((group[1]>>12)&0xf);
	bool version_code=(group[1]>>11)&0x1;
	printf("%02i%c ", group_type, (version_code?'B':'A'));
	std::cout << "(" << rds_group_acronyms[group_type] << ")";

	program_identification=group[0];			// "PI"
	program_type=(group[1]>>5)&0x1f;			// "PTY"
	int pi_country_identification=(program_identification>>12)&0xf;
	int pi_area_coverage=(program_identification>>8)&0xf;
	unsigned char pi_program_reference_number=program_identification&0xff;
	char pistring[5];
	sprintf(pistring,"%04X",program_identification);
	send_message(0,pistring);
	send_message(2,pty_table[program_type]);

/* page 69, Annex D in the standard */
	std::cout << " - PI:" << pistring << " - " << "PTY:" << pty_table[program_type];
	std::cout << " (country:" << pi_country_codes[pi_country_identification-1][0];
	std::cout << "/" << pi_country_codes[pi_country_identification-1][1];
	std::cout << "/" << pi_country_codes[pi_country_identification-1][2];
	std::cout << "/" << pi_country_codes[pi_country_identification-1][3];
	std::cout << "/" << pi_country_codes[pi_country_identification-1][4];
	std::cout << ", area:" << coverage_area_codes[pi_area_coverage];
	std::cout << ", program:" << (int)pi_program_reference_number << ")" << std::endl;

	switch (group_type) {
		case 0:
			//decode_type0(group, version_code);
		break;
		case 1:
			decode_type1(group, version_code);
		break;
		case 2:
			//decode_type2(group, version_code);
		break;
		case 3:
			if(!version_code) decode_type3a(group);
		break;
		case 4:
			//if(!version_code) decode_type4a(group);
		break;
		case 5:
		break;
		case 6:
		break;
		case 7:
		break;
		case 8:
			//if(!version_code) decode_type8a(group);
		break;
		case 9:
		break;
		case 10:
		break;
		case 11:
		break;
		case 12:
		break;
		case 13:
		break;
		case 14:
			//decode_type14(group, version_code);
		break;
		case 15:
			//if(version_code) decode_type15b(group);
		break;
		default:
			printf("DECODE ERROR!!! (group_type=%u)\n",group_type);
		break;
	}
}
