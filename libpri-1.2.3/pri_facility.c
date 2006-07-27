/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Matthew Fredrickson <creslin@digium.com>
 *
 * Copyright (C) 2004-2005, Digium
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include "compat.h"
#include "libpri.h"
#include "pri_internal.h"
#include "pri_q921.h"
#include "pri_q931.h"
#include "pri_facility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char get_invokeid(struct pri *pri)
{
	return ++pri->last_invoke;
}

struct addressingdataelements_presentednumberunscreened {
	char partyaddress[21];
	char partysubaddress[21];
	int  npi;
	int  ton;
	int  pres;
};

static void dump_apdu(struct pri *pri, unsigned char *c, int len) 
{
	#define MAX_APDU_LENGTH	255
	int i;
	char message[(2 + MAX_APDU_LENGTH * 3 + 6 + MAX_APDU_LENGTH + 3)] = "";	/* please adjust here, if you make changes below! */
	
	if (len > MAX_APDU_LENGTH)
		return;
	
	snprintf(message, sizeof(message)-1, " [");	
	for (i=0; i<len; i++)
		snprintf((char *)(message+strlen(message)), sizeof(message)-strlen(message)-1, " %02x", c[i]);
	snprintf((char *)(message+strlen(message)), sizeof(message)-strlen(message)-1, " ] - [");
	for (i=0; i<len; i++) {
		if (c[i] < 20 || c[i] >= 128)
			snprintf((char *)(message+strlen(message)), sizeof(message)-strlen(message)-1, "°");
		else
			snprintf((char *)(message+strlen(message)), sizeof(message)-strlen(message)-1, "%c", c[i]);
	}
	snprintf((char *)(message+strlen(message)), sizeof(message)-strlen(message)-1, "]\n");
	pri_message(pri, message);
}

int redirectingreason_from_q931(struct pri *pri, int redirectingreason)
{
	switch(pri->switchtype) {
		case PRI_SWITCH_QSIG:
			switch(redirectingreason) {
				case PRI_REDIR_UNKNOWN:
					return QSIG_DIVERT_REASON_UNKNOWN;
				case PRI_REDIR_FORWARD_ON_BUSY:
					return QSIG_DIVERT_REASON_CFB;
				case PRI_REDIR_FORWARD_ON_NO_REPLY:
					return QSIG_DIVERT_REASON_CFNR;
				case PRI_REDIR_UNCONDITIONAL:
					return QSIG_DIVERT_REASON_CFU;
				case PRI_REDIR_DEFLECTION:
				case PRI_REDIR_DTE_OUT_OF_ORDER:
				case PRI_REDIR_FORWARDED_BY_DTE:
					pri_message(pri, "!! Don't know how to convert Q.931 redirection reason %d to Q.SIG\n", redirectingreason);
					/* Fall through */
				default:
					return QSIG_DIVERT_REASON_UNKNOWN;
			}
		default:
			switch(redirectingreason) {
				case PRI_REDIR_UNKNOWN:
					return Q952_DIVERT_REASON_UNKNOWN;
				case PRI_REDIR_FORWARD_ON_BUSY:
					return Q952_DIVERT_REASON_CFB;
				case PRI_REDIR_FORWARD_ON_NO_REPLY:
					return Q952_DIVERT_REASON_CFNR;
				case PRI_REDIR_DEFLECTION:
					return Q952_DIVERT_REASON_CD;
				case PRI_REDIR_UNCONDITIONAL:
					return Q952_DIVERT_REASON_CFU;
				case PRI_REDIR_DTE_OUT_OF_ORDER:
				case PRI_REDIR_FORWARDED_BY_DTE:
					pri_message(pri, "!! Don't know how to convert Q.931 redirection reason %d to Q.952\n", redirectingreason);
					/* Fall through */
				default:
					return Q952_DIVERT_REASON_UNKNOWN;
			}
	}
}

static int redirectingreason_for_q931(struct pri *pri, int redirectingreason)
{
	switch(pri->switchtype) {
		case PRI_SWITCH_QSIG:
			switch(redirectingreason) {
				case QSIG_DIVERT_REASON_UNKNOWN:
					return PRI_REDIR_UNKNOWN;
				case QSIG_DIVERT_REASON_CFU:
					return PRI_REDIR_UNCONDITIONAL;
				case QSIG_DIVERT_REASON_CFB:
					return PRI_REDIR_FORWARD_ON_BUSY;
				case QSIG_DIVERT_REASON_CFNR:
					return PRI_REDIR_FORWARD_ON_NO_REPLY;
				default:
					pri_message(pri, "!! Unknown Q.SIG diversion reason %d\n", redirectingreason);
					return PRI_REDIR_UNKNOWN;
			}
		default:
			switch(redirectingreason) {
				case Q952_DIVERT_REASON_UNKNOWN:
					return PRI_REDIR_UNKNOWN;
				case Q952_DIVERT_REASON_CFU:
					return PRI_REDIR_UNCONDITIONAL;
				case Q952_DIVERT_REASON_CFB:
					return PRI_REDIR_FORWARD_ON_BUSY;
				case Q952_DIVERT_REASON_CFNR:
					return PRI_REDIR_FORWARD_ON_NO_REPLY;
				case Q952_DIVERT_REASON_CD:
					return PRI_REDIR_DEFLECTION;
				case Q952_DIVERT_REASON_IMMEDIATE:
					pri_message(pri, "!! Dont' know how to convert Q.952 diversion reason IMMEDIATE to PRI analog\n");
					return PRI_REDIR_UNKNOWN;	/* ??? */
				default:
					pri_message(pri, "!! Unknown Q.952 diversion reason %d\n", redirectingreason);
					return PRI_REDIR_UNKNOWN;
			}
	}
}

