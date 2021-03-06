/*
 * BSD Telephony Of Mexico "Tormenta" Tone Zone Support 2/22/01
 * 
 * Working with the "Tormenta ISA" Card 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under thet erms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * Primary Author: Mark Spencer <markster@linux-support.net>
 *
 * This information from ITU E.180 Supplement 2.
 * UK information from BT SIN 350 Issue 1.1
 */
#include <sys/types.h>

#include "tonezone.h"

struct tone_zone builtin_zones[]  =
{
	{ 0, "us", "United States / North America", { 2000, 4000 }, 
	{
		{ ZT_TONE_DIALTONE, "350+440" },
		{ ZT_TONE_BUSY, "480+620/500,0/500" },
		{ ZT_TONE_RINGTONE, "440+480/2000,0/4000" },
		{ ZT_TONE_CONGESTION, "480+620/250,0/250" },
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		{ ZT_TONE_DIALRECALL, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" } },
	},
	{ 1, "au", "Australia", {  400, 200, 400, 2000 },
	{
		{ ZT_TONE_DIALTONE, "413+438" },		
		{ ZT_TONE_BUSY, "425/375,0/375" },
		{ ZT_TONE_RINGTONE, "413+438/400,0/200,413+438/400,0/2000" },
		/* XXX Congestion: Should reduce by 10 db every other cadence XXX */
		{ ZT_TONE_CONGESTION, "425/375,0/375,420/375,8/375" }, 
		{ ZT_TONE_CALLWAIT, "425/100,0/200,425/200,0/4400" },
		{ ZT_TONE_DIALRECALL, "413+428" },
		{ ZT_TONE_RECORDTONE, "!425/1000,!0/15000,425/360,0/15000" },
		{ ZT_TONE_INFO, "425/2500,0/500" },
		{ ZT_TONE_STUTTER, "413+438/100,0/40" } },
	},
	{ 2, "fr", "France", { 1500, 3500 },
	{
		/* Dialtone can also be 440+330 */
		{ ZT_TONE_DIALTONE, "440" },
		{ ZT_TONE_BUSY, "440/500,0/500" },
		{ ZT_TONE_RINGTONE, "440/1500,0/3500" },
		/* XXX I'm making up the congestion tone XXX */
		{ ZT_TONE_CONGESTION, "440/250,0/250" },
		/* XXX I'm making up the call wait tone too XXX */
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		/* XXX I'm making up dial recall XXX */
		{ ZT_TONE_DIALRECALL, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" },
		/* XXX I'm making up the record tone XXX */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "!440/100,!0/100,!440/100,!0/100,!440/100,!0/100,!440/100,!0/100,!440/100,!0/100,!440/100,!0/100,440" } },
	},
	{ 3, "nl", "Netherlands", { 1000, 4000 },
	{
		/* Most of these 425's can also be 450's */
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/500,0/500" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/250,0/250" },
		/* XXX I'm making up the call wait tone XXX */
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		/* XXX Assuming this is "Special Dial Tone" XXX */
		{ ZT_TONE_DIALRECALL, "425/500,0/50" },
		/* XXX I'm making up the record tone XXX */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "425/500,0/50" } },
	},
	{ 4, "uk", "United Kingdom", { 400, 200, 400, 2000 },
	{
		{ ZT_TONE_DIALTONE, "350+440" },
		{ ZT_TONE_BUSY, "400/375,0/375" },
		{ ZT_TONE_RINGTONE, "400+450/400,0/200,400+450/400,0/2000" },
		{ ZT_TONE_CONGESTION, "400/400,0/350,400/225,0/525" },
		{ ZT_TONE_CALLWAIT, "440/100,0/4000" },
		{ ZT_TONE_DIALRECALL, "350+440" },
		/* Not sure about the RECORDTONE */
		{ ZT_TONE_RECORDTONE, "1400/500,0/10000" },
		{ ZT_TONE_INFO, "950/330,1400/330,1800/330,0" },
		{ ZT_TONE_STUTTER, "350+440" } },
	},
	{ 5, "fi", "Finland", { 1000, 4000 },
        {
                { ZT_TONE_DIALTONE, "425" },
                { ZT_TONE_BUSY, "425/300,0/300" },
                { ZT_TONE_RINGTONE, "425/1000,0/4000" },
                { ZT_TONE_CONGESTION, "425/200,0/200" },
                { ZT_TONE_CALLWAIT, "425/150,0/150,425/150,0/8000" },
                { ZT_TONE_DIALRECALL, "425/650,0/25" },
                { ZT_TONE_RECORDTONE, "1400/500,0/15000" },
                { ZT_TONE_INFO, "950/650,0/325,950/325,0/30,1400/1300,0/2600" } },
        },
	{ 6,"es","Spain", { 1500, 3000},
	{
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/200,0/200" },
		{ ZT_TONE_RINGTONE, "425/1500,0/3000" },
		{ ZT_TONE_CONGESTION, "425/200,0/200,425/200,0/200,425/200,0/600" },
		{ ZT_TONE_CALLWAIT, "425/175,0/175,425/175,0/3500" },
		{ ZT_TONE_DIALRECALL, "!425/200,!0/200,!425/200,!0/200,!425/200,!0/200,425" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "950/330,0/1000" },
		{ ZT_TONE_STUTTER, "425/500,0/50" } },
	},
	{ 7,"jp","Japan", { 1000, 2000 },
	{
		{ ZT_TONE_DIALTONE, "400" },
		{ ZT_TONE_BUSY, "400/500,0/500" },
		{ ZT_TONE_RINGTONE, "400+15/1000,0/2000" },
		{ ZT_TONE_CONGESTION, "400/500,0/500" },
		{ ZT_TONE_CALLWAIT, "400+16/500,0/8000" },
		{ ZT_TONE_DIALRECALL, "!400/200,!0/200,!400/200,!0/200,!400/200,!0/200,400" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,400" } },
	},
	{ 8,"no","Norway", { 1000, 4000 },
	{
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/500,0/500" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/200,0/200" },
		{ ZT_TONE_CALLWAIT, "425/200,0/600,425/200,0/10000" },
		{ ZT_TONE_DIALRECALL, "470/400,425/400" },
		{ ZT_TONE_RECORDTONE, "1400/400,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,0" },
		{ ZT_TONE_STUTTER, "470/400,425/400" } },
	},
	{ 9, "at", "Austria", { 1000, 5000 },
	{
		{ ZT_TONE_DIALTONE, "440" },
		{ ZT_TONE_BUSY, "440/400,0/400" },
		{ ZT_TONE_RINGTONE, "440/1000,0/5000" },
		{ ZT_TONE_CONGESTION, "440/200,440/200" },
		{ ZT_TONE_CALLWAIT, "440/40,0/1950" },
		/*XXX what is this? XXX*/
		{ ZT_TONE_DIALRECALL, "425/500,0/50" },
		/*XXX hmm? XXX*/
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1450/330,!1850/330,0/1000" },
		{ ZT_TONE_STUTTER, "350+430" } },
	},
	{ 10, "nz", "New Zealand", { 400, 200, 400, 2000 },
	{
		{ ZT_TONE_DIALTONE, "400" },
		{ ZT_TONE_BUSY, "400/500,0/500" },
		{ ZT_TONE_RINGTONE, "400+450/400,0/200,400+450/400,0/2000" },
		{ ZT_TONE_CONGESTION, "400/250,0/250" },
		{ ZT_TONE_CALLWAIT, "400/250,0/250,400/250,0/3250" },
		{ ZT_TONE_DIALRECALL, "!400/100!0/100,!400/100,!0/100,!400/100,!0/100,400" },
	        { ZT_TONE_RECORDTONE, "1400/425,0/15000" },
		{ ZT_TONE_INFO, "400/750,0/100,400/750,0/100,400/750,0/100,400/750,0/400" },
		{ ZT_TONE_STUTTER, "!400/100!0/100,!400/100,!0/100,!400/100,!0/100,!400/100!0/100,!400/100,!0/100,!400/100,!0/100,400" } },
	},
	{ 11,"it","Italy", { 1000, 4000 },
	{
		{ ZT_TONE_DIALTONE, "425/600,0/1000,425/200,0/200" },
		{ ZT_TONE_BUSY, "425/500,0/500" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/200,0/200" },
		{ ZT_TONE_CALLWAIT, "425/200,0/600,425/200,0/10000" },
		{ ZT_TONE_DIALRECALL, "470/400,425/400" },
		{ ZT_TONE_RECORDTONE, "1400/400,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,0" },
		{ ZT_TONE_STUTTER, "470/400,425/400" } },
	},
	{ 12, "us-old", "United States Circa 1950/ North America", { 2000, 4000 }, 
	{
		{ ZT_TONE_DIALTONE, "600*120" },
		{ ZT_TONE_BUSY, "500*100/500,0/500" },
		{ ZT_TONE_RINGTONE, "420*40/2000,0/4000" },
		{ ZT_TONE_CONGESTION, "500*100/250,0/250" },
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		{ ZT_TONE_DIALRECALL, "!600*120/100,!0/100,!600*120/100,!0/100,!600*120/100,!0/100,600*120" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "!600*120/100,!0/100,!600*120/100,!0/100,!600*120/100,!0/100,!600*120/100,!0/100,!600*120/100,!0/100,!600*120/100,!0/100,600*120" } },
	},
	{ 13, "gr", "Greece", { 1000, 4000 },
	{
		{ ZT_TONE_DIALTONE, "425/200,0/300,425/700,0/800" },
		{ ZT_TONE_BUSY, "425/300,0/300" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/200,0/200" },
		{ ZT_TONE_CALLWAIT, "425/150,0/150,425/150,0/8000" },
		{ ZT_TONE_DIALRECALL, "425/650,0/25" },
		{ ZT_TONE_RECORDTONE, "1400/400,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,!950/330,!1400/330,!1800/330,!0/1000,0" },
		{ ZT_TONE_STUTTER, "425/650,0/25" } },
	},
	{ 14, "tw", "Taiwan", { 1000, 4000 },
	{
		{ ZT_TONE_DIALTONE, "350+440" },
		{ ZT_TONE_BUSY, "480+620/500,0/500" },
		{ ZT_TONE_RINGTONE, "440+480/1000,0/2000" },
		{ ZT_TONE_CONGESTION, "480+620/250,0/250" },
		{ ZT_TONE_CALLWAIT, "350+440/250,0/250,350+440/250,0/3250" },
		{ ZT_TONE_DIALRECALL, "300/1500,0/500" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" },
		{ ZT_TONE_STUTTER, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" } },
	},
	{ 15, "cl", "Chile", { 1000, 3000 }, 
	{
		{ ZT_TONE_DIALTONE, "400" },
		{ ZT_TONE_BUSY, "400/500,0/500" },
		{ ZT_TONE_RINGTONE, "400/1000,0/3000" },
		{ ZT_TONE_CONGESTION, "400/200,0/200" },
		{ ZT_TONE_CALLWAIT, "400/250,0/8750" },
		{ ZT_TONE_DIALRECALL, "!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,400" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/333,!1400/333,!1800/333,0" },
		{ ZT_TONE_STUTTER, "!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,!400/100,!0/100,400" } },
	},
	{ 16, "se", "Sweden", { 1000, 5000 }, 
	{
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/250,0/250" },
		{ ZT_TONE_RINGTONE, "425/1000,0/5000" },
		{ ZT_TONE_CONGESTION, "425/250,0/750" },
		{ ZT_TONE_CALLWAIT, "425/200,0/500,425/200,0/9100" },
		{ ZT_TONE_DIALRECALL, "!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,425" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/332,!0/24,!1400/332,!0/24,!1800/332,!0/2024,"
                                "!950/332,!0/24,!1400/332,!0/24,!1800/332,!0/2024,"
                                "!950/332,!0/24,!1400/332,!0/24,!1800/332,!0/2024,"
                                "!950/332,!0/24,!1400/332,!0/24,!1800/332,!0/2024,"
                                "!950/332,!0/24,!1400/332,!0/24,!1800/332,0" },
		/*{ ZT_TONE_STUTTER, "425/320,0/20" },              Real swedish standard, not used for now */
		{ ZT_TONE_STUTTER, "!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,425" } },
	},
	{ 17, "be", "Belgium", { 1000, 3000 },
	{
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/500,0/500" },
		{ ZT_TONE_RINGTONE, "425/1000,0/3000" },
		{ ZT_TONE_CONGESTION, "425/167,0/167" },
		{ ZT_TONE_CALLWAIT, "1400/175,0/175,1400/175,0/3500" },
		/* DIALRECALL - not specified */
		{ ZT_TONE_DIALRECALL, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" },
		/* RECORDTONE - not specified */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "950/330,1400/330,1800/330,0/1000" },
		/* STUTTER not specified */
		{ ZT_TONE_STUTTER, "!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,!425/100,!0/100,425" } },
	},
	{ 18, "sg", "Singapore", { 400, 200, 400, 2000 },
	{
		/* Reference: http://www.ida.gov.sg/idaweb/doc/download/I397/ida_ts_pstn1_i4r2.pdf */
		{ ZT_TONE_DIALTONE,   "425" },
		{ ZT_TONE_BUSY,       "425/750,0/750" },
		{ ZT_TONE_RINGTONE,   "425*24/400,0/200,425*24/400,0/2000" },
		{ ZT_TONE_CONGESTION, "425/250,0/250" },
		{ ZT_TONE_CALLWAIT,   "425*24/300,0/200,425*24/300,0/3200" },
		{ ZT_TONE_STUTTER,    "!425/200,!0/200,!425/600,!0/200,!425/200,!0/200,!425/600,!0/200,!425/200,!0/200,!425/600,!0/200,!425/200,!0/200,!425/600,!0/200,425" },
		{ ZT_TONE_INFO,       "950/330,1400/330,1800/330,0/1000" },	/* Special Information Tone (not in use) */
		{ ZT_TONE_DIALRECALL, "425*24/500,0/500,425/500,0/2500" },	/* unspecified in IDA reference, use repeating Holding Tone A,B */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" } },			/* unspecified in IDA reference, use 0.5s tone every 15s */
	}, 	
	{ 19, "il", "Israel", { 1000, 3000 },
	{
		{ ZT_TONE_DIALTONE, "414" },
		{ ZT_TONE_BUSY, "414/500,0/500" },
		{ ZT_TONE_RINGTONE, "414/1000,0/3000" },
		{ ZT_TONE_CONGESTION, "414/250,0/250" },
		{ ZT_TONE_CALLWAIT, "414/100,0/100,414/100,0/100,414/600,0/3000" },
		{ ZT_TONE_DIALRECALL, "!414/100,!0/100,!414/100,!0/100,!414/100,!0/100,414" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "1000/330,1400/330,1800/330,0/1000" },
		{ ZT_TONE_STUTTER, "!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,!414/160,!0/160,414" } },
	},
	{ 20, "br", "Brazil", { 1000, 4000 },
	{
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/250,0/250" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/250,0/250,425/750,0/250" },
		{ ZT_TONE_CALLWAIT, "425/50,0/1000" },
		{ ZT_TONE_DIALRECALL, "350+440" },
		{ ZT_TONE_RECORDTONE, "425/250,0/250" },
		{ ZT_TONE_INFO, "950/330,1400/330,1800/330" },
		{ ZT_TONE_STUTTER, "350+440" } },
	},
	{ -1 }		
};