int typeofnumber_from_q931(struct pri *pri, int ton)
{
	switch(ton) {
		case PRI_TON_INTERNATIONAL:
			return Q932_TON_INTERNATIONAL;
		case PRI_TON_NATIONAL:
			return Q932_TON_NATIONAL;
		case PRI_TON_NET_SPECIFIC:
			return Q932_TON_NET_SPECIFIC;
		case PRI_TON_SUBSCRIBER:
			return Q932_TON_SUBSCRIBER;
		case PRI_TON_ABBREVIATED:
			return Q932_TON_ABBREVIATED;
		case PRI_TON_RESERVED:
		default:
			pri_message(pri, "!! Unsupported Q.931 TypeOfNumber value (%d)\n", ton);
			/* fall through */
		case PRI_TON_UNKNOWN:
			return Q932_TON_UNKNOWN;
	}
}

static int typeofnumber_for_q931(struct pri *pri, int ton)
{
	switch (ton) {
		case Q932_TON_UNKNOWN:
			return PRI_TON_UNKNOWN;
		case Q932_TON_INTERNATIONAL:
			return PRI_TON_INTERNATIONAL;
		case Q932_TON_NATIONAL:
			return PRI_TON_NATIONAL;
		case Q932_TON_NET_SPECIFIC:
			return PRI_TON_NET_SPECIFIC;
		case Q932_TON_SUBSCRIBER:
			return PRI_TON_SUBSCRIBER;
		case Q932_TON_ABBREVIATED:
			return PRI_TON_ABBREVIATED;
		default:
			pri_message(pri, "!! Invalid Q.932 TypeOfNumber %d\n", ton);
			return PRI_TON_UNKNOWN;
	}
}

int asn1_name_decode(void * data, int len, char *namebuf, int buflen)
{
	struct rose_component *comp = (struct rose_component*)data;
	int datalen = 0, res = 0;

	if (comp->len == ASN1_LEN_INDEF) {
		datalen = strlen((char *)comp->data);
		res = datalen + 2;
	} else
		datalen = res = comp->len;

	if (datalen > buflen) {
		/* Truncate */
		datalen = buflen;
	}
	memcpy(namebuf, comp->data, datalen);
	return res + 2;
}

int asn1_string_encode(unsigned char asn1_type, void *data, int len, int max_len, void *src, int src_len)
{
	struct rose_component *comp = NULL;
	
	if (len < 2 + src_len)
		return -1;

	if (max_len && (src_len > max_len))
		src_len = max_len;

	comp = (struct rose_component *)data;
	comp->type = asn1_type;
	comp->len = src_len;
	memcpy(comp->data, src, src_len);
	
	return 2 + src_len;
}

int asn1_copy_string(char * buf, int buflen, struct rose_component *comp)
{
	int res;
	int datalen;

	if ((comp->len > buflen) && (comp->len != ASN1_LEN_INDEF))
		return -1;

	if (comp->len == ASN1_LEN_INDEF) {
		datalen = strlen((char*)comp->data);
		res = datalen + 2;
	} else
		res = datalen = comp->len;

	memcpy(buf, comp->data, datalen);
	buf[datalen] = 0;

	return res;
}

static int rose_number_digits_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int datalen = 0;
	int res = 0;

	do {
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_NUMERICSTRING, "Don't know what to do with PublicPartyNumber ROSE component type 0x%x\n");
		if(comp->len > 20 && comp->len != ASN1_LEN_INDEF) {
			pri_message(pri, "!! Oversized NumberDigits component (%d)\n", comp->len);
			return -1;
		}
		if (comp->len == ASN1_LEN_INDEF) {
			datalen = strlen((char *)comp->data);
			res = datalen + 2;
		} else
			res = datalen = comp->len;
			
		memcpy(value->partyaddress, comp->data, datalen);
		value->partyaddress[datalen] = '\0';

		return res + 2;
	}
	while(0);
	
	return -1;
}

static int rose_public_party_number_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int ton;
	int res = 0;

	if (len < 2)
		return -1;

	do {
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Don't know what to do with PublicPartyNumber ROSE component type 0x%x\n");
		ASN1_GET_INTEGER(comp, ton);
		NEXT_COMPONENT(comp, i);
		ton = typeofnumber_for_q931(pri, ton);

		res = rose_number_digits_decode(pri, call, &vdata[i], len-i, value);
		if (res < 0)
			return -1;
		value->ton = ton;

		return res + 3;

	} while(0);
	return -1;
}

static int rose_address_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;
	int res = 0;

	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):	/* [0] unknownPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_UNKNOWN;
			value->ton = PRI_TON_UNKNOWN;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0):	/* [0] unknownPartyNumber */
			res = asn1_copy_string(value->partyaddress, sizeof(value->partyaddress), comp);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_UNKNOWN;
			value->ton = PRI_TON_UNKNOWN;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):	/* [1] publicPartyNumber */
			res = rose_public_party_number_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_E163_E164;
			break;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):	/* [2] nsapEncodedNumber */
			pri_message(pri, "!! NsapEncodedNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):	/* [3] dataPartyNumber */
			if(rose_number_digits_decode(pri, call, comp->data, comp->len, value))
				return -1;
			value->npi = PRI_NPI_X121 /* ??? */;
			value->ton = PRI_TON_UNKNOWN /* ??? */;
			pri_message(pri, "!! dataPartyNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_4):	/* [4] telexPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_F69 /* ??? */;
			value->ton = PRI_TON_UNKNOWN /* ??? */;
			pri_message(pri, "!! telexPartyNumber isn't handled\n");
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_5):	/* [5] priavePartyNumber */
			pri_message(pri, "!! privatePartyNumber isn't handled\n");
			value->npi = PRI_NPI_PRIVATE;
			return -1;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_8):	/* [8] nationalStandardPartyNumber */
			res = rose_number_digits_decode(pri, call, comp->data, comp->len, value);
			if (res < 0)
				return -1;
			value->npi = PRI_NPI_NATIONAL;
			value->ton = PRI_TON_NATIONAL;
			break;
		default:
			pri_message(pri, "!! Unknown Party number component received 0x%X\n", comp->type);
			return -1;
		}
		ASN1_FIXUP_LEN(comp, res);
		NEXT_COMPONENT(comp, i);
		if(i < len)
			pri_message(pri, "!! not all information is handled from Address component\n");
		return res + 2;
	}
	while (0);

	return -1;
}

static int rose_presented_number_unscreened_decode(struct pri *pri, q931_call *call, unsigned char *data, int len, struct addressingdataelements_presentednumberunscreened *value)
{
	int i = 0;
	int size = 0;
	struct rose_component *comp = NULL;
	unsigned char *vdata = data;

	/* Fill in default values */
	value->ton = PRI_TON_UNKNOWN;
	value->npi = PRI_NPI_E163_E164;
	value->pres = -1;	/* Data is not available */

	do {
		GET_COMPONENT(comp, i, vdata, len);

		switch(comp->type) {
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0):		/* [0] presentationAllowedNumber */
			value->pres = PRES_ALLOWED_USER_NUMBER_NOT_SCREENED;
			size = rose_address_decode(pri, call, comp->data, comp->len, value);
			ASN1_FIXUP_LEN(comp, size);
			return size + 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1):		/* [1] IMPLICIT presentationRestricted */
			if (comp->len != 0) { /* must be NULL */
				pri_error(pri, "!! Invalid PresentationRestricted component received (len != 0)\n");
				return -1;
			}
			value->pres = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
			return 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2):		/* [2] IMPLICIT numberNotAvailableDueToInterworking */
			if (comp->len != 0) { /* must be NULL */
				pri_error(pri, "!! Invalid NumberNotAvailableDueToInterworking component received (len != 0)\n");
				return -1;
			}
			value->pres = PRES_NUMBER_NOT_AVAILABLE;
			return 2;
		case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):		/* [3] presentationRestrictedNumber */
			value->pres = PRES_PROHIB_USER_NUMBER_NOT_SCREENED;
			size = rose_address_decode(pri, call, comp->data, comp->len, value) + 2;
			ASN1_FIXUP_LEN(comp, size);
			return size + 2;
		default:
			pri_message(pri, "Invalid PresentedNumberUnscreened component 0x%X\n", comp->type);
		}
		return -1;
	}
	while (0);

	return -1;
}

static int rose_diverting_leg_information2_decode(struct pri *pri, q931_call *call, struct rose_component *sequence, int len)
{
	int i = 0;
	int diversion_counter;
	int diversion_reason;
	char origcalledname[50] = "", redirectingname[50] = "";
	struct addressingdataelements_presentednumberunscreened divertingnr;
 	struct addressingdataelements_presentednumberunscreened originalcallednr;
	struct rose_component *comp = NULL;
	unsigned char *vdata = sequence->data;
	int res = 0;

	/* Data checks */
	if (sequence->type != (ASN1_CONSTRUCTOR | ASN1_SEQUENCE)) { /* Constructed Sequence */
		pri_message(pri, "Invalid DivertingLegInformation2Type argument\n");
		return -1;
	}

	if (sequence->len == ASN1_LEN_INDEF) {
		len -= 4; /* For the 2 extra characters at the end
                           * and two characters of header */
	} else
		len -= 2;

	do {
		/* diversionCounter stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_INTEGER, "Don't know what to do it diversionCounter is of type 0x%x\n");
		ASN1_GET_INTEGER(comp, diversion_counter);
		NEXT_COMPONENT(comp, i);

		/* diversionReason stuff */
		GET_COMPONENT(comp, i, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "Invalid diversionReason type 0x%X of ROSE divertingLegInformation2 component received\n");
		ASN1_GET_INTEGER(comp, diversion_reason);
		NEXT_COMPONENT(comp, i);

		diversion_reason = redirectingreason_for_q931(pri, diversion_reason);
	
		if(pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "    Redirection reason: %d, total diversions: %d\n", diversion_reason, diversion_counter);
		pri_message(NULL, "Length of message is %d\n", len);

		for(; i < len; NEXT_COMPONENT(comp, i)) {
			GET_COMPONENT(comp, i, vdata, len);
			switch(comp->type) {
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0):
				call->origredirectingreason = redirectingreason_for_q931(pri, comp->data[0]);
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received reason for original redirection %d\n", call->origredirectingreason);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):
				res = rose_presented_number_unscreened_decode(pri, call, comp->data, comp->len, &divertingnr);
				/* TODO: Fix indefinite length form hacks */
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (res < 0)
					return -1;
				if (pri->debug & PRI_DEBUG_APDU) {
					pri_message(pri, "    Received divertingNr '%s'\n", divertingnr.partyaddress);
					pri_message(pri, "      ton = %d, pres = %d, npi = %d\n", divertingnr.ton, divertingnr.pres, divertingnr.npi);
				}
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):
				res = rose_presented_number_unscreened_decode(pri, call, comp->data, comp->len, &originalcallednr);
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU) {
					pri_message(pri, "    Received originalcallednr '%s'\n", originalcallednr.partyaddress);
					pri_message(pri, "      ton = %d, pres = %d, npi = %d\n", originalcallednr.ton, originalcallednr.pres, originalcallednr.npi);
				}
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_3):
				res = asn1_name_decode(comp->data, comp->len, redirectingname, sizeof(redirectingname));
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received RedirectingName '%s'\n", redirectingname);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_4):
				res = asn1_name_decode(comp->data, comp->len, origcalledname, sizeof(origcalledname));
				if (res < 0)
					return -1;
				ASN1_FIXUP_LEN(comp, res);
				comp->len = res;
				if (pri->debug & PRI_DEBUG_APDU)
					pri_message(pri, "    Received Originally Called Name '%s'\n", origcalledname);
				break;
			default:
				if (comp->type == 0 && comp->len == 0) {
					break; /* Found termination characters */
				}
				pri_message(pri, "!! Invalid DivertingLegInformation2 component received 0x%X\n", comp->type);
				return -1;
			}
		}

		if (divertingnr.pres >= 0) {
			call->redirectingplan = divertingnr.npi;
			call->redirectingpres = divertingnr.pres;
			call->redirectingreason = diversion_reason;
			libpri_copy_string(call->redirectingnum, divertingnr.partyaddress, sizeof(call->redirectingnum));
		}
		if (originalcallednr.pres >= 0) {
			call->origcalledplan = originalcallednr.npi;
			call->origcalledpres = originalcallednr.pres;
			libpri_copy_string(call->origcallednum, originalcallednr.partyaddress, sizeof(call->origcallednum));
		}
		libpri_copy_string(call->redirectingname, redirectingname, sizeof(call->redirectingname));
		libpri_copy_string(call->origcalledname, origcalledname, sizeof(call->origcalledname));
		return 0;
	}
	while (0);

	return -1;
}
				
static int rose_diverting_leg_information2_encode(struct pri *pri, q931_call *call)
{
	int i = 0, j, compsp = 0;
	struct rose_component *comp, *compstk[10];
	unsigned char buffer[256];
	int len = 253;
	
	if (!strlen(call->callername)) {
		return -1;
	}

	buffer[i] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	i++;
	/* Interpretation component */
	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0x00 /* Discard unrecognized invokes */);
	
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	
	ASN1_PUSH(compstk, compsp, comp);
	/* Invoke component contents */
	/*	Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));
	/*	Operation Tag */
	
	/* ROSE operationId component */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, ROSE_DIVERTING_LEG_INFORMATION2);

	/* ROSE ARGUMENT component */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* ROSE DivertingLegInformation2.diversionCounter component */
	/* Always is 1 because other isn't available in the current design */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, 1);
	
	/* ROSE DivertingLegInformation2.diversionReason component */
	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, redirectingreason_from_q931(pri, call->redirectingreason));
		
	/* ROSE DivertingLegInformation2.divertingNr component */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
	
	ASN1_PUSH(compstk, compsp, comp);
		/* Redirecting information always not screened */
	
	switch(call->redirectingpres) {
		case PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		case PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
			if (call->redirectingnum && strlen(call->redirectingnum)) {
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
					/* NPI of redirected number is not supported in the current design */
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
					ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, typeofnumber_from_q931(pri, call->redirectingplan >> 4));
					j = asn1_string_encode(ASN1_NUMERICSTRING, &buffer[i], len - i, 20, call->redirectingnum, strlen(call->redirectingnum));
				if (j < 0)
					return -1;
					
				i += j;
				ASN1_FIXUP(compstk, compsp, buffer, i);
				ASN1_FIXUP(compstk, compsp, buffer, i);
				break;
			}
			/* fall through */
		case PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		/* Don't know how to handle this */
		case PRES_ALLOWED_NETWORK_NUMBER:
		case PRES_PROHIB_NETWORK_NUMBER:
		case PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		default:
			pri_message(pri, "!! Undefined presentation value for redirecting number: %d\n", call->redirectingpres);
		case PRES_NUMBER_NOT_AVAILABLE:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i);
			break;
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);

	/* ROSE DivertingLegInformation2.originalCalledNr component */
	/* This information isn't supported by current design - duplicate divertingNr */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
		/* Redirecting information always not screened */
	switch(call->redirectingpres) {
		case PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		case PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
			if (call->redirectingnum && strlen(call->redirectingnum)) {
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
				ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
				ASN1_PUSH(compstk, compsp, comp);
				ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, typeofnumber_from_q931(pri, call->redirectingplan >> 4));
	
				j = asn1_string_encode(ASN1_NUMERICSTRING, &buffer[i], len - i, 20, call->redirectingnum, strlen(call->redirectingnum));
				if (j < 0)
					return -1;
				
				i += j;
				ASN1_FIXUP(compstk, compsp, buffer, i);
				ASN1_FIXUP(compstk, compsp, buffer, i);
				break;
			}
				/* fall through */
		case PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		/* Don't know how to handle this */
		case PRES_ALLOWED_NETWORK_NUMBER:
		case PRES_PROHIB_NETWORK_NUMBER:
		case PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		case PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
			break;
		default:
			pri_message(pri, "!! Undefined presentation value for redirecting number: %d\n", call->redirectingpres);
		case PRES_NUMBER_NOT_AVAILABLE:
			ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i);
			break;
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);
		
	/* Fix length of stacked components */
	while(compsp > 0) {
		ASN1_FIXUP(compstk, compsp, buffer, i);
	}
	
	if (pri_call_apdu_queue(call, Q931_SETUP, buffer, i, NULL, NULL))
		return -1;
		
	return 0;
}

/* Sending callername information functions */
static int add_callername_facility_ies(struct pri *pri, q931_call *c, int cpe)
{
	int res = 0;
	int i = 0;
	unsigned char buffer[256];
	unsigned char namelen = 0;
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	int mymessage = 0;
	static unsigned char op_tag[] = { 
		0x2a, /* informationFollowing 42 */
		0x86,
		0x48,
		0xce,
		0x15,
		0x00,
		0x04
	};
		
	if (!strlen(c->callername)) {
		return -1;
	}

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	/* Operation Tag */
	res = asn1_string_encode(ASN1_OBJECTIDENTIFIER, &buffer[i], sizeof(buffer)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;

	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (!cpe) {
		if (pri_call_apdu_queue(c, Q931_SETUP, buffer, i, NULL, NULL))
			return -1;
	}


	/* Now the ADPu that contains the information that needs sent.
	 * We can reuse the buffer since the queue function doesn't
	 * need it. */

	i = 0;
	namelen = strlen(c->callername);
	if (namelen > 50) {
		namelen = 50; /* truncate the name */
	}

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	/* Invoke ID */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	/* Operation ID: Calling name */
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, SS_CNID_CALLINGNAME);

	res = asn1_string_encode((ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), &buffer[i], sizeof(buffer)-i,  50, c->callername, namelen);
	if (res < 0)
		return -1;
	i += res;
	ASN1_FIXUP(compstk, compsp, buffer, i);

	if (cpe) 
		mymessage = Q931_SETUP;
	else
		mymessage = Q931_FACILITY;

	if (pri_call_apdu_queue(c, mymessage, buffer, i, NULL, NULL))
		return -1;
	
	return 0;
}
/* End Callername */

/* MWI related encode and decode functions */
static void mwi_activate_encode_cb(void *data)
{
	return;
}

extern int mwi_message_send(struct pri* pri, q931_call *call, struct pri_sr *req, int activate)
{
	int i = 0;
	unsigned char buffer[255] = "";
	int destlen = strlen(req->called);
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	int res;

	if (destlen <= 0) {
		return -1;
	} else if (destlen > 20)
		destlen = 20;  /* Destination number cannot be greater then 20 digits */

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, (activate) ? SS_MWI_ACTIVATE : SS_MWI_DEACTIVATE);
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	/* PartyNumber */
	res = asn1_string_encode((ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), &buffer[i], sizeof(buffer)-i, destlen, req->called, destlen);
	
	if (res < 0)
		return -1;
	i += res;

	/* Enumeration: basicService */
	ASN1_ADD_BYTECOMP(comp, ASN1_ENUMERATED, buffer, i, 1 /* contents: Voice */);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	return pri_call_apdu_queue(call, Q931_SETUP, buffer, i, mwi_activate_encode_cb, NULL);
}
/* End MWI */

/* EECT functions */
extern int eect_initiate_transfer(struct pri *pri, q931_call *c1, q931_call *c2)
{
	/* Did all the tests to see if we're on the same PRI and
	 * are on a compatible switchtype */
	/* TODO */
	int i = 0;
	int res = 0;
	unsigned char buffer[255] = "";
	unsigned short call_reference = c2->cr;
	struct rose_component *comp = NULL, *compstk[10];
	int compsp = 0;
	static unsigned char op_tag[] = {
		0x2A,
		0x86,
		0x48,
		0xCE,
		0x15,
		0x00,
		0x08,
	};

	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_EXTENSIONS);
	/* Interpretation component */

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_NFE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_0), buffer, i, 0);
	ASN1_ADD_BYTECOMP(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_2), buffer, i, 0);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	ASN1_ADD_BYTECOMP(comp, COMP_TYPE_INTERPRETATION, buffer, i, 0);

	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, get_invokeid(pri));

	res = asn1_string_encode(ASN1_OBJECTIDENTIFIER, &buffer[i], sizeof(buffer)-i, sizeof(op_tag), op_tag, sizeof(op_tag));
	if (res < 0)
		return -1;
	i += res;

	ASN1_ADD_SIMPLE(comp, (ASN1_SEQUENCE | ASN1_CONSTRUCTOR), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);
	ASN1_ADD_WORDCOMP(comp, ASN1_INTEGER, buffer, i, call_reference);
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i);

	res = pri_call_apdu_queue(c1, Q931_FACILITY, buffer, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue ADPU in facility message\n");
		return -1;
	}

	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */

	res = q931_facility(c1->pri, c1);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c1->cr);
		return -1;
	}

	return 0;
}
/* End EECT */

/* AOC */
static int aoc_aoce_charging_request_decode(struct pri *pri, q931_call *call, unsigned char *data, int len) 
{
	int chargingcase = -1;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL;
	int pos1 = 0;

	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, data, len);

	do {
		GET_COMPONENT(comp, pos1, vdata, len);
		CHECK_COMPONENT(comp, ASN1_ENUMERATED, "!! Invalid AOC Charging Request argument. Expected Enumerated (0x0A) but Received 0x%02X\n");
		ASN1_GET_INTEGER(comp, chargingcase);				
		if (chargingcase >= 0 && chargingcase <= 2) {
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "Channel %d/%d, Call %d  - received AOC charging request - charging case: %i\n", 
					call->ds1no, call->channelno, call->cr, chargingcase);
		} else {
			pri_message(pri, "!! unkown AOC ChargingCase: 0x%02X", chargingcase);
			chargingcase = -1;
		}
		NEXT_COMPONENT(comp, pos1);
	} while (pos1 < len);
	if (pos1 < len) {
		pri_message(pri, "!! Only reached position %i in %i bytes long AOC-E structure:", pos1, len );
		dump_apdu (pri, data, len);
		return -1;	/* Aborted before */
	}
	return 0;
}
	

static int aoc_aoce_charging_unit_decode(struct pri *pri, q931_call *call, unsigned char *data, int len) 
{
	long chargingunits = 0, chargetype = -1, temp, chargeIdentifier = -1;
	unsigned char *vdata = data;
	struct rose_component *comp1 = NULL, *comp2 = NULL, *comp3 = NULL;
	int pos1 = 0, pos2, pos3, sublen2, sublen3;
	struct addressingdataelements_presentednumberunscreened chargednr;

	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, data, len);

	do {
		GET_COMPONENT(comp1, pos1, vdata, len);	/* AOCEChargingUnitInfo */
		CHECK_COMPONENT(comp1, ASN1_SEQUENCE, "!! Invalid AOC-E Charging Unit argument. Expected Sequence (0x30) but Received 0x%02X\n");
		SUB_COMPONENT(comp1, pos1);
		GET_COMPONENT(comp1, pos1, vdata, len);
		switch (comp1->type) {
			case (ASN1_SEQUENCE | ASN1_CONSTRUCTOR):	/* specificChargingUnits */
				sublen2 = comp1->len; 
				pos2 = pos1;
				comp2 = comp1;
				SUB_COMPONENT(comp2, pos2);
				do {
					GET_COMPONENT(comp2, pos2, vdata, len);
					switch (comp2->type) {
						case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1):	/* RecordedUnitsList (0xA1) */
							SUB_COMPONENT(comp2, pos2);
							GET_COMPONENT(comp2, pos2, vdata, len);
							CHECK_COMPONENT(comp2, ASN1_SEQUENCE, "!! Invalid AOC-E Charging Unit argument. Expected Sequence (0x30) but received 0x02%X\n");	/* RecordedUnits */
							sublen3 = pos2 + comp2->len;
							pos3 = pos2;
							comp3 = comp2;
							SUB_COMPONENT(comp3, pos3);
							do {
								GET_COMPONENT(comp3, pos3, vdata, len);
								switch (comp3->type) {
									case ASN1_INTEGER:	/* numberOfUnits */
										ASN1_GET_INTEGER(comp3, temp);
										chargingunits += temp;
									case ASN1_NULL:		/* notAvailable */
										break;
									default:
										pri_message(pri, "!! Don't know how to handle 0x%02X in AOC-E RecordedUnits\n", comp3->type);
								}
								NEXT_COMPONENT(comp3, pos3);
							} while (pos3 < sublen3);
							if (pri->debug & PRI_DEBUG_AOC)
								pri_message(pri, "Channel %d/%d, Call %d - received AOC-E charging: %i unit%s\n", 
									call->ds1no, call->channelno, call->cr, chargingunits, (chargingunits == 1) ? "" : "s");
							break;
						case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_2):	/* AOCEBillingID (0xA2) */
							SUB_COMPONENT(comp2, pos2);
							GET_COMPONENT(comp2, pos2, vdata, len);
							ASN1_GET_INTEGER(comp2, chargetype);
							pri_message(pri, "!! not handled: Channel %d/%d, Call %d - received AOC-E billing ID: %i\n", 
								call->ds1no, call->channelno, call->cr, chargetype);
							break;
						default:
							pri_message(pri, "!! Don't know how to handle 0x%02X in AOC-E RecordedUnitsList\n", comp2->type);
					}
					NEXT_COMPONENT(comp2, pos2);
				} while (pos2 < sublen2);
				break;
			case (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1): /* freeOfCharge (0x81) */
				if (pri->debug & PRI_DEBUG_AOC)
					pri_message(pri, "Channel %d/%d, Call %d - received AOC-E free of charge\n", call->ds1no, call->channelno, call->cr);
				chargingunits = 0;
				break;
			default:
				pri_message(pri, "!! Invalid AOC-E specificChargingUnits. Expected Sequence (0x30) or Object Identifier (0x81/0x01) but received 0x%02X\n", comp1->type);
		}
		NEXT_COMPONENT(comp1, pos1);
		GET_COMPONENT(comp1, pos1, vdata, len); /* get optional chargingAssociation. will 'break' when reached end of structure */
		switch (comp1->type) {
			/* TODO: charged number is untested - please report! */
			case (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_0): /* chargedNumber (0xA0) */
				if(rose_presented_number_unscreened_decode(pri, call, comp1->data, comp1->len, &chargednr) != 0)
					return -1;
				pri_message(pri, "!! not handled: Received ChargedNr '%s' \n", chargednr.partyaddress);
				pri_message(pri, "  ton = %d, pres = %d, npi = %d\n", chargednr.ton, chargednr.pres, chargednr.npi);
				break;
			case ASN1_INTEGER:
				ASN1_GET_INTEGER(comp1, chargeIdentifier);
				break;
			default:
				pri_message(pri, "!! Invalid AOC-E chargingAssociation. Expected Object Identifier (0xA0) or Integer (0x02) but received 0x%02X\n", comp1->type);
		}
		NEXT_COMPONENT(comp1, pos1);
	} while (pos1 < len);

	if (pos1 < len) {
		pri_message(pri, "!! Only reached position %i in %i bytes long AOC-E structure:", pos1, len );
		dump_apdu (pri, data, len);
		return -1;	/* oops - aborted before */
	}
	call->aoc_units = chargingunits;
	
	return 0;
}

static int aoc_aoce_charging_unit_encode(struct pri *pri, q931_call *c, long chargedunits)
{
	/* sample data: [ 91 a1 12 02 02 3a 78 02 01 24 30 09 30 07 a1 05 30 03 02 01 01 ] */
	int i = 0, res = 0, compsp = 0;
	unsigned char buffer[255] = "";
	struct rose_component *comp = NULL, *compstk[10];

	/* ROSE protocol (0x91)*/
	buffer[i++] = (ASN1_CONTEXT_SPECIFIC | Q932_PROTOCOL_ROSE);

	/* ROSE Component (0xA1,len)*/
	ASN1_ADD_SIMPLE(comp, COMP_TYPE_INVOKE, buffer, i);
	ASN1_PUSH(compstk, compsp, comp); 

	/* ROSE invokeId component (0x02,len,id)*/
	ASN1_ADD_WORDCOMP(comp, INVOKE_IDENTIFIER, buffer, i, ++pri->last_invoke);

	/* ROSE operationId component (0x02,0x01,0x24)*/
	ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, ROSE_AOC_AOCE_CHARGING_UNIT);

	/* AOCEChargingUnitInfo (0x30,len) */
	ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
	ASN1_PUSH(compstk, compsp, comp);

	if (chargedunits > 0) {
		/* SpecificChargingUnits (0x30,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);

		/* RecordedUnitsList (0xA1,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_CONSTRUCTOR | ASN1_TAG_1), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		
		/* RecordedUnits (0x30,len) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONSTRUCTOR | ASN1_SEQUENCE), buffer, i);
		ASN1_PUSH(compstk, compsp, comp);
		
		/* NumberOfUnits (0x02,len,charge) */
		ASN1_ADD_BYTECOMP(comp, ASN1_INTEGER, buffer, i, chargedunits);

		ASN1_FIXUP(compstk, compsp, buffer, i);
		ASN1_FIXUP(compstk, compsp, buffer, i);
		ASN1_FIXUP(compstk, compsp, buffer, i);
	} else {
		/* freeOfCharge (0x81,0) */
		ASN1_ADD_SIMPLE(comp, (ASN1_CONTEXT_SPECIFIC | ASN1_TAG_1), buffer, i);
	}
	ASN1_FIXUP(compstk, compsp, buffer, i);
	ASN1_FIXUP(compstk, compsp, buffer, i); 
	
	if (pri->debug & PRI_DEBUG_AOC)
		dump_apdu (pri, buffer, i);
		
	/* code below is untested */
	res = pri_call_apdu_queue(c, Q931_FACILITY, buffer, i, NULL, NULL);
	if (res) {
		pri_message(pri, "Could not queue ADPU in facility message\n");
		return -1;
	}

	/* Remember that if we queue a facility IE for a facility message we
	 * have to explicitly send the facility message ourselves */
	res = q931_facility(c->pri, c);
	if (res) {
		pri_message(pri, "Could not schedule facility message for call %d\n", c->cr);
		return -1;
	}

	return 0;
}
/* End AOC */

extern int rose_invoke_decode(struct pri *pri, q931_call *call, unsigned char *data, int len)
{
	int i = 0;
	int operation_tag;
	unsigned char *vdata = data;
	struct rose_component *comp = NULL, *invokeid = NULL, *operationid = NULL;
	
	do {
		/* Invoke ID stuff */
		GET_COMPONENT(comp, i, vdata, len);
#if 0
		CHECK_COMPONENT(comp, INVOKE_IDENTIFIER, "Don't know what to do if first ROSE component is of type 0x%x\n");
#endif
		invokeid = comp;
		NEXT_COMPONENT(comp, i);

		/* Operation Tag */
		GET_COMPONENT(comp, i, vdata, len);
#if 0
		CHECK_COMPONENT(comp, ASN1_INTEGER, "Don't know what to do if second ROSE component is of type 0x%x\n");
#endif
		operationid = comp;
		ASN1_GET_INTEGER(comp, operation_tag);
		NEXT_COMPONENT(comp, i);

		/* No argument - return with error */
		if (i >= len) 
			return -1;

		/* Arguement Tag */
		GET_COMPONENT(comp, i, vdata, len);
		if (!comp->type)
			return -1;

		if (pri->debug & PRI_DEBUG_APDU)
			pri_message(pri, "  [ Handling operation %d ]\n", operation_tag);
		switch (operation_tag) {
		case SS_CNID_CALLINGNAME:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "  Handle Name display operation\n");
			switch (comp->type) {
				case ROSE_NAME_PRESENTATION_ALLOWED_SIMPLE:
					memcpy(call->callername, comp->data, comp->len);
					call->callername[comp->len] = 0;
					if (pri->debug & PRI_DEBUG_APDU)
						pri_message(pri, "    Received caller name '%s'\n", call->callername);
					return 0;
				default:
					if (pri->debug & PRI_DEBUG_APDU)
						pri_message(pri, "Do not handle argument of type 0x%X\n", comp->type);
					return -1;
			}
			break;
		case ROSE_DIVERTING_LEG_INFORMATION2:
			if (pri->debug & PRI_DEBUG_APDU)
				pri_message(pri, "  Handle DivertingLegInformation2\n");
			return rose_diverting_leg_information2_decode(pri, call, comp, len-i);
		case ROSE_AOC_NO_CHARGING_INFO_AVAILABLE:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC No Charging Info Available - not handled!", operation_tag);
				dump_apdu (pri, comp->data, comp->len);
			}
			return -1;
		case ROSE_AOC_CHARGING_REQUEST:
			return aoc_aoce_charging_request_decode(pri, call, (u_int8_t *)comp, comp->len + 2);
		case ROSE_AOC_AOCS_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-S Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCS_SPECIAL_ARR:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-S Special Array - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCD_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-D Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCD_CHARGING_UNIT:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-D Charging Unit - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCE_CURRENCY:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC-E Currency - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		case ROSE_AOC_AOCE_CHARGING_UNIT:
			return aoc_aoce_charging_unit_decode(pri, call, (u_int8_t *)comp, comp->len + 2);
			if (0) { /* the following function is currently not used - just to make the compiler happy */
				aoc_aoce_charging_unit_encode(pri, call, call->aoc_units); /* use this function to forward the aoc-e on a bridged channel */ 
				return 0;
			}
		case ROSE_AOC_IDENTIFICATION_OF_CHARGE:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "ROSE %i: AOC Identification Of Charge - not handled!", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		default:
			if (pri->debug & PRI_DEBUG_APDU) {
				pri_message(pri, "!! Unable to handle ROSE operation %d", operation_tag);
				dump_apdu (pri, (u_int8_t *)comp, comp->len + 2);
			}
			return -1;
		}
	} while(0);
	
	return -1;
}

extern int pri_call_apdu_queue(q931_call *call, int messagetype, void *apdu, int apdu_len, void (*function)(void *data), void *data)
{
	struct apdu_event *cur = NULL;
	struct apdu_event *new_event = NULL;

	if (!call || !messagetype || !apdu || (apdu_len < 1) || (apdu_len > 255))
		return -1;

	new_event = malloc(sizeof(struct apdu_event));

	if (new_event) {
		memset(new_event, 0, sizeof(struct apdu_event));
		new_event->message = messagetype;
		new_event->callback = function;
		new_event->data = data;
		memcpy(new_event->apdu, apdu, apdu_len);
		new_event->apdu_len = apdu_len;
	} else {
		pri_error(call->pri, "!! Malloc failed!\n");
		return -1;
	}
	
	if (call->apdus) {
		cur = call->apdus;
		while (cur->next) {
			cur = cur->next;
		}
		cur->next = new_event;
	} else
		call->apdus = new_event;

	return 0;
}

extern int pri_call_apdu_queue_cleanup(q931_call *call)
{
	struct apdu_event *cur_event = NULL, *free_event = NULL;

	if (call && call->apdus) {
		cur_event = call->apdus;
		while (cur_event) {
			/* TODO: callbacks, some way of giving return res on status of apdu */
			free_event = cur_event;
			cur_event = cur_event->next;
			free(free_event);
		}
		call->apdus = NULL;
	}

	return 0;
}

extern int pri_call_add_standard_apdus(struct pri *pri, q931_call *call)
{
	if (!pri->sendfacility)
		return 0;

	if (pri->switchtype == PRI_SWITCH_QSIG) { /* For Q.SIG it does network and cpe operations */
		if (call->redirectingnum[0]) 
			rose_diverting_leg_information2_encode(pri, call);
		add_callername_facility_ies(pri, call, 1);
		return 0;
	}

	if (pri->localtype == PRI_NETWORK) {
		switch (pri->switchtype) {
			case PRI_SWITCH_NI2:
				add_callername_facility_ies(pri, call, 0);
				break;
			default:
				break;
		}
		return 0;
	} else if (pri->localtype == PRI_CPE) {
		switch (pri->switchtype) {
			case PRI_SWITCH_NI2:
				add_callername_facility_ies(pri, call, 1);
				break;
			default:
				break;
		}
		return 0;
	}

	return 0;
}

