////////////////////////////////////////////////////////////////////////////////
//
// Filename: 	fftgen.cpp
//
// Project:	A General Purpose Pipelined FFT Implementation
//
// Purpose:	This is the core generator for the project.  Every part
//		and piece of this project begins and ends in this program.
//	Once built, this program will build an FFT (or IFFT) core of arbitrary
//	width, precision, etc., that will run at two samples per clock.
//	(Incidentally, I didn't pick two samples per clock because it was
//	easier, but rather because there weren't any two-sample per clock
//	FFT's posted on opencores.com.  Further, FFT's running at one sample
//	per aren't that hard to find.)
//
//	You can find the documentation for this program in two places.  One is
//	in the usage() function below.  The second is in the 'doc'uments
//	directory that comes with this package, specifically in the spec.pdf
//	file.  If it's not there, type make in the documents directory to
//	build it.
//
//	20160123 - Thanks to Lesha Birukov, adjusted for MS Visual Studio 2012.
//		(Adjustments are at the top of the file ...)
//
// Creator:	Dan Gisselquist, Ph.D.
//		Gisselquist Technology, LLC
//
////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2015-2018, Gisselquist Technology, LLC
//
// This program is free software (firmware): you can redistribute it and/or
// modify it under the terms of  the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at
// your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTIBILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  (It's in the $(ROOT)/doc directory, run make with no
// target there if the PDF file isn't present.)  If not, see
// <http://www.gnu.org/licenses/> for a copy.
//
// License:	GPL, v3, as defined and found on www.gnu.org,
//		http://www.gnu.org/licenses/gpl.html
//
//
////////////////////////////////////////////////////////////////////////////////
//
//
#define _CRT_SECURE_NO_WARNINGS   //  ms vs 2012 doesn't like fopen
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER //  added for ms vs compatibility

#include <io.h>
#include <direct.h>
#define _USE_MATH_DEFINES
#define	R_OK    4       /* Test for read permission.  */
#define	W_OK    2       /* Test for write permission.  */
#define	X_OK    0       /* !!!!!! execute permission - unsupported in windows*/
#define	F_OK    0       /* Test for existence.  */

#if _MSC_VER <= 1700

long long llround(double d) {
	if (d<0) return -(long long)(-d+0.5);
	else	return (long long)(d+0.5); }
int lstat(const char *filename, struct stat *buf) { return 1; };
#define	S_ISDIR(A)	0

#else

#define	lstat	_stat
#define S_ISDIR	_S_IFDIR

#endif

#define	mkdir(A,B)	_mkdir(A)

#define access _access

#else
// And for G++/Linux environment

#include <unistd.h>	// Defines the R_OK/W_OK/etc. macros
#include <sys/stat.h>
#endif

#include <string.h>
#include <string>
#include <math.h>
#include <ctype.h>
#include <assert.h>

#define	DEF_NBITSIN	16
#define	DEF_COREDIR	"fft-core"
#define	DEF_XTRACBITS	4
#define	DEF_NMPY	0
#define	DEF_XTRAPBITS	0
#define	USE_OLD_MULTIPLY	false
#define	SLASHLINE "////////////////////////////////////////////////////////////////////////////////\n"

// To coordinate testing, it helps to have some defines in our header file that
// are common with the default parameters found within the various subroutines.
// We'll define those common parameters here.  These values, however, have no
// effect on anything other than bench testing.  They do, though, allow us to
// bench test exact copies of what is going on within the FFT when necessary
// in order to find problems.
// First, parameters for the new multiply based upon the bi-multiply structure
// (2-bits/2-tableau rows at a time).
#define	TST_LONGBIMPY_AW	16
#define	TST_LONGBIMPY_BW	20	// Leave undefined to match AW

//  We also include parameters for the shift add multiply
#define	TST_SHIFTADDMPY_AW	16
#define	TST_SHIFTADDMPY_BW	20	// Leave undefined to match AW

// Now for parameters matching the butterfly
#define	TST_BUTTERFLY_IWIDTH	16
#define	TST_BUTTERFLY_CWIDTH	20
#define	TST_BUTTERFLY_OWIDTH	17

// Now for parameters matching the qtrstage
#define	TST_QTRSTAGE_IWIDTH	16
#define	TST_QTRSTAGE_LGWIDTH	8

// Parameters for the dblstage
#define	TST_DBLSTAGE_IWIDTH	16
#define	TST_DBLSTAGE_SHIFT	0

// Now for parameters matching the dblreverse stage
#define	TST_DBLREVERSE_LGSIZE	5

typedef	enum {
	RND_TRUNCATE, RND_FROMZERO, RND_HALFUP, RND_CONVERGENT
} ROUND_T;

const char	cpyleft[] =
SLASHLINE
"//\n"
"// Copyright (C) 2015-2018, Gisselquist Technology, LLC\n"
"//\n"
"// This program is free software (firmware): you can redistribute it and/or\n"
"// modify it under the terms of  the GNU General Public License as published\n"
"// by the Free Software Foundation, either version 3 of the License, or (at\n"
"// your option) any later version.\n"
"//\n"
"// This program is distributed in the hope that it will be useful, but WITHOUT\n"
"// ANY WARRANTY; without even the implied warranty of MERCHANTIBILITY or\n"
"// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License\n"
"// for more details.\n"
"//\n"
"// You should have received a copy of the GNU General Public License along\n"
"// with this program.  (It's in the $(ROOT)/doc directory, run make with no\n"
"// target there if the PDF file isn\'t present.)  If not, see\n"
"// <http://www.gnu.org/licenses/> for a copy.\n"
"//\n"
"// License:	GPL, v3, as defined and found on www.gnu.org,\n"
"//		http://www.gnu.org/licenses/gpl.html\n"
"//\n"
"//\n"
SLASHLINE;
const char	prjname[] = "A General Purpose Pipelined FFT Implementation";
const char	creator[] =	"// Creator:	Dan Gisselquist, Ph.D.\n"
				"//		Gisselquist Technology, LLC\n";

int	lgval(int vl) {
	int	lg;

	for(lg=1; (1<<lg) < vl; lg++)
		;
	return lg;
}

int	nextlg(int vl) {
	int	r;

	for(r=1; r<vl; r<<=1)
		;
	return r;
}

int	bflydelay(int nbits, int xtra) {
	int	cbits = nbits + xtra;
	int	delay;

	if (USE_OLD_MULTIPLY) {
		if (nbits+1<cbits)
			delay = nbits+4;
		else
			delay = cbits+3;
	} else {
		int	na=nbits+2, nb=cbits+1;
		if (nb<na) {
			int tmp = nb;
			nb = na; na = tmp;
		} delay = ((na)/2+(na&1)+2);
	}
	return delay;
}

int	lgdelay(int nbits, int xtra) {
	// The butterfly code needs to compare a valid address, of this
	// many bits, with an address two greater.  This guarantees we
	// have enough bits for that comparison.  We'll also end up with
	// more storage space to look for these values, but without a
	// redesign that's just what we'll deal with.
	return lgval(bflydelay(nbits, xtra)+3);
}

void	build_truncator(const char *fname) {
	printf("TRUNCATING!\n");
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\ttruncate.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:	Truncation is one of several options that can be used\n"
"//		internal to the various FFT stages to drop bits from one\n"
"//	stage to the next.  In general, it is the simplest method of dropping\n"
"//	bits, since it requires only a bit selection.\n"
"//\n"
"//	This form of rounding isn\'t really that great for FFT\'s, since it\n"
"//	tends to produce a DC bias in the result.  (Other less pronounced\n"
"//	biases may also exist.)\n"
"//\n"
"//	This particular version also registers the output with the clock, so\n"
"//	there will be a delay of one going through this module.  This will\n"
"//	keep it in line with the other forms of rounding that can be used.\n"
"//\n"
"//\n%s"
"//\n",
		prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	truncate(i_clk, i_ce, i_val, o_val);\n"
	"\tparameter\tIWID=16, OWID=8, SHIFT=0;\n"
	"\tinput\t\t\t\t\ti_clk, i_ce;\n"
	"\tinput\t\tsigned\t[(IWID-1):0]\ti_val;\n"
	"\toutput\treg\tsigned\t[(OWID-1):0]\to_val;\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\t\to_val <= i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
"\n"
"endmodule\n");
}


void	build_roundhalfup(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\troundhalfup.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tRounding half up is the way I was always taught to round in\n"
"//		school.  A one half value is added to the result, and then\n"
"//	the result is truncated.  When used in an FFT, this produces less\n"
"//	bias than the truncation method, although a bias still tends to\n"
"//	remain.\n"
"//\n"
"//\n%s"
"//\n",
		prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	roundhalfup(i_clk, i_ce, i_val, o_val);\n"
	"\tparameter\tIWID=16, OWID=8, SHIFT=0;\n"
	"\tinput\t\t\t\t\ti_clk, i_ce;\n"
	"\tinput\t\tsigned\t[(IWID-1):0]\ti_val;\n"
	"\toutput\treg\tsigned\t[(OWID-1):0]\to_val;\n"
"\n"
	"\t// Let's deal with two cases to be as general as we can be here\n"
	"\t//\n"
	"\t//	1. The desired output would lose no bits at all\n"
	"\t//	2. One or more bits would be dropped, so the rounding is simply\n"
	"\t//\t\ta matter of adding one to the bit about to be dropped,\n"
	"\t//\t\tmoving all halfway and above numbers up to the next\n"
	"\t//\t\tvalue.\n"
	"\tgenerate\n"
	"\tif (IWID-SHIFT == OWID)\n"
	"\tbegin // No truncation or rounding, output drops no bits\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\to_val <= i_val[(IWID-SHIFT-1):0];\n"
"\n"
	"\tend else // if (IWID-SHIFT-1 >= OWID)\n"
	"\tbegin // Output drops one bit, can only add one or ... not.\n"
		"\t\twire\t[(OWID-1):0]	truncated_value, rounded_up;\n"
		"\t\twire\t\t\tlast_valid_bit, first_lost_bit;\n"
		"\t\tassign\ttruncated_value=i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
		"\t\tassign\trounded_up=truncated_value + {{(OWID-1){1\'b0}}, 1\'b1 };\n"
		"\t\tassign\tfirst_lost_bit = i_val[(IWID-SHIFT-OWID-1)];\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\t\tif (i_ce)\n"
		"\t\t\tbegin\n"
			"\t\t\t\tif (!first_lost_bit) // Round down / truncate\n"
			"\t\t\t\t\to_val <= truncated_value;\n"
			"\t\t\t\telse\n"
			"\t\t\t\t\to_val <= rounded_up; // even value\n"
		"\t\t\tend\n"
"\n"
	"\tend\n"
	"\tendgenerate\n"
"\n"
"endmodule\n");
}

void	build_roundfromzero(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\troundfromzero.v\n"
"//\n"
"// Project:	%s\n"
"//\n"
"// Purpose:	Truncation is one of several options that can be used\n"
"//		internal to the various FFT stages to drop bits from one\n"
"//	stage to the next.  In general, it is the simplest method of dropping\n"
"//	bits, since it requires only a bit selection.\n"
"//\n"
"//	This form of rounding isn\'t really that great for FFT\'s, since it\n"
"//	tends to produce a DC bias in the result.  (Other less pronounced\n"
"//	biases may also exist.)\n"
"//\n"
"//	This particular version also registers the output with the clock, so\n"
"//	clock, so there will be a delay of one going through this module.\n"
"//	This will keep it in line with the other forms of rounding that can\n"
"//	be used.\n"
"//\n"
"//\n%s"
"//\n",
		prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	roundfromzero(i_clk, i_ce, i_val, o_val);\n"
	"\tparameter\tIWID=16, OWID=8, SHIFT=0;\n"
	"\tinput\t\t\t\t\ti_clk, i_ce;\n"
	"\tinput\t\tsigned\t[(IWID-1):0]\ti_val;\n"
	"\toutput\treg\tsigned\t[(OWID-1):0]\to_val;\n"
"\n"
	"\t// Let's deal with three cases to be as general as we can be here\n"
	"\t//\n"
	"\t//\t1. The desired output would lose no bits at all\n"
	"\t//\t2. One bit would be dropped, so the rounding is simply\n"
	"\t//\t\tadjusting the value to be the closer to zero in\n"
	"\t//\t\tcases of being halfway between two.  If identically\n"
	"\t//\t\tequal to a number, we just leave it as is.\n"
	"\t//\t3. Two or more bits would be dropped.  In this case, we round\n"
	"\t//\t\tnormally unless we are rounding a value of exactly\n"
	"\t//\t\thalfway between the two.  In the halfway case, we\n"
	"\t//\t\tround away from zero.\n"
	"\tgenerate\n"
	"\tif (IWID == OWID) // In this case, the shift is irrelevant and\n"
	"\tbegin // cannot be applied.  No truncation or rounding takes\n"
	"\t// effect here.\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\to_val <= i_val[(IWID-1):0];\n"
"\n"
	"\tend else if (IWID-SHIFT == OWID)\n"
	"\tbegin // No truncation or rounding, output drops no bits\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\to_val <= i_val[(IWID-SHIFT-1):0];\n"
"\n"
	"\tend else if (IWID-SHIFT-1 == OWID)\n"
	"\tbegin // Output drops one bit, can only add one or ... not.\n"
	"\t\twire\t[(OWID-1):0]\ttruncated_value, rounded_up;\n"
	"\t\twire\t\t\tsign_bit, first_lost_bit;\n"
	"\t\tassign\ttruncated_value=i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
	"\t\tassign\trounded_up=truncated_value + {{(OWID-1){1\'b0}}, 1\'b1 };\n"
	"\t\tassign\tfirst_lost_bit = i_val[0];\n"
	"\t\tassign\tsign_bit = i_val[(IWID-1)];\n"
"\n"
	"\t\talways @(posedge i_clk)\n"
		"\t\t\tif (i_ce)\n"
		"\t\t\tbegin\n"
			"\t\t\t\tif (!first_lost_bit) // Round down / truncate\n"
				"\t\t\t\t\to_val <= truncated_value;\n"
			"\t\t\t\telse if (sign_bit)\n"
				"\t\t\t\t\to_val <= truncated_value;\n"
			"\t\t\t\telse\n"
				"\t\t\t\t\to_val <= rounded_up;\n"
		"\t\t\tend\n"
"\n"
	"\tend else // If there's more than one bit we are dropping\n"
	"\tbegin\n"
		"\t\twire\t[(OWID-1):0]\ttruncated_value, rounded_up;\n"
		"\t\twire\t\t\tsign_bit, first_lost_bit;\n"
		"\t\tassign\ttruncated_value=i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
		"\t\tassign\trounded_up=truncated_value + {{(OWID-1){1\'b0}}, 1\'b1 };\n"
		"\t\tassign\tfirst_lost_bit = i_val[(IWID-SHIFT-OWID-1)];\n"
		"\t\tassign\tsign_bit = i_val[(IWID-1)];\n"
"\n"
		"\t\twire\t[(IWID-SHIFT-OWID-2):0]\tother_lost_bits;\n"
		"\t\tassign\tother_lost_bits = i_val[(IWID-SHIFT-OWID-2):0];\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\n"
			"\t\t\tbegin\n"
			"\t\t\t\tif (!first_lost_bit) // Round down / truncate\n"
				"\t\t\t\t\to_val <= truncated_value;\n"
			"\t\t\t\telse if (|other_lost_bits) // Round up to\n"
				"\t\t\t\t\to_val <= rounded_up; // closest value\n"
			"\t\t\t\telse if (sign_bit)\n"
				"\t\t\t\t\to_val <= truncated_value;\n"
			"\t\t\t\telse\n"
				"\t\t\t\t\to_val <= rounded_up;\n"
			"\t\t\tend\n"
	"\tend\n"
	"\tendgenerate\n"
"\n"
"endmodule\n");
}

void	build_convround(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename: 	convround.v\n"
"//\n"
"// Project:	%s\n"
"//\n"
"// Purpose:	A convergent rounding routine, also known as banker\'s\n"
"//		rounding, Dutch rounding, Gaussian rounding, unbiased\n"
"//	rounding, or ... more, at least according to Wikipedia.\n"
"//\n"
"//	This form of rounding works by rounding, when the direction is in\n"
"//	question, towards the nearest even value.\n"
"//\n"
"//\n%s"
"//\n",
		prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	convround(i_clk, i_ce, i_val, o_val);\n"
"\tparameter\tIWID=16, OWID=8, SHIFT=0;\n"
"\tinput\t\t\t\t\ti_clk, i_ce;\n"
"\tinput\t\tsigned\t[(IWID-1):0]\ti_val;\n"
"\toutput\treg\tsigned\t[(OWID-1):0]\to_val;\n"
"\n"
"\t// Let's deal with three cases to be as general as we can be here\n"
"\t//\n"
"\t//\t1. The desired output would lose no bits at all\n"
"\t//\t2. One bit would be dropped, so the rounding is simply\n"
"\t//\t\tadjusting the value to be the nearest even number in\n"
"\t//\t\tcases of being halfway between two.  If identically\n"
"\t//\t\tequal to a number, we just leave it as is.\n"
"\t//\t3. Two or more bits would be dropped.  In this case, we round\n"
"\t//\t\tnormally unless we are rounding a value of exactly\n"
"\t//\t\thalfway between the two.  In the halfway case we round\n"
"\t//\t\tto the nearest even number.\n"
"\tgenerate\n"
// What if IWID < OWID?  We should expand here ... somehow
	"\tif (IWID == OWID) // In this case, the shift is irrelevant and\n"
	"\tbegin // cannot be applied.  No truncation or rounding takes\n"
	"\t// effect here.\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\to_val <= i_val[(IWID-1):0];\n"
"\n"
// What if IWID-SHIFT < OWID?  Shouldn't we also shift here as well?
"\tend else if (IWID-SHIFT == OWID)\n"
"\tbegin // No truncation or rounding, output drops no bits\n"
"\n"
"\t\talways @(posedge i_clk)\n"
"\t\t\tif (i_ce)\to_val <= i_val[(IWID-SHIFT-1):0];\n"
"\n"
"\tend else if (IWID-SHIFT-1 == OWID)\n"
// Is there any way to limit the number of bits that are examined here, for the
// purpose of simplifying/reducing logic?  I mean, if we go from 32 to 16 bits,
// must we check all 15 bits for equality to zero?
"\tbegin // Output drops one bit, can only add one or ... not.\n"
"\t\twire\t[(OWID-1):0]	truncated_value, rounded_up;\n"
"\t\twire\t\t\tlast_valid_bit, first_lost_bit;\n"
"\t\tassign\ttruncated_value=i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
"\t\tassign\trounded_up=truncated_value + {{(OWID-1){1\'b0}}, 1\'b1 };\n"
"\t\tassign\tlast_valid_bit = truncated_value[0];\n"
"\t\tassign\tfirst_lost_bit = i_val[0];\n"
"\n"
"\t\talways @(posedge i_clk)\n"
"\t\t\tif (i_ce)\n"
"\t\t\tbegin\n"
"\t\t\t\tif (!first_lost_bit) // Round down / truncate\n"
"\t\t\t\t\to_val <= truncated_value;\n"
"\t\t\t\telse if (last_valid_bit)// Round up to nearest\n"
"\t\t\t\t\to_val <= rounded_up; // even value\n"
"\t\t\t\telse // else round down to the nearest\n"
"\t\t\t\t\to_val <= truncated_value; // even value\n"
"\t\t\tend\n"
"\n"
"\tend else // If there's more than one bit we are dropping\n"
"\tbegin\n"
"\t\twire\t[(OWID-1):0]	truncated_value, rounded_up;\n"
"\t\twire\t\t\tlast_valid_bit, first_lost_bit;\n"
"\t\tassign\ttruncated_value=i_val[(IWID-1-SHIFT):(IWID-SHIFT-OWID)];\n"
"\t\tassign\trounded_up=truncated_value + {{(OWID-1){1\'b0}}, 1\'b1 };\n"
"\t\tassign\tlast_valid_bit = truncated_value[0];\n"
"\t\tassign\tfirst_lost_bit = i_val[(IWID-SHIFT-OWID-1)];\n"
"\n"
"\t\twire\t[(IWID-SHIFT-OWID-2):0]\tother_lost_bits;\n"
"\t\tassign\tother_lost_bits = i_val[(IWID-SHIFT-OWID-2):0];\n"
"\n"
"\t\talways @(posedge i_clk)\n"
"\t\t\tif (i_ce)\n"
"\t\t\tbegin\n"
"\t\t\t\tif (!first_lost_bit) // Round down / truncate\n"
"\t\t\t\t\to_val <= truncated_value;\n"
"\t\t\t\telse if (|other_lost_bits) // Round up to\n"
"\t\t\t\t\to_val <= rounded_up; // closest value\n"
"\t\t\t\telse if (last_valid_bit) // Round up to\n"
"\t\t\t\t\to_val <= rounded_up; // nearest even\n"
"\t\t\t\telse	// else round down to nearest even\n"
"\t\t\t\t\to_val <= truncated_value;\n"
"\t\t\tend\n"
"\tend\n"
"\tendgenerate\n"
"\n"
"endmodule\n");
}

void	build_quarters(const char *fname, ROUND_T rounding, const bool async_reset=false, const bool dbg=false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}
	const	char	*rnd_string;
	if (rounding == RND_TRUNCATE)
		rnd_string = "truncate";
	else if (rounding == RND_FROMZERO)
		rnd_string = "roundfromzero";
	else if (rounding == RND_HALFUP)
		rnd_string = "roundhalfup";
	else
		rnd_string = "convround";


	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tqtrstage%s.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:	This file encapsulates the 4 point stage of a decimation in\n"
"//		frequency FFT.  This particular implementation is optimized\n"
"//	so that all of the multiplies are accomplished by additions and\n"
"//	multiplexers only.\n"
"//\n"
"//\n%s"
"//\n",
		(dbg)?"_dbg":"", prjname, creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");

	fprintf(fp,
"module\tqtrstage%s(i_clk, %s, i_ce, i_sync, i_data, o_data, o_sync%s);\n"
	"\tparameter	IWIDTH=%d, OWIDTH=IWIDTH+1;\n"
	"\t// Parameters specific to the core that should be changed when this\n"
	"\t// core is built ... Note that the minimum LGSPAN is 2.  Smaller\n"
	"\t// spans must use the fftdoubles stage.\n"
	"\tparameter\tLGWIDTH=%d, ODD=0, INVERSE=0,SHIFT=0;\n"
	"\tinput\t				i_clk, %s, i_ce, i_sync;\n"
	"\tinput\t	[(2*IWIDTH-1):0]	i_data;\n"
	"\toutput\treg	[(2*OWIDTH-1):0]	o_data;\n"
	"\toutput\treg				o_sync;\n"
	"\t\n", (dbg)?"_dbg":"",
	resetw.c_str(),
	(dbg)?", o_dbg":"", TST_QTRSTAGE_IWIDTH,
	TST_QTRSTAGE_LGWIDTH, resetw.c_str());
	if (dbg) { fprintf(fp, "\toutput\twire\t[33:0]\t\t\to_dbg;\n"
		"\tassign\to_dbg = { ((o_sync)&&(i_ce)), i_ce, o_data[(2*OWIDTH-1):(2*OWIDTH-16)],\n"
			"\t\t\t\t\to_data[(OWIDTH-1):(OWIDTH-16)] };\n"
"\n");
	}
	fprintf(fp,
	"\treg\t	wait_for_sync;\n"
	"\treg\t[3:0]	pipeline;\n"
"\n"
	"\treg\t[(IWIDTH):0]	sum_r, sum_i, diff_r, diff_i;\n"
"\n"
	"\treg\t[(2*OWIDTH-1):0]\tob_a;\n"
	"\twire\t[(2*OWIDTH-1):0]\tob_b;\n"
	"\treg\t[(OWIDTH-1):0]\t\tob_b_r, ob_b_i;\n"
	"\tassign\tob_b = { ob_b_r, ob_b_i };\n"
"\n"
	"\treg\t[(LGWIDTH-1):0]\t\tiaddr;\n"
	"\treg\t[(2*IWIDTH-1):0]\timem;\n"
"\n"
	"\twire\tsigned\t[(IWIDTH-1):0]\timem_r, imem_i;\n"
	"\tassign\timem_r = imem[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\timem_i = imem[(IWIDTH-1):0];\n"
"\n"
	"\twire\tsigned\t[(IWIDTH-1):0]\ti_data_r, i_data_i;\n"
	"\tassign\ti_data_r = i_data[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\ti_data_i = i_data[(IWIDTH-1):0];\n"
"\n"
	"\treg	[(2*OWIDTH-1):0]	omem;\n"
"\n");
	fprintf(fp,
	"\twire\tsigned\t[(OWIDTH-1):0]\trnd_sum_r, rnd_sum_i, rnd_diff_r, rnd_diff_i,\n");
	fprintf(fp,
	"\t\t\t\t\tn_rnd_diff_r, n_rnd_diff_i;\n");
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT)\tdo_rnd_sum_r(i_clk, i_ce,\n"
	"\t\t\t\tsum_r, rnd_sum_r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT)\tdo_rnd_sum_i(i_clk, i_ce,\n"
	"\t\t\t\tsum_i, rnd_sum_i);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT)\tdo_rnd_diff_r(i_clk, i_ce,\n"
	"\t\t\t\tdiff_r, rnd_diff_r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT)\tdo_rnd_diff_i(i_clk, i_ce,\n"
	"\t\t\t\tdiff_i, rnd_diff_i);\n\n", rnd_string);
	fprintf(fp, "\tassign n_rnd_diff_r = - rnd_diff_r;\n"
		"\tassign n_rnd_diff_i = - rnd_diff_i;\n");
/*
	fprintf(fp,
	"\twire	[(IWIDTH-1):0]	rnd;\n"
	"\tgenerate\n"
	"\tif ((ROUND)&&((IWIDTH+1-OWIDTH-SHIFT)>0))\n"
		"\t\tassign rnd = { {(IWIDTH-1){1\'b0}}, 1\'b1 };\n"
	"\telse\n"
		"\t\tassign rnd = { {(IWIDTH){1\'b0}}};\n"
	"\tendgenerate\n"
"\n"
*/
	fprintf(fp,
	"\tinitial wait_for_sync = 1\'b1;\n"
	"\tinitial iaddr = 0;\n");
	if (async_reset)
		fprintf(fp,
			"\talways @(posedge i_clk, negedge i_areset_n)\n"
				"\t\tif (!i_reset)\n");
	else
		fprintf(fp,
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\tbegin\n"
			"\t\t\twait_for_sync <= 1\'b1;\n"
			"\t\t\tiaddr <= 0;\n"
		"\t\tend else if ((i_ce)&&((!wait_for_sync)||(i_sync)))\n"
		"\t\tbegin\n"
			"\t\t\tiaddr <= iaddr + { {(LGWIDTH-1){1\'b0}}, 1\'b1 };\n"
			"\t\t\twait_for_sync <= 1\'b0;\n"
		"\t\tend\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\timem <= i_data;\n"
		"\n\n");
	fprintf(fp,
	"\t// Note that we don\'t check on wait_for_sync or i_sync here.\n"
	"\t// Why not?  Because iaddr will always be zero until after the\n"
	"\t// first i_ce, so we are safe.\n"
	"\tinitial pipeline = 4\'h0;\n");
	if (async_reset)
		fprintf(fp,
	"\talways\t@(posedge i_clk, negedge i_areset_n)\n"
		"\t\tif (!i_reset)\n");
	else
		fprintf(fp,
	"\talways\t@(posedge i_clk)\n"
		"\t\tif (i_reset)\n");

	fprintf(fp,
			"\t\t\tpipeline <= 4\'h0;\n"
		"\t\telse if (i_ce) // is our pipeline process full?  Which stages?\n"
			"\t\t\tpipeline <= { pipeline[2:0], iaddr[0] };\n\n");
	fprintf(fp,
	"\t// This is the pipeline[-1] stage, pipeline[0] will be set next.\n"
	"\talways\t@(posedge i_clk)\n"
		"\t\tif ((i_ce)&&(iaddr[0]))\n"
		"\t\tbegin\n"
			"\t\t\tsum_r  <= imem_r + i_data_r;\n"
			"\t\t\tsum_i  <= imem_i + i_data_i;\n"
			"\t\t\tdiff_r <= imem_r - i_data_r;\n"
			"\t\t\tdiff_i <= imem_i - i_data_i;\n"
		"\t\tend\n\n");
	fprintf(fp,
	"\t// pipeline[1] takes sum_x and diff_x and produces rnd_x\n\n");
	fprintf(fp,
	"\t// Now for pipeline[2].  We can actually do this at all i_ce\n"
	"\t// clock times, since nothing will listen unless pipeline[3]\n"
	"\t// on the next clock.  Thus, we simplify this logic and do\n"
	"\t// it independent of pipeline[2].\n"
	"\talways\t@(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tob_a <= { rnd_sum_r, rnd_sum_i };\n"
			"\t\t\t// on Even, W = e^{-j2pi 1/4 0} = 1\n"
			"\t\t\tif (ODD == 0)\n"
			"\t\t\tbegin\n"
			"\t\t\t\tob_b_r <= rnd_diff_r;\n"
			"\t\t\t\tob_b_i <= rnd_diff_i;\n"
			"\t\t\tend else if (INVERSE==0) begin\n"
			"\t\t\t\t// on Odd, W = e^{-j2pi 1/4} = -j\n"
			"\t\t\t\tob_b_r <=   rnd_diff_i;\n"
			"\t\t\t\tob_b_i <= n_rnd_diff_r;\n"
			"\t\t\tend else begin\n"
			"\t\t\t\t// on Odd, W = e^{j2pi 1/4} = j\n"
			"\t\t\t\tob_b_r <= n_rnd_diff_i;\n"
			"\t\t\t\tob_b_i <=   rnd_diff_r;\n"
			"\t\t\tend\n"
		"\t\tend\n\n");
	fprintf(fp,
	"\talways\t@(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin // In sequence, clock = 3\n"
			"\t\t\tif (pipeline[3])\n"
			"\t\t\tbegin\n"
				"\t\t\t\tomem <= ob_b;\n"
				"\t\t\t\to_data <= ob_a;\n"
			"\t\t\tend else\n"
				"\t\t\t\to_data <= omem;\n"
		"\t\tend\n\n");

	fprintf(fp,
	"\t// Don\'t forget in the sync check that we are running\n"
	"\t// at two clocks per sample.  Thus we need to\n"
	"\t// produce a sync every 2^(LGWIDTH-1) clocks.\n"
	"\tinitial\to_sync = 1\'b0;\n");

	if (async_reset)
		fprintf(fp,
	"\talways\t@(posedge i_clk, negedge i_areset_n)\n"
		"\t\tif (!i_areset_n)\n");
	else
		fprintf(fp,
	"\talways\t@(posedge i_clk)\n"
		"\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\t\to_sync <= 1\'b0;\n"
		"\t\telse if (i_ce)\n"
			"\t\t\to_sync <= &(~iaddr[(LGWIDTH-2):3]) && (iaddr[2:0] == 3'b101);\n");
	fprintf(fp, "endmodule\n");
}

void	build_dblstage(const char *fname, ROUND_T rounding, const bool async_reset = false, const bool dbg = false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	const	char	*rnd_string;
	if (rounding == RND_TRUNCATE)
		rnd_string = "truncate";
	else if (rounding == RND_FROMZERO)
		rnd_string = "roundfromzero";
	else if (rounding == RND_HALFUP)
		rnd_string = "roundhalfup";
	else
		rnd_string = "convround";

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");


	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tdblstage%s.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis is part of an FPGA implementation that will process\n"
"//		the final stage of a decimate-in-frequency FFT, running\n"
"//	through the data at two samples per clock.  If you notice from the\n"
"//	derivation of an FFT, the only time both even and odd samples are\n"
"//	used at the same time is in this stage.  Therefore, other than this\n"
"//	stage and these twiddles, all of the other stages can run two stages\n"
"//	at a time at one sample per clock.\n"
"//\n"
"//	In this implementation, the output is valid one clock after the input\n"
"//	is valid.  The output also accumulates one bit above and beyond the\n"
"//	number of bits in the input.\n"
"//\n"
"//		i_clk	A system clock\n", (dbg)?"_dbg":"", prjname);
	if (async_reset)
		fprintf(fp,
"//		i_areset_n	An active low asynchronous reset\n");
	else
		fprintf(fp,
"//		i_reset	A synchronous reset\n");

	fprintf(fp,
"//		i_ce	Circuit enable--nothing happens unless this line is high\n"
"//		i_sync	A synchronization signal, high once per FFT at the start\n"
"//		i_left	The first (even) complex sample input.  The higher order\n"
"//			bits contain the real portion, low order bits the\n"
"//			imaginary portion, all in two\'s complement.\n"
"//		i_right	The next (odd) complex sample input, same format as\n"
"//			i_left.\n"
"//		o_left	The first (even) complex output.\n"
"//		o_right	The next (odd) complex output.\n"
"//		o_sync	Output synchronization signal.\n"
"//\n%s"
"//\n", creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module\tdblstage%s(i_clk, %s, i_ce, i_sync, i_left, i_right, o_left, o_right, o_sync%s);\n"
	"\tparameter\tIWIDTH=%d,OWIDTH=IWIDTH+1, SHIFT=%d;\n"
	"\tinput\t\ti_clk, %s, i_ce, i_sync;\n"
	"\tinput\t\t[(2*IWIDTH-1):0]\ti_left, i_right;\n"
	"\toutput\treg\t[(2*OWIDTH-1):0]\to_left, o_right;\n"
	"\toutput\treg\t\t\to_sync;\n"
	"\n", (dbg)?"_dbg":"", resetw.c_str(), (dbg)?", o_dbg":"",
	TST_DBLSTAGE_IWIDTH, TST_DBLSTAGE_SHIFT,
		resetw.c_str());

	if (dbg) { fprintf(fp, "\toutput\twire\t[33:0]\t\t\to_dbg;\n"
		"\tassign\to_dbg = { ((o_sync)&&(i_ce)), i_ce, o_left[(2*OWIDTH-1):(2*OWIDTH-16)],\n"
			"\t\t\t\t\to_left[(OWIDTH-1):(OWIDTH-16)] };\n"
"\n");
	}
	fprintf(fp,
	"\twire\tsigned\t[(IWIDTH-1):0]\ti_in_0r, i_in_0i, i_in_1r, i_in_1i;\n"
	"\tassign\ti_in_0r = i_left[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\ti_in_0i = i_left[(IWIDTH-1):0];\n"
	"\tassign\ti_in_1r = i_right[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\ti_in_1i = i_right[(IWIDTH-1):0];\n"
	"\twire\t[(OWIDTH-1):0]\t\to_out_0r, o_out_0i,\n"
				"\t\t\t\t\to_out_1r, o_out_1i;\n"
"\n"
"\n"
	"\t// Handle a potential rounding situation, when IWIDTH>=OWIDTH.\n"
"\n"
"\n");
	fprintf(fp,
	"\n"
	"\t// As with any register connected to the sync pulse, these must\n"
	"\t// have initial values and be reset on the %s signal.\n"
	"\t// Other data values need only restrict their updates to i_ce\n"
	"\t// enabled clocks, but sync\'s must obey resets and initial\n"
	"\t// conditions as well.\n"
	"\treg\trnd_sync, r_sync;\n"
"\n"
	"\tinitial\trnd_sync      = 1\'b0; // Sync into rounding\n"
	"\tinitial\tr_sync        = 1\'b0; // Sync coming out\n",
		resetw.c_str());
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negdge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\tbegin\n"
			"\t\t\trnd_sync <= 1\'b0;\n"
			"\t\t\tr_sync <= 1\'b0;\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\trnd_sync <= i_sync;\n"
			"\t\t\tr_sync <= rnd_sync;\n"
		"\t\tend\n"
"\n"
	"\t// As with other variables, these are really only updated when in\n"
	"\t// the processing pipeline, after the first i_sync.  However, to\n"
	"\t// eliminate as much unnecessary logic as possible, we toggle\n"
	"\t// these any time the i_ce line is enabled, and don\'t reset.\n"
	"\t// them on %s.\n", resetw.c_str());
	fprintf(fp,
	"\t// Don't forget that we accumulate a bit by adding two values\n"
	"\t// together. Therefore our intermediate value must have one more\n"
	"\t// bit than the two originals.\n"
	"\treg\tsigned\t[(IWIDTH):0]\trnd_in_0r, rnd_in_0i;\n"
	"\treg\tsigned\t[(IWIDTH):0]\trnd_in_1r, rnd_in_1i;\n\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t//\n"
			"\t\t\trnd_in_0r <= i_in_0r + i_in_1r;\n"
			"\t\t\trnd_in_0i <= i_in_0i + i_in_1i;\n"
			"\t\t\t//\n"
			"\t\t\trnd_in_1r <= i_in_0r - i_in_1r;\n"
			"\t\t\trnd_in_1i <= i_in_0i - i_in_1i;\n"
			"\t\t\t//\n"
		"\t\tend\n"
"\n");
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_0r(i_clk, i_ce,\n"
	"\t\t\t\t\t\t\trnd_in_0r, o_out_0r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_0i(i_clk, i_ce,\n"
	"\t\t\t\t\t\t\trnd_in_0i, o_out_0i);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_1r(i_clk, i_ce,\n"
	"\t\t\t\t\t\t\trnd_in_1r, o_out_1r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_1i(i_clk, i_ce,\n"
	"\t\t\t\t\t\t\trnd_in_1i, o_out_1i);\n\n", rnd_string);

	fprintf(fp, "\n"
	"\t// Prior versions of this routine did not include the extra\n"
	"\t// clock and register/flip-flops that this routine requires.\n"
	"\t// These are placed in here to correct a bug in Verilator, that\n"
	"\t// otherwise struggles.  (Hopefully this will fix the problem ...)\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\to_left  <= { o_out_0r, o_out_0i };\n"
			"\t\t\to_right <= { o_out_1r, o_out_1i };\n"
		"\t\tend\n"
"\n"
	"\tinitial\to_sync = 1\'b0; // Final sync coming out of module\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negdge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\t\to_sync <= 1\'b0;\n"
		"\t\telse if (i_ce)\n"
		"\t\t\to_sync <= r_sync;\n"
"\n"
"endmodule\n");
	fclose(fp);
}

void	build_sngllast(const char *fname, const bool async_reset = false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tsngllast.v\n"
"//\n"
"// Project:	%s\n"
"//\n"
"// Purpose:	This is part of an FPGA implementation that will process\n"
"//		the final stage of a decimate-in-frequency FFT, running\n"
"//	through the data at one sample per clock.\n"
"//\n"
"//\n%s"
"//\n", prjname, creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");

	fprintf(fp,
"module	sngllast(i_clk, %s, i_ce, i_sync, i_val, o_val, o_sync);\n"
"	parameter	IWIDTH=16,OWIDTH=IWIDTH+1, SHIFT=0;\n"
"	input					i_clk, %s, i_ce, i_sync;\n"
"	input		[(2*IWIDTH-1):0]	i_val;\n"
"	output	wire	[(2*OWIDTH-1):0]	o_val;\n"
"	output	reg				o_sync;\n\n",
		resetw.c_str(), resetw.c_str());

	fprintf(fp,
"	reg	signed	[(IWIDTH-1):0]	m_r, m_i;\n"
"	wire	signed	[(IWIDTH-1):0]	i_r, i_i;\n"
"\n"
"	assign	i_r = i_val[(2*IWIDTH-1):(IWIDTH)]; \n"
"	assign	i_i = i_val[(IWIDTH-1):0]; \n"
"\n"
"	// Don't forget that we accumulate a bit by adding two values\n"
"	// together. Therefore our intermediate value must have one more\n"
"	// bit than the two originals.\n"
"	reg	signed	[(IWIDTH):0]	rnd_r, rnd_i, sto_r, sto_i;\n"
"	reg				wait_for_sync, rnd_sync, stage, pre_sync;\n"
"\n"
"	initial	rnd_sync      = 1'b0;\n"
"	initial	o_sync        = 1'b0;\n"
"	initial	wait_for_sync = 1'b1;\n"
"	initial	stage         = 1'b0;\n");

	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
"		begin\n"
"			rnd_sync      <= 1'b0;\n"
"			o_sync        <= 1'b0;\n"
"			wait_for_sync <= 1'b1;\n"
"			stage         <= 1'b0;\n"
"		end else if ((i_ce)&&((!wait_for_sync)||(i_sync))&&(!stage))\n"
"		begin\n"
"			wait_for_sync <= 1'b0;\n"
"			//\n"
"			stage <= 1'b1;\n"
"			//\n"
"			o_sync <= rnd_sync;\n"
"		end else if (i_ce)\n"
"		begin\n"
"			rnd_sync <= pre_sync;\n"
"			//\n"
"			stage <= 1'b0;\n"
"		end\n");

	if (async_reset)
		fprintf(fp,
		"\talways @(posedge i_clk)\n"
		"\t\tpre_sync <= (i_areset_n)&&(i_ce)&&(i_sync);\n");
	else
		fprintf(fp,
		"\talways @(posedge i_clk)\n"
		"\t\tpre_sync <= (!i_reset)&&(i_ce)&&(i_sync);\n");

	fprintf(fp, "\n\n"
"	always @(posedge i_clk)\n"
"	if (i_ce)\n"
"	begin\n"
"		if (!stage)\n"
"		begin\n"
"			// Clock 1\n"
"			m_r <= i_r;\n"
"			m_i <= i_i;\n"
"			// Clock 3\n"
"			rnd_r <= sto_r;\n"
"			rnd_i <= sto_i;\n"
"			//\n"
"		end else begin\n"
"			// Clock 2\n"
"			rnd_r <= m_r + i_r;\n"
"			rnd_i <= m_i + i_i;\n"
"			//\n"
"			sto_r <= m_r - i_r;\n"
"			sto_i <= m_i - i_i;\n"
"			//\n"
"		end\n"
"	end\n"
"\n"
"	// Now that we have our results, let's round them and report them\n"
"	wire	signed	[(OWIDTH-1):0]	o_r, o_i;\n"
"\n"
"	convround #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_r(i_clk, i_ce, rnd_r, o_r);\n"
"	convround #(IWIDTH+1,OWIDTH,SHIFT) do_rnd_i(i_clk, i_ce, rnd_i, o_i);\n"
"\n"
"	assign	o_val  = { o_r, o_i };\n"
"\n"
"endmodule\n");

	fclose(fp);
}

void	build_multiply(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tshiftaddmpy.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tA portable shift and add multiply.\n"
"//\n"
"//	While both Xilinx and Altera will offer single clock multiplies, this\n"
"//	simple approach will multiply two numbers on any architecture.  The\n"
"//	result maintains the full width of the multiply, there are no extra\n"
"//	stuff bits, no rounding, no shifted bits, etc.\n"
"//\n"
"//	Further, for those applications that can support it, this multiply\n"
"//	is pipelined and will produce one answer per clock.\n"
"//\n"
"//	For minimal processing delay, make the first parameter the one with\n"
"//	the least bits, so that AWIDTH <= BWIDTH.\n"
"//\n"
"//	The processing delay in this multiply is (AWIDTH+1) cycles.  That is,\n"
"//	if the data is present on the input at clock t=0, the result will be\n"
"//	present on the output at time t=AWIDTH+1;\n"
"//\n"
"//\n%s"
"//\n", prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	shiftaddmpy(i_clk, i_ce, i_a, i_b, o_r);\n"
	"\tparameter\tAWIDTH=%d,BWIDTH=", TST_SHIFTADDMPY_AW);
#ifdef	TST_SHIFTADDMPY_BW
	fprintf(fp, "%d;\n", TST_SHIFTADDMPY_BW);
#else
	fprintf(fp, "AWIDTH;\n");
#endif
	fprintf(fp,
	"\tinput\t\t\t\t\ti_clk, i_ce;\n"
	"\tinput\t\t[(AWIDTH-1):0]\t\ti_a;\n"
	"\tinput\t\t[(BWIDTH-1):0]\t\ti_b;\n"
	"\toutput\treg\t[(AWIDTH+BWIDTH-1):0]\to_r;\n"
"\n"
	"\treg\t[(AWIDTH-1):0]\tu_a;\n"
	"\treg\t[(BWIDTH-1):0]\tu_b;\n"
	"\treg\t\t\tsgn;\n"
"\n"
	"\treg\t[(AWIDTH-2):0]\t\tr_a[0:(AWIDTH-1)];\n"
	"\treg\t[(AWIDTH+BWIDTH-2):0]\tr_b[0:(AWIDTH-1)];\n"
	"\treg\t\t\t\tr_s[0:(AWIDTH-1)];\n"
	"\treg\t[(AWIDTH+BWIDTH-1):0]\tacc[0:(AWIDTH-1)];\n"
	"\tgenvar k;\n"
"\n"
	"\t// If we were forced to stay within two\'s complement arithmetic,\n"
	"\t// taking the absolute value here would require an additional bit.\n"
	"\t// However, because our results are now unsigned, we can stay\n"
	"\t// within the number of bits given (for now).\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tu_a <= (i_a[AWIDTH-1])?(-i_a):(i_a);\n"
			"\t\t\tu_b <= (i_b[BWIDTH-1])?(-i_b):(i_b);\n"
			"\t\t\tsgn <= i_a[AWIDTH-1] ^ i_b[BWIDTH-1];\n"
		"\t\tend\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tacc[0] <= (u_a[0]) ? { {(AWIDTH){1\'b0}}, u_b }\n"
			"\t\t\t\t\t: {(AWIDTH+BWIDTH){1\'b0}};\n"
			"\t\t\tr_a[0] <= { u_a[(AWIDTH-1):1] };\n"
			"\t\t\tr_b[0] <= { {(AWIDTH-1){1\'b0}}, u_b };\n"
			"\t\t\tr_s[0] <= sgn; // The final sign, needs to be preserved\n"
		"\t\tend\n"
"\n"
	"\tgenerate\n"
	"\tfor(k=0; k<AWIDTH-1; k=k+1)\n"
	"\tbegin : genstages\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tacc[k+1] <= acc[k] + ((r_a[k][0]) ? {r_b[k],1\'b0}:0);\n"
			"\t\t\tr_a[k+1] <= { 1\'b0, r_a[k][(AWIDTH-2):1] };\n"
			"\t\t\tr_b[k+1] <= { r_b[k][(AWIDTH+BWIDTH-3):0], 1\'b0};\n"
			"\t\t\tr_s[k+1] <= r_s[k];\n"
		"\t\tend\n"
	"\tend\n"
	"\tendgenerate\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\to_r <= (r_s[AWIDTH-1]) ? (-acc[AWIDTH-1]) : acc[AWIDTH-1];\n"
"\n"
"endmodule\n");

	fclose(fp);
}

void	build_bimpy(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\t%s\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tA simple 2-bit multiply based upon the fact that LUT's allow\n"
"//		6-bits of input.  In other words, I could build a 3-bit\n"
"//	multiply from 6 LUTs (5 actually, since the first could have two\n"
"//	outputs).  This would allow multiplication of three bit digits, save\n"
"//	only for the fact that you would need two bits of carry.  The bimpy\n"
"//	approach throttles back a bit and does a 2x2 bit multiply in a LUT,\n"
"//	guaranteeing that it will never carry more than one bit.  While this\n"
"//	multiply is hardware independent (and can still run under Verilator\n"
"//	therefore), it is really motivated by trying to optimize for a\n"
"//	specific piece of hardware (Xilinx-7 series ...) that has at least\n"
"//	4-input LUT's with carry chains.\n"
"//\n"
"//\n"
"//\n%s"
"//\n", fname, prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	bimpy(i_clk, i_ce, i_a, i_b, o_r);\n"
"\tparameter\tBW=18, // Number of bits in i_b\n"
"\t\t\tLUTB=2; // Number of bits in i_a for our LUT multiply\n"
"\tinput\t\t\t\ti_clk, i_ce;\n"
"\tinput\t\t[(LUTB-1):0]\ti_a;\n"
"\tinput\t\t[(BW-1):0]\ti_b;\n"
"\toutput\treg\t[(BW+LUTB-1):0]	o_r;\n"
"\n"
"\twire	[(BW+LUTB-2):0]	w_r;\n"
"\twire	[(BW+LUTB-3):1]	c;\n"
"\n"
"\tassign\tw_r =  { ((i_a[1])?i_b:{(BW){1\'b0}}), 1\'b0 }\n"
"\t\t\t\t^ { 1\'b0, ((i_a[0])?i_b:{(BW){1\'b0}}) };\n"
"\tassign\tc = { ((i_a[1])?i_b[(BW-2):0]:{(BW-1){1\'b0}}) }\n"
"\t\t\t& ((i_a[0])?i_b[(BW-1):1]:{(BW-1){1\'b0}});\n"
"\n"
"\talways @(posedge i_clk)\n"
"\t\tif (i_ce)\n"
"\t\t\to_r <= w_r + { c, 2'b0 };\n"
"\n"
"endmodule\n");

	fclose(fp);
}

void	build_longbimpy(const char *fname) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename: 	%s\n"
"//\n"
"// Project:	%s\n"
"//\n"
"// Purpose:	A portable shift and add multiply, built with the knowledge\n"
"//	of the existence of a six bit LUT and carry chain.  That knowledge\n"
"//	allows us to multiply two bits from one value at a time against all\n"
"//	of the bits of the other value.  This sub multiply is called the\n"
"//	bimpy.\n"
"//\n"
"//	For minimal processing delay, make the first parameter the one with\n"
"//	the least bits, so that AWIDTH <= BWIDTH.\n"
"//\n"
"//\n"
"//\n%s"
"//\n", fname, prjname, creator);

	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	longbimpy(i_clk, i_ce, i_a_unsorted, i_b_unsorted, o_r);\n"
	"\tparameter	IAW=%d,	// The width of i_a, min width is 5\n"
			"\t\t\tIBW=", TST_LONGBIMPY_AW);
#ifdef	TST_LONGBIMPY_BW
	fprintf(fp, "%d", TST_LONGBIMPY_BW);
#else
	fprintf(fp, "IAW");
#endif

	fprintf(fp, ",	// The width of i_b, can be anything\n"
			"\t\t\t// The following three parameters should not be changed\n"
			"\t\t\t// by any implementation, but are based upon hardware\n"
			"\t\t\t// and the above values:\n"
			"\t\t\tOW=IAW+IBW;	// The output width\n");
	fprintf(fp,
	"\tlocalparam	AW = (IAW<IBW) ? IAW : IBW,\n"
			"\t\t\tBW = (IAW<IBW) ? IBW : IAW,\n"
			"\t\t\tIW=(AW+1)&(-2),	// Internal width of A\n"
			"\t\t\tLUTB=2,	// How many bits we can multiply by at once\n"
			"\t\t\tTLEN=(AW+(LUTB-1))/LUTB; // Nmbr of rows in our tableau\n"
	"\tinput\t\t\t\ti_clk, i_ce;\n"
	"\tinput\t\t[(IAW-1):0]\ti_a_unsorted;\n"
	"\tinput\t\t[(IBW-1):0]\ti_b_unsorted;\n"
	"\toutput\treg\t[(AW+BW-1):0]\to_r;\n"
"\n"
	"\t//\n"
	"\t// Swap parameter order, so that AW <= BW -- for performance\n"
	"\t// reasons\n"
	"\twire	[AW-1:0]	i_a;\n"
	"\twire	[BW-1:0]	i_b;\n"
	"\tgenerate if (IAW <= IBW)\n"
	"\tbegin : NO_PARAM_CHANGE\n"
	"\t\tassign i_a = i_a_unsorted;\n"
	"\t\tassign i_b = i_b_unsorted;\n"
	"\tend else begin : SWAP_PARAMETERS\n"
	"\t\tassign i_a = i_b_unsorted;\n"
	"\t\tassign i_b = i_a_unsorted;\n"
	"\tend endgenerate\n"
"\n"
	"\treg\t[(IW-1):0]\tu_a;\n"
	"\treg\t[(BW-1):0]\tu_b;\n"
	"\treg\t\t\tsgn;\n"
"\n"
	"\treg\t[(IW-1-2*(LUTB)):0]\tr_a[0:(TLEN-3)];\n"
	"\treg\t[(BW-1):0]\t\tr_b[0:(TLEN-3)];\n"
	"\treg\t[(TLEN-1):0]\t\tr_s;\n"
	"\treg\t[(IW+BW-1):0]\t\tacc[0:(TLEN-2)];\n"
	"\tgenvar k;\n"
"\n"
	"\t// First step:\n"
	"\t// Switch to unsigned arithmetic for our multiply, keeping track\n"
	"\t// of the along the way.  We'll then add the sign again later at\n"
	"\t// the end.\n"
	"\t//\n"
	"\t// If we were forced to stay within two's complement arithmetic,\n"
	"\t// taking the absolute value here would require an additional bit.\n"
	"\t// However, because our results are now unsigned, we can stay\n"
	"\t// within the number of bits given (for now).\n"
	"\tgenerate if (IW > AW)\n"
	"\tbegin\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\n"
			"\t\t\t\tu_a <= { 1\'b0, (i_a[AW-1])?(-i_a):(i_a) };\n"
	"\tend else begin\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\n"
			"\t\t\t\tu_a <= (i_a[AW-1])?(-i_a):(i_a);\n"
	"\tend endgenerate\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tu_b <= (i_b[BW-1])?(-i_b):(i_b);\n"
			"\t\t\tsgn <= i_a[AW-1] ^ i_b[BW-1];\n"
		"\t\tend\n"
"\n"
	"\twire	[(BW+LUTB-1):0]	pr_a, pr_b;\n"
"\n"
	"\t//\n"
	"\t// Second step: First two 2xN products.\n"
	"\t//\n"
	"\t// Since we have no tableau of additions (yet), we can do both\n"
	"\t// of the first two rows at the same time and add them together.\n"
	"\t// For the next round, we'll then have a previous sum to accumulate\n"
	"\t// with new and subsequent product, and so only do one product at\n"
	"\t// a time can follow this--but the first clock can do two at a time.\n"
	"\tbimpy\t#(BW) lmpy_0(i_clk,i_ce,u_a[(  LUTB-1):   0], u_b, pr_a);\n"
	"\tbimpy\t#(BW) lmpy_1(i_clk,i_ce,u_a[(2*LUTB-1):LUTB], u_b, pr_b);\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) r_a[0] <= u_a[(IW-1):(2*LUTB)];\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) r_b[0] <= u_b;\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) r_s <= { r_s[(TLEN-2):0], sgn };\n"
	"\talways @(posedge i_clk) // One clk after p[0],p[1] become valid\n"
		"\t\tif (i_ce) acc[0] <= { {(IW-LUTB){1\'b0}}, pr_a}\n"
			"\t\t\t  +{ {(IW-(2*LUTB)){1\'b0}}, pr_b, {(LUTB){1\'b0}} };\n"
"\n"
	"\tgenerate // Keep track of intermediate values, before multiplying them\n"
	"\tif (TLEN > 3) for(k=0; k<TLEN-3; k=k+1)\n"
	"\tbegin : gencopies\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tr_a[k+1] <= { {(LUTB){1\'b0}},\n"
				"\t\t\t\tr_a[k][(IW-1-(2*LUTB)):LUTB] };\n"
			"\t\t\tr_b[k+1] <= r_b[k];\n"
			"\t\tend\n"
	"\tend endgenerate\n"
"\n"
	"\tgenerate // The actual multiply and accumulate stage\n"
	"\tif (TLEN > 2) for(k=0; k<TLEN-2; k=k+1)\n"
	"\tbegin : genstages\n"
		"\t\t// First, the multiply: 2-bits times BW bits\n"
		"\t\twire\t[(BW+LUTB-1):0] genp;\n"
		"\t\tbimpy #(BW) genmpy(i_clk,i_ce,r_a[k][(LUTB-1):0],r_b[k], genp);\n"
"\n"
		"\t\t// Then the accumulate step -- on the next clock\n"
		"\t\talways @(posedge i_clk)\n"
			"\t\t\tif (i_ce)\n"
				"\t\t\t\tacc[k+1] <= acc[k] + {{(IW-LUTB*(k+3)){1\'b0}},\n"
					"\t\t\t\t\tgenp, {(LUTB*(k+2)){1\'b0}} };\n"
	"\tend endgenerate\n"
"\n"
	"\twire	[(IW+BW-1):0]	w_r;\n"
	"\tassign\tw_r = (r_s[TLEN-1]) ? (-acc[TLEN-2]) : acc[TLEN-2];\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\to_r <= w_r[(AW+BW-1):0];\n"
"\n"
	"\tgenerate if (IW > AW)\n"
	"\tbegin : VUNUSED\n"
	"\t\t// verilator lint_off UNUSED\n"
	"\t\twire\t[(IW-AW)-1:0]\tunused;\n"
	"\t\tassign\tunused = w_r[(IW+BW-1):(AW+BW)];\n"
	"\t\t// verilator lint_on UNUSED\n"
	"\tend endgenerate\n"
"\n"
"endmodule\n");

	fclose(fp);
}

void	build_snglbrev(const char *fname, const bool async_reset = false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tsnglbrev.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis module bitreverses a pipelined FFT input.  It differes\n"
"//		from the dblreverse module in that this is just a simple and\n"
"//	straightforward bitreverse, rather than one written to handle two\n"
"//	words at once.\n"
"//\n"
"//\n%s"
"//\n", prjname, creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	snglbrev(i_clk, %s, i_ce, i_in, o_out, o_sync);\n"
	"\tparameter\t\t\tLGSIZE=%d, WIDTH=24;\n"
	"\tinput\t\t\t\ti_clk, %s, i_ce;\n"
	"\tinput\t\t[(2*WIDTH-1):0]\ti_in;\n"
	"\toutput\twire\t[(2*WIDTH-1):0]\to_out;\n"
	"\toutput\treg\t\t\to_sync;\n", resetw.c_str(),
		TST_DBLREVERSE_LGSIZE,
		resetw.c_str());

	fprintf(fp,
"	reg	[(LGSIZE):0]	wraddr;\n"
"	wire	[(LGSIZE):0]	rdaddr;\n"
"\n"
"	reg	[(2*WIDTH-1):0]	brmem	[0:((1<<(LGSIZE+1))-1)];\n"
"\n"
"	genvar	k;\n"
"	generate for(k=0; k<LGSIZE; k=k+1)\n"
"		assign rdaddr[k] = wraddr[LGSIZE-1-k];\n"
"	endgenerate\n"
"	assign	rdaddr[LGSIZE] = ~wraddr[LGSIZE];\n"
"\n"
"	reg	in_reset;\n"
"\n"
"	initial	in_reset = 1'b1;\n");

	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
"			in_reset <= 1'b1;\n"
"		else if ((i_ce)&&(&wraddr[(LGSIZE-1):0]))\n"
"			in_reset <= 1'b0;\n"
"\n"
"	initial	wraddr = 0;\n");

	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
"			wraddr <= 0;\n"
"		else if (i_ce)\n"
"		begin\n"
"			brmem[wraddr] <= i_in;\n"
"			wraddr <= wraddr + 1;\n"
"		end\n"
"\n"
"	always @(posedge i_clk)\n"
"		if (i_ce) // If (i_reset) we just output junk ... not a problem\n"
"			o_out <= brmem[rdaddr]; // w/o a sync pulse\n"
"\n"
"	initial	o_sync = 1'b0;\n");

	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
"			o_sync <= 1'b0;\n"
"		else if ((i_ce)&&(!in_reset))\n"
"			o_sync <= (wraddr[(LGSIZE-1):0] == 0);\n"
"\n"
"endmodule\n");


}

void	build_dblreverse(const char *fname, const bool async_reset = false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");

	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tdblreverse.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis module bitreverses a pipelined FFT input.  Operation is\n"
"//		expected as follows:\n"
"//\n"
"//		i_clk	A running clock at whatever system speed is offered.\n",
	prjname);

	if (async_reset)
		fprintf(fp,
"//		i_areset_n	An active low asynchronous reset signal,\n"
"//				that resets all internals\n");
	else
		fprintf(fp,
"//		i_reset	A synchronous reset signal, that resets all internals\n");

	fprintf(fp,
"//		i_ce	If this is one, one input is consumed and an output\n"
"//			is produced.\n"
"//		i_in_0, i_in_1\n"
"//			Two inputs to be consumed, each of width WIDTH.\n"
"//		o_out_0, o_out_1\n"
"//			Two of the bitreversed outputs, also of the same\n"
"//			width, WIDTH.  Of course, there is a delay from the\n"
"//			first input to the first output.  For this purpose,\n"
"//			o_sync is present.\n"
"//		o_sync	This will be a 1\'b1 for the first value in any block.\n"
"//			Following a reset, this will only become 1\'b1 once\n"
"//			the data has been loaded and is now valid.  After that,\n"
"//			all outputs will be valid.\n"
"//\n"
"//	20150602 -- This module has undergone massive rework in order to\n"
"//		ensure that it uses resources efficiently.  As a result,\n"
"//		it now optimizes nicely into block RAMs.  As an unfortunately\n"
"//		side effect, it now passes it\'s bench test (dblrev_tb) but\n"
"//		fails the integration bench test (fft_tb).\n"
"//\n"
"//\n%s"
"//\n", creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"\n\n"
"//\n"
"// How do we do bit reversing at two smples per clock?  Can we separate out\n"
"// our work into eight memory banks, writing two banks at once and reading\n"
"// another two banks in the same clock?\n"
"//\n"
"//	mem[00xxx0] = s_0[n]\n"
"//	mem[00xxx1] = s_1[n]\n"
"//	o_0[n] = mem[10xxx0]\n"
"//	o_1[n] = mem[11xxx0]\n"
"//	...\n"
"//	mem[01xxx0] = s_0[m]\n"
"//	mem[01xxx1] = s_1[m]\n"
"//	o_0[m] = mem[10xxx1]\n"
"//	o_1[m] = mem[11xxx1]\n"
"//	...\n"
"//	mem[10xxx0] = s_0[n]\n"
"//	mem[10xxx1] = s_1[n]\n"
"//	o_0[n] = mem[00xxx0]\n"
"//	o_1[n] = mem[01xxx0]\n"
"//	...\n"
"//	mem[11xxx0] = s_0[m]\n"
"//	mem[11xxx1] = s_1[m]\n"
"//	o_0[m] = mem[00xxx1]\n"
"//	o_1[m] = mem[01xxx1]\n"
"//	...\n"
"//\n"
"//	The answer is that, yes we can but: we need to use four memory banks\n"
"//	to do it properly.  These four banks are defined by the two bits\n"
"//	that determine the top and bottom of the correct address.  Larger\n"
"//	FFT\'s would require more memories.\n"
"//\n"
"//\n");
	fprintf(fp,
"module	dblreverse(i_clk, %s, i_ce, i_in_0, i_in_1,\n"
	"\t\to_out_0, o_out_1, o_sync);\n"
	"\tparameter\t\t\tLGSIZE=%d, WIDTH=24;\n"
	"\tinput\t\t\t\ti_clk, %s, i_ce;\n"
	"\tinput\t\t[(2*WIDTH-1):0]\ti_in_0, i_in_1;\n"
	"\toutput\twire\t[(2*WIDTH-1):0]\to_out_0, o_out_1;\n"
	"\toutput\treg\t\t\to_sync;\n", resetw.c_str(), TST_DBLREVERSE_LGSIZE,
		resetw.c_str());

	fprintf(fp,
"\n"
	"\treg\t\t\tin_reset;\n"
	"\treg\t[(LGSIZE-1):0]\tiaddr;\n"
	"\twire\t[(LGSIZE-3):0]\tbraddr;\n"
"\n"
	"\tgenvar\tk;\n"
	"\tgenerate for(k=0; k<LGSIZE-2; k=k+1)\n"
	"\tbegin : gen_a_bit_reversed_value\n"
		"\t\tassign braddr[k] = iaddr[LGSIZE-3-k];\n"
	"\tend endgenerate\n"
"\n"
	"\tinitial iaddr = 0;\n"
	"\tinitial in_reset = 1\'b1;\n"
	"\tinitial o_sync = 1\'b0;\n");

	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\tbegin\n"
			"\t\t\tiaddr <= 0;\n"
			"\t\t\tin_reset <= 1\'b1;\n"
			"\t\t\to_sync <= 1\'b0;\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\tiaddr <= iaddr + { {(LGSIZE-1){1\'b0}}, 1\'b1 };\n"
			"\t\t\tif (&iaddr[(LGSIZE-2):0])\n"
				"\t\t\t\tin_reset <= 1\'b0;\n"
			"\t\t\tif (in_reset)\n"
				"\t\t\t\to_sync <= 1\'b0;\n"
			"\t\t\telse\n"
				"\t\t\t\to_sync <= ~(|iaddr[(LGSIZE-2):0]);\n"
		"\t\tend\n"
"\n"
	"\treg\t[(2*WIDTH-1):0]\tmem_e [0:((1<<(LGSIZE))-1)];\n"
	"\treg\t[(2*WIDTH-1):0]\tmem_o [0:((1<<(LGSIZE))-1)];\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\tmem_e[iaddr] <= i_in_0;\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\tmem_o[iaddr] <= i_in_1;\n"
"\n"
"\n"
	"\treg [(2*WIDTH-1):0] evn_out_0, evn_out_1, odd_out_0, odd_out_1;\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n\t\t\tevn_out_0 <= mem_e[{~iaddr[LGSIZE-1],1\'b0,braddr}];\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n\t\t\tevn_out_1 <= mem_e[{~iaddr[LGSIZE-1],1\'b1,braddr}];\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n\t\t\todd_out_0 <= mem_o[{~iaddr[LGSIZE-1],1\'b0,braddr}];\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n\t\t\todd_out_1 <= mem_o[{~iaddr[LGSIZE-1],1\'b1,braddr}];\n"
"\n"
	"\treg\tadrz;\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) adrz <= iaddr[LGSIZE-2];\n"
"\n"
	"\tassign\to_out_0 = (adrz)?odd_out_0:evn_out_0;\n"
	"\tassign\to_out_1 = (adrz)?odd_out_1:evn_out_1;\n"
"\n"
"endmodule\n");

	fclose(fp);
}

void	build_butterfly(const char *fname, int xtracbits, ROUND_T rounding,
			int	ckpce = 1,
			const bool async_reset = false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}
	const	char	*rnd_string;
	if (rounding == RND_TRUNCATE)
		rnd_string = "truncate";
	else if (rounding == RND_FROMZERO)
		rnd_string = "roundfromzero";
	else if (rounding == RND_HALFUP)
		rnd_string = "roundhalfup";
	else
		rnd_string = "convround";

	//if (ckpce >= 3)
		//ckpce = 3;
	if (ckpce <= 1)
		ckpce = 1;
	if (ckpce > 1) {
		fprintf(stderr, "WARNING: Butterfly code does not yet support CKPCE=%d\n", ckpce);
		fprintf(stderr, "WARNING: Using CKPCE=1 instead\n");
		ckpce = 1;
	}

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");


	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\tbutterfly.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis routine caculates a butterfly for a decimation\n"
"//		in frequency version of an FFT.  Specifically, given\n"
"//	complex Left and Right values together with a coefficient, the output\n"
"//	of this routine is given by:\n"
"//\n"
"//		L' = L + R\n"
"//		R' = (L - R)*C\n"
"//\n"
"//	The rest of the junk below handles timing (mostly), to make certain\n"
"//	that L' and R' reach the output at the same clock.  Further, just to\n"
"//	make certain that is the case, an 'aux' input exists.  This aux value\n"
"//	will come out of this routine synchronized to the values it came in\n"
"//	with.  (i.e., both L', R', and aux all have the same delay.)  Hence,\n"
"//	a caller of this routine may set aux on the first input with valid\n"
"//	data, and then wait to see aux set on the output to know when to find\n"
"//	the first output with valid data.\n"
"//\n"
"//	All bits are preserved until the very last clock, where any more bits\n"
"//	than OWIDTH will be quietly discarded.\n"
"//\n"
"//	This design features no overflow checking.\n"
"//\n"
"// Notes:\n"
"//	CORDIC:\n"
"//		Much as we might like, we can't use a cordic here.\n"
"//		The goal is to accomplish an FFT, as defined, and a\n"
"//		CORDIC places a scale factor onto the data.  Removing\n"
"//		the scale factor would cost two multiplies, which\n"
"//		is precisely what we are trying to avoid.\n"
"//\n"
"//\n"
"//	3-MULTIPLIES:\n"
"//		It should also be possible to do this with three multiplies\n"
"//		and an extra two addition cycles.\n"
"//\n"
"//		We want\n"
"//			R+I = (a + jb) * (c + jd)\n"
"//			R+I = (ac-bd) + j(ad+bc)\n"
"//		We multiply\n"
"//			P1 = ac\n"
"//			P2 = bd\n"
"//			P3 = (a+b)(c+d)\n"
"//		Then\n"
"//			R+I=(P1-P2)+j(P3-P2-P1)\n"
"//\n"
"//		WIDTHS:\n"
"//		On multiplying an X width number by an\n"
"//		Y width number, X>Y, the result should be (X+Y)\n"
"//		bits, right?\n"
"//		-2^(X-1) <= a <= 2^(X-1) - 1\n"
"//		-2^(Y-1) <= b <= 2^(Y-1) - 1\n"
"//		(2^(Y-1)-1)*(-2^(X-1)) <= ab <= 2^(X-1)2^(Y-1)\n"
"//		-2^(X+Y-2)+2^(X-1) <= ab <= 2^(X+Y-2) <= 2^(X+Y-1) - 1\n"
"//		-2^(X+Y-1) <= ab <= 2^(X+Y-1)-1\n"
"//		YUP!  But just barely.  Do this and you'll really want\n"
"//		to drop a bit, although you will risk overflow in so\n"
"//		doing.\n"
"//\n"
"//	20150602 -- The sync logic lines have been completely redone.  The\n"
"//		synchronization lines no longer go through the FIFO with the\n"
"//		left hand sum, but are kept out of memory.  This allows the\n"
"//		butterfly to use more optimal memory resources, while also\n"
"//		guaranteeing that the sync lines can be properly reset upon\n"
"//		any reset signal.\n"
"//\n"
"//\n%s"
"//\n", prjname, creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");

	fprintf(fp,
"module\tbutterfly(i_clk, %s, i_ce, i_coef, i_left, i_right, i_aux,\n"
		"\t\to_left, o_right, o_aux);\n"
	"\t// Public changeable parameters ...\n"
	"\tparameter IWIDTH=%d,", resetw.c_str(), TST_BUTTERFLY_IWIDTH);
#ifdef	TST_BUTTERFLY_CWIDTH
	fprintf(fp, "CWIDTH=%d,", TST_BUTTERFLY_CWIDTH);
#else
	fprintf(fp, "CWIDTH=IWIDTH+%d,", xtracbits);
#endif
#ifdef	TST_BUTTERFLY_OWIDTH
	fprintf(fp, "OWIDTH=%d;\n", TST_BUTTERFLY_OWIDTH);
#else
	fprintf(fp, "OWIDTH=IWIDTH+1;\n");
#endif
	fprintf(fp,
	"\t// Parameters specific to the core that should not be changed.\n"
	"\tparameter	MPYDELAY=%d'd%d,\n"
			"\t\t\tSHIFT=0, AUXLEN=(MPYDELAY+3);\n"
	"\t// The LGDELAY should be the base two log of the MPYDELAY.  If\n"
	"\t// this value is fractional, then round up to the nearest\n"
	"\t// integer: LGDELAY=ceil(log(MPYDELAY)/log(2));\n"
	"\tparameter\tLGDELAY=%d;\n"
	"\tparameter\tCKPCE=%d;\n"
	"\tinput\t\ti_clk, %s, i_ce;\n"
	"\tinput\t\t[(2*CWIDTH-1):0] i_coef;\n"
	"\tinput\t\t[(2*IWIDTH-1):0] i_left, i_right;\n"
	"\tinput\t\ti_aux;\n"
	"\toutput\twire	[(2*OWIDTH-1):0] o_left, o_right;\n"
	"\toutput\treg\to_aux;\n"
	"\n", lgdelay(16,xtracbits), bflydelay(16, xtracbits),
		lgdelay(16,xtracbits), ckpce, resetw.c_str());
	fprintf(fp,
	"\treg\t[(2*IWIDTH-1):0]\tr_left, r_right;\n"
	"\treg\t[(2*CWIDTH-1):0]\tr_coef, r_coef_2;\n"
	"\twire\tsigned\t[(IWIDTH-1):0]\tr_left_r, r_left_i, r_right_r, r_right_i;\n"
	"\tassign\tr_left_r  = r_left[ (2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\tr_left_i  = r_left[ (IWIDTH-1):0];\n"
	"\tassign\tr_right_r = r_right[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\tr_right_i = r_right[(IWIDTH-1):0];\n"
"\n"
	"\treg\tsigned\t[(IWIDTH):0]\tr_sum_r, r_sum_i, r_dif_r, r_dif_i;\n"
"\n"
	"\treg	[(LGDELAY-1):0]	fifo_addr;\n"
	"\twire	[(LGDELAY-1):0]	fifo_read_addr;\n"
	"\tassign\tfifo_read_addr = fifo_addr - MPYDELAY[(LGDELAY-1):0];\n"
	"\treg	[(2*IWIDTH+1):0]	fifo_left [ 0:((1<<LGDELAY)-1)];\n"
"\n");
	fprintf(fp,
	"\t// Set up the input to the multiply\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// One clock just latches the inputs\n"
			"\t\t\tr_left <= i_left;	// No change in # of bits\n"
			"\t\t\tr_right <= i_right;\n"
			"\t\t\tr_coef  <= i_coef;\n"
			"\t\t\t// Next clock adds/subtracts\n"
			"\t\t\tr_sum_r <= r_left_r + r_right_r; // Now IWIDTH+1 bits\n"
			"\t\t\tr_sum_i <= r_left_i + r_right_i;\n"
			"\t\t\tr_dif_r <= r_left_r - r_right_r;\n"
			"\t\t\tr_dif_i <= r_left_i - r_right_i;\n"
			"\t\t\t// Other inputs are simply delayed on second clock\n"
			"\t\t\tr_coef_2<= r_coef;\n"
	"\t\tend\n"
"\n");
	fprintf(fp,
	"\t// Don\'t forget to record the even side, since it doesn\'t need\n"
	"\t// to be multiplied, but yet we still need the results in sync\n"
	"\t// with the answer when it is ready.\n"
	"\tinitial fifo_addr = 0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
			"\t\t\tfifo_addr <= 0;\n"
		"\t\telse if (i_ce)\n"
			"\t\t\t// Need to delay the sum side--nothing else happens\n"
			"\t\t\t// to it, but it needs to stay synchronized with the\n"
			"\t\t\t// right side.\n"
			"\t\t\tfifo_addr <= fifo_addr + 1;\n"
"\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\tfifo_left[fifo_addr] <= { r_sum_r, r_sum_i };\n"
"\n"
	"\twire\tsigned\t[(CWIDTH-1):0]	ir_coef_r, ir_coef_i;\n"
	"\tassign\tir_coef_r = r_coef_2[(2*CWIDTH-1):CWIDTH];\n"
	"\tassign\tir_coef_i = r_coef_2[(CWIDTH-1):0];\n"
	"\twire\tsigned\t[((IWIDTH+2)+(CWIDTH+1)-1):0]\tp_one, p_two, p_three;\n"
"\n"
"\n");
	fprintf(fp,
	"\t// Multiply output is always a width of the sum of the widths of\n"
	"\t// the two inputs.  ALWAYS.  This is independent of the number of\n"
	"\t// bits in p_one, p_two, or p_three.  These values needed to\n"
	"\t// accumulate a bit (or two) each.  However, this approach to a\n"
	"\t// three multiply complex multiply cannot increase the total\n"
	"\t// number of bits in our final output.  We\'ll take care of\n"
	"\t// dropping back down to the proper width, OWIDTH, in our routine\n"
	"\t// below.\n"
"\n"
"\n");
	fprintf(fp,
	"\t// We accomplish here \"Karatsuba\" multiplication.  That is,\n"
	"\t// by doing three multiplies we accomplish the work of four.\n"
	"\t// Let\'s prove to ourselves that this works ... We wish to\n"
	"\t// multiply: (a+jb) * (c+jd), where a+jb is given by\n"
	"\t//\ta + jb = r_dif_r + j r_dif_i, and\n"
	"\t//\tc + jd = ir_coef_r + j ir_coef_i.\n"
	"\t// We do this by calculating the intermediate products P1, P2,\n"
	"\t// and P3 as\n"
	"\t//\tP1 = ac\n"
	"\t//\tP2 = bd\n"
	"\t//\tP3 = (a + b) * (c + d)\n"
	"\t// and then complete our final answer with\n"
	"\t//\tac - bd = P1 - P2 (this checks)\n"
	"\t//\tad + bc = P3 - P2 - P1\n"
	"\t//\t        = (ac + bc + ad + bd) - bd - ac\n"
	"\t//\t        = bc + ad (this checks)\n"
"\n"
"\n");
	fprintf(fp,
	"\t// This should really be based upon an IF, such as in\n"
	"\t// if (IWIDTH < CWIDTH) then ...\n"
	"\t// However, this is the only (other) way I know to do it.\n"
	"\tgenerate if (CKPCE <= 1)\n"
	"\tbegin\n"
"\n"
		"\t\twire\t[(CWIDTH):0]\tp3c_in;\n"
		"\t\twire\t[(IWIDTH+1):0]\tp3d_in;\n"
		"\t\tassign\tp3c_in = ir_coef_i + ir_coef_r;\n"
		"\t\tassign\tp3d_in = r_dif_r + r_dif_i;\n"
		"\n"
		"\t\t// We need to pad these first two multiplies by an extra\n"
		"\t\t// bit just to keep them aligned with the third,\n"
		"\t\t// simpler, multiply.\n"
		"\t\t%s #(CWIDTH+1,IWIDTH+2) p1(i_clk, i_ce,\n"
				"\t\t\t\t{ir_coef_r[CWIDTH-1],ir_coef_r},\n"
				"\t\t\t\t{r_dif_r[IWIDTH],r_dif_r}, p_one);\n"
		"\t\t%s #(CWIDTH+1,IWIDTH+2) p2(i_clk, i_ce,\n"
				"\t\t\t\t{ir_coef_i[CWIDTH-1],ir_coef_i},\n"
				"\t\t\t\t{r_dif_i[IWIDTH],r_dif_i}, p_two);\n"
		"\t\t%s #(CWIDTH+1,IWIDTH+2) p3(i_clk, i_ce,\n"
			"\t\t\t\tp3c_in, p3d_in, p_three);\n"
"\n"
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy",
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy",
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy");

	///////////////////////////////////////////
	///
	///	Two clocks per CE, so CE, no-ce, CE, no-ce, etc
	///
	fprintf(fp,
	"\tend else if (CKPCE == 2)\n"
	"\tbegin : CKPCE_TWO\n"
		"\t\t// Coefficient multiply inputs\n"
		"\t\treg		[2*(CWIDTH)-1:0]	mpy_pipe_c;\n"
		"\t\t// Data multiply inputs\n"
		"\t\treg		[2*(IWIDTH+1)-1:0]	mpy_pipe_d;\n"
		"\t\twire	signed	[(CWIDTH-1):0]	mpy_pipe_vc;\n"
		"\t\twire	signed	[(IWIDTH):0]	mpy_pipe_vd;\n"
		"\t\t//\n"
		"\t\treg	signed	[(CWIDTH)-1:0]		mpy_cof_sum;\n"
		"\t\treg	signed	[(IWIDTH+1)-1:0]	mpy_dif_sum;\n"
"\n"
		"\t\tassign	mpy_pipe_vc =  mpy_pipe_c[2*(CWIDTH)-1:CWIDTH];\n"
		"\t\tassign	mpy_pipe_vd =  mpy_pipe_d[2*(IWIDTH+1)-1:IWIDTH+1];\n"
"\n"
		"\t\treg			mpy_pipe_v;\n"
		"\t\treg			ce_phase;\n"
"\n"
		"\t\treg	signed	[(CWIDTH+IWIDTH+1)-1:0]	mpy_pipe_out;\n"
		"\t\treg	signed [IWIDTH+CWIDTH+3-1:0]	longmpy;\n"
"\n"
"\n"
		"\t\tinitial	ce_phase = 1'b0;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_reset)\n"
			"\t\t\tce_phase <= 1'b0;\n"
		"\t\telse if (i_ce)\n"
			"\t\t\tce_phase <= 1'b1;\n"
		"\t\telse\n"
			"\t\t\tce_phase <= 1'b0;\n"
"\n"
		"\t\talways @(*)\n"
			"\t\t\tmpy_pipe_v = (i_ce)||(ce_phase);\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (ce_phase)\n"
		"\t\tbegin\n"
			"\t\t\t// Pre-clock\n"
			"\t\t\tmpy_pipe_c[2*CWIDTH-1:0] <=\n"
				"\t\t\t\t\t{ ir_coef_r, ir_coef_i };\n"
			"\t\t\tmpy_pipe_d[2*(IWIDTH+1)-1:0] <=\n"
				"\t\t\t\t\t{ r_dif_r, r_dif_i };\n"
"\n"
			"\t\t\tmpy_cof_sum  <= ir_coef_i + ir_coef_r;\n"
			"\t\t\tmpy_dif_sum <= r_dif_r + r_dif_i;\n"
"\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// First clock\n"
			"\t\t\tmpy_pipe_c[2*(CWIDTH)-1:0] <= {\n"
				"\t\t\t\tmpy_pipe_c[(CWIDTH)-1:0], {(CWIDTH){1'b0}} };\n"
			"\t\t\tmpy_pipe_d[2*(IWIDTH+1)-1:0] <= {\n"
				"\t\t\t\tmpy_pipe_d[(IWIDTH+1)-1:0], {(IWIDTH+1){1'b0}} };\n"
		"\t\tend\n"
"\n"
	fprintf(fp,
		"\t\t%s #(CWIDTH+1,IWIDTH+2) mpy0(i_clk, mpy_pipe_v,\n"
			"\t\t\t\tmpy_cof_sum, mpy_dif_sum, longmpy);\n"
"\n"
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy",
"\n");

	fprintf(fp,
		"\t\t%s #(CWIDTH,IWIDTH+1) mpy1(i_clk, mpy_pipe_v,\n"
			"\t\t\t\tmpy_pipe_vc, mpy_pipe_vd, mpy_pipe_out);\n"
"\n"
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy",
"\n");

	fprintf(fp,
		"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]	rp_one;\n"
		"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]	rp2_one,\n"
				"\t\t\t\t\t\t\t\trp_two;\n"
		"\t\treg\tsigned\t[((IWIDTH+2)+(CWIDTH+1)-1):0]	rp_three;\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (ce_phase) // 1.5 clock\n"
			"\t\t\trp_one <= mpy_pipe_out;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) // two clocks\n"
			"\t\t\trp_two <= mpy_pipe_out;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) // Second clock\n"
			"\t\t\trp_three<= longmpy;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\trp2_one<= rp_one;\n"
"\n"
		"\t\tassign	p_one	= rp2_one;\n"
		"\t\tassign	p_two	= rp_two;\n"
		"\t\tassign	p_three	= rp_three;\n"
"\n");

	/////////////////////////
	///
	///	Three clock per CE, so CE, no-ce, no-ce*, CE
	///
	fprintf(fp,
"\tend else if (CKPCE <= 3)\n\tbegin : CKPCE_THREE\n");

	fprintf(fp,
	"\t\t// Coefficient multiply inputs\n"
	"\t\treg\t\t[3*(CWIDTH+1)-1:0]\tmpy_pipe_c;\n"
	"\t\t// Data multiply inputs\n"
	"\t\treg\t\t[3*(IWIDTH+2)-1:0]\tmpy_pipe_d;\n"
	"\t\twire\tsigned	[(CWIDTH):0]	mpy_pipe_vc;\n"
	"\t\twire\tsigned	[(IWIDTH+1):0]	mpy_pipe_vd;\n"
	"\n"
	"\t\tassign\tmpy_pipe_vc =  mpy_pipe_c[3*(CWIDTH+1)-1:2*(CWIDTH+1)];\n"
	"\t\tassign\tmpy_pipe_vd =  mpy_pipe_d[3*(IWIDTH+2)-1:2*(IWIDTH+2)];\n"
	"\n"
	"\t\treg\t\t\tmpy_pipe_v;\n"
	"\t\treg\t\t[2:0]\tce_phase;\n"
	"\n"
	"\t\treg\tsigned	[  (CWIDTH+IWIDTH+3)-1:0]	mpy_pipe_out;\n"
"\n");
	fprintf(fp,
	"\t\tinitial\tce_phase = 3'b011;\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif (i_reset)\n"
		"\t\t\tce_phase <= 3'b011;\n"
	"\t\telse if (i_ce)\n"
		"\t\t\tce_phase <= 3'b000;\n"
	"\t\telse if (ce_phase != 3'b011)\n"
		"\t\t\tce_phase <= ce_phase + 1'b1;\n"
"\n"
	"\t\talways @(*)\n"
		"\t\t\tmpy_pipe_v = (i_ce)||(ce_phase < 3'b010);\n"
"\n");

	fprintf(fp,
	"\t\talways @(posedge i_clk)\n"
		"\t\t\tif (ce_phase == 3\'b000)\n"
		"\t\t\tbegin\n"
			"\t\t\t\t// Second clock\n"
			"\t\t\t\tmpy_pipe_c[3*(CWIDTH+1)-1:(CWIDTH+1)] <= {\n"
			"\t\t\t\t\tir_coef_r[CWIDTH-1], ir_coef_r,\n"
			"\t\t\t\t\tir_coef_i[CWIDTH-1], ir_coef_i };\n"
			"\t\t\t\tmpy_pipe_c[CWIDTH:0] <= ir_coef_i + ir_coef_r;\n"
			"\t\t\t\tmpy_pipe_d[3*(IWIDTH+2)-1:(IWIDTH+2)] <= {\n"
			"\t\t\t\t\tr_dif_r[IWIDTH], r_dif_r,\n"
			"\t\t\t\t\tr_dif_i[IWIDTH], r_dif_i };\n"
			"\t\t\t\tmpy_pipe_d[(IWIDTH+2)-1:0] <= r_dif_r + r_dif_i;\n"
"\n"
		"\t\t\tend else if (mpy_pipe_v)\n"
		"\t\t\tbegin\n"
			"\t\t\t\tmpy_pipe_c[3*(CWIDTH+1)-1:0] <= {\n"
			"\t\t\t\t\tmpy_pipe_c[2*(CWIDTH+1)-1:0], {(CWIDTH+1){1\'b0}} };\n"
			"\t\t\t\tmpy_pipe_d[3*(IWIDTH+2)-1:0] <= {\n"
			"\t\t\t\t\tmpy_pipe_d[2*(IWIDTH+2)-1:0], {(IWIDTH+2){1\'b0}} };\n"
		"\t\t\tend\n"
"\n");
	fprintf(fp,
		"\t\t%s #(CWIDTH+1,IWIDTH+2) mpy(i_clk, mpy_pipe_v,\n"
			"\t\t\t\tmpy_pipe_vc, mpy_pipe_vd, mpy_pipe_out);\n"
"\n"
		(USE_OLD_MULTIPLY)?"shiftaddmpy":"longbimpy",
"\n");

	fprintf(fp,
	"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]\trp_one, rp_two;\n"
	"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]\trp2_one, rp2_two;\n"
	"\t\treg\tsigned\t[((IWIDTH+2)+(CWIDTH+1)-1):0]\trp_three, rp2_three;\n"

"\n");

	fprintf(fp,
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(i_ce)\n"
		"\t\t\trp_one <= mpy_pipe_out[(CWIDTH+IWIDTH+3)-3:0];\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(ce_phase == 3'b000)\n"
		"\t\t\trp_two <= mpy_pipe_out[(CWIDTH+IWIDTH+3)-3:0];\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(ce_phase == 3'b001)\n"
		"\t\t\trp_three <= mpy_pipe_out;\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\tbegin\n"
		"\t\t\trp2_one<= rp_one;\n"
		"\t\t\trp2_two<= rp_two;\n"
		"\t\t\trp2_three<= rp_three;\n"
	"\t\tend\n");
	fprintf(fp,

	"\t\tassign\tp_one\t= rp2_one;\n"
	"\t\tassign\tp_two\t= rp2_two;\n"
	"\t\tassign\tp_three\t= rp2_three;\n"
"\n");

	fprintf(fp,
"\tend endgenerate\n");

	fprintf(fp,
	"\tend else begin // if (CKPCE >= 3)\n"
	"\tend\n"
	"\tendgenerate\n"
"\n");

	fprintf(fp,
	"\t// These values are held in memory and delayed during the\n"
	"\t// multiply.  Here, we recover them.  During the multiply,\n"
	"\t// values were multiplied by 2^(CWIDTH-2)*exp{-j*2*pi*...},\n"
	"\t// therefore, the left_x values need to be right shifted by\n"
	"\t// CWIDTH-2 as well.  The additional bits come from a sign\n"
	"\t// extension.\n"
	"\twire\tsigned\t[(IWIDTH+CWIDTH):0]	fifo_i, fifo_r;\n"
	"\treg\t\t[(2*IWIDTH+1):0]	fifo_read;\n"
	"\tassign\tfifo_r = { {2{fifo_read[2*(IWIDTH+1)-1]}}, fifo_read[(2*(IWIDTH+1)-1):(IWIDTH+1)], {(CWIDTH-2){1\'b0}} };\n"
	"\tassign\tfifo_i = { {2{fifo_read[(IWIDTH+1)-1]}}, fifo_read[((IWIDTH+1)-1):0], {(CWIDTH-2){1\'b0}} };\n"
"\n"
"\n"
	"\treg\tsigned\t[(CWIDTH+IWIDTH+3-1):0]	mpy_r, mpy_i;\n"
"\n");
	fprintf(fp,
	"\t// Let's do some rounding and remove unnecessary bits.\n"
	"\t// We have (IWIDTH+CWIDTH+3) bits here, we need to drop down to\n"
	"\t// OWIDTH, and SHIFT by SHIFT bits in the process.  The trick is\n"
	"\t// that we don\'t need (IWIDTH+CWIDTH+3) bits.  We\'ve accumulated\n"
	"\t// them, but the actual values will never fill all these bits.\n"
	"\t// In particular, we only need:\n"
	"\t//\t IWIDTH bits for the input\n"
	"\t//\t     +1 bit for the add/subtract\n"
	"\t//\t+CWIDTH bits for the coefficient multiply\n"
	"\t//\t     +1 bit for the add/subtract in the complex multiply\n"
	"\t//\t ------\n"
	"\t//\t (IWIDTH+CWIDTH+2) bits at full precision.\n"
	"\t//\n"
	"\t// However, the coefficient multiply multiplied by a maximum value\n"
	"\t// of 2^(CWIDTH-2).  Thus, we only have\n"
	"\t//\t   IWIDTH bits for the input\n"
	"\t//\t       +1 bit for the add/subtract\n"
	"\t//\t+CWIDTH-2 bits for the coefficient multiply\n"
	"\t//\t       +1 (optional) bit for the add/subtract in the cpx mpy.\n"
	"\t//\t -------- ... multiply.  (This last bit may be shifted out.)\n"
	"\t//\t (IWIDTH+CWIDTH) valid output bits.\n"
	"\t// Now, if the user wants to keep any extras of these (via OWIDTH),\n"
	"\t// or if he wishes to arbitrarily shift some of these off (via\n"
	"\t// SHIFT) we accomplish that here.\n"
"\n");
	fprintf(fp,
	"\twire\tsigned\t[(OWIDTH-1):0]\trnd_left_r, rnd_left_i, rnd_right_r, rnd_right_i;\n\n");

	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_left_r(i_clk, i_ce,\n"
	"\t\t\t\t{ {2{fifo_r[(IWIDTH+CWIDTH)]}}, fifo_r }, rnd_left_r);\n\n",
		rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_left_i(i_clk, i_ce,\n"
	"\t\t\t\t{ {2{fifo_i[(IWIDTH+CWIDTH)]}}, fifo_i }, rnd_left_i);\n\n",
		rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_right_r(i_clk, i_ce,\n"
	"\t\t\t\tmpy_r, rnd_right_r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_right_i(i_clk, i_ce,\n"
	"\t\t\t\tmpy_i, rnd_right_i);\n\n", rnd_string);
	fprintf(fp,
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// First clock, recover all values\n"
			"\t\t\tfifo_read <= fifo_left[fifo_read_addr];\n"
			"\t\t\t// These values are IWIDTH+CWIDTH+3 bits wide\n"
			"\t\t\t// although they only need to be (IWIDTH+1)\n"
			"\t\t\t// + (CWIDTH) bits wide.  (We\'ve got two\n"
			"\t\t\t// extra bits we need to get rid of.)\n"
			"\t\t\tmpy_r <= p_one - p_two;\n"
			"\t\t\tmpy_i <= p_three - p_one - p_two;\n"
		"\t\tend\n"
"\n");

	fprintf(fp,
	"\treg\t[(AUXLEN-1):0]\taux_pipeline;\n"
	"\tinitial\taux_pipeline = 0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
	"\t\t\taux_pipeline <= 0;\n"
	"\t\telse if (i_ce)\n"
	"\t\t\taux_pipeline <= { aux_pipeline[(AUXLEN-2):0], i_aux };\n"
"\n");
	fprintf(fp,
	"\tinitial o_aux = 1\'b0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\t\to_aux <= 1\'b0;\n"
		"\t\telse if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// Second clock, latch for final clock\n"
			"\t\t\to_aux <= aux_pipeline[AUXLEN-1];\n"
		"\t\tend\n"
"\n");

	fprintf(fp,
	"\t// As a final step, we pack our outputs into two packed two\'s\n"
	"\t// complement numbers per output word, so that each output word\n"
	"\t// has (2*OWIDTH) bits in it, with the top half being the real\n"
	"\t// portion and the bottom half being the imaginary portion.\n"
	"\tassign	o_left = { rnd_left_r, rnd_left_i };\n"
	"\tassign	o_right= { rnd_right_r,rnd_right_i};\n"
"\n"
"endmodule\n");
	fclose(fp);
}

void	build_hwbfly(const char *fname, int xtracbits, ROUND_T rounding,
		int ckpce = 3,
		const bool async_reset= false) {
	FILE	*fp = fopen(fname, "w");
	if (NULL == fp) {
		fprintf(stderr, "Could not open \'%s\' for writing\n", fname);
		perror("O/S Err was:");
		return;
	}

	const	char	*rnd_string;
	if (rounding == RND_TRUNCATE)
		rnd_string = "truncate";
	else if (rounding == RND_FROMZERO)
		rnd_string = "roundfromzero";
	else if (rounding == RND_HALFUP)
		rnd_string = "roundhalfup";
	else
		rnd_string = "convround";

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");


	fprintf(fp,
SLASHLINE
"//\n"
"// Filename:\thwbfly.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis routine is identical to the butterfly.v routine found\n"
"//		in 'butterfly.v', save only that it uses the verilog\n"
"//	operator '*' in hopes that the synthesizer would be able to optimize\n"
"//	it with hardware resources.\n"
"//\n"
"//	It is understood that a hardware multiply can complete its operation in\n"
"//	a single clock.\n"
"//\n"
"//\n%s"
"//\n", prjname, creator);
	fprintf(fp, "%s", cpyleft);
	fprintf(fp, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fp,
"module	hwbfly(i_clk, %s, i_ce, i_coef, i_left, i_right, i_aux,\n"
		"\t\to_left, o_right, o_aux);\n"
	"\t// Public changeable parameters ...\n"
	"\tparameter IWIDTH=16,CWIDTH=IWIDTH+%d,OWIDTH=IWIDTH+1;\n"
	"\t// Parameters specific to the core that should not be changed.\n"
	"\tparameter\tSHIFT=0;\n"
	"\tparameter\t[1:0]\tCKPCE=%d;\n"
	"\tinput\t\ti_clk, %s, i_ce;\n"
	"\tinput\t\t[(2*CWIDTH-1):0]\ti_coef;\n"
	"\tinput\t\t[(2*IWIDTH-1):0]\ti_left, i_right;\n"
	"\tinput\t\ti_aux;\n"
	"\toutput\twire\t[(2*OWIDTH-1):0]\to_left, o_right;\n"
	"\toutput\treg\to_aux;\n"
"\n", resetw.c_str(), xtracbits, ckpce, resetw.c_str());
	fprintf(fp,
	"\treg\t[(2*IWIDTH-1):0]	r_left, r_right;\n"
	"\treg\t			r_aux, r_aux_2;\n"
	"\treg\t[(2*CWIDTH-1):0]	r_coef;\n"
	"\twire	signed	[(IWIDTH-1):0]	r_left_r, r_left_i, r_right_r, r_right_i;\n"
	"\tassign\tr_left_r  = r_left[ (2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\tr_left_i  = r_left[ (IWIDTH-1):0];\n"
	"\tassign\tr_right_r = r_right[(2*IWIDTH-1):(IWIDTH)];\n"
	"\tassign\tr_right_i = r_right[(IWIDTH-1):0];\n"
	"\treg	signed	[(CWIDTH-1):0]	ir_coef_r, ir_coef_i;\n"
"\n"
	"\treg	signed	[(IWIDTH):0]	r_sum_r, r_sum_i, r_dif_r, r_dif_i;\n"
"\n"
	"\treg	[(2*IWIDTH+2):0]	leftv, leftvv;\n"
"\n"
	"\t// Set up the input to the multiply\n"
	"\tinitial r_aux   = 1\'b0;\n"
	"\tinitial r_aux_2 = 1\'b0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\tbegin\n"
			"\t\t\tr_aux <= 1\'b0;\n"
			"\t\t\tr_aux_2 <= 1\'b0;\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// One clock just latches the inputs\n"
			"\t\t\tr_aux <= i_aux;\n"
			"\t\t\t// Next clock adds/subtracts\n"
			"\t\t\t// Other inputs are simply delayed on second clock\n"
			"\t\t\tr_aux_2 <= r_aux;\n"
		"\t\tend\n"
	"\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// One clock just latches the inputs\n"
			"\t\t\tr_left <= i_left;	// No change in # of bits\n"
			"\t\t\tr_right <= i_right;\n"
			"\t\t\tr_coef  <= i_coef;\n"
			"\t\t\t// Next clock adds/subtracts\n"
			"\t\t\tr_sum_r <= r_left_r + r_right_r; // Now IWIDTH+1 bits\n"
			"\t\t\tr_sum_i <= r_left_i + r_right_i;\n"
			"\t\t\tr_dif_r <= r_left_r - r_right_r;\n"
			"\t\t\tr_dif_i <= r_left_i - r_right_i;\n"
			"\t\t\t// Other inputs are simply delayed on second clock\n"
			"\t\t\tir_coef_r <= r_coef[(2*CWIDTH-1):CWIDTH];\n"
			"\t\t\tir_coef_i <= r_coef[(CWIDTH-1):0];\n"
		"\t\tend\n"
	"\n\n");
	fprintf(fp,
"\t// See comments in the butterfly.v source file for a discussion of\n"
"\t// these operations and the appropriate bit widths.\n\n");
	fprintf(fp,
	"\twire\tsigned	[((IWIDTH+1)+(CWIDTH)-1):0]	p_one, p_two;\n"
	"\twire\tsigned	[((IWIDTH+2)+(CWIDTH+1)-1):0]	p_three;\n"
"\n"
	"\tinitial leftv    = 0;\n"
	"\tinitial leftvv   = 0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
		"\t\tbegin\n"
			"\t\t\tleftv <= 0;\n"
			"\t\t\tleftvv <= 0;\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// Second clock, pipeline = 1\n"
			"\t\t\tleftv <= { r_aux_2, r_sum_r, r_sum_i };\n"
"\n"
			"\t\t\t// Third clock, pipeline = 3\n"
			"\t\t\t//   As desired, each of these lines infers a DSP48\n"
			"\t\t\tleftvv <= leftv;\n"
		"\t\tend\n"
"\n");

	// Nominally, we should handle code for 1, 2, or 3 clocks per CE, with
	// one clock per CE meaning CE could be constant.  The code below
	// instead handles 1 or 3 clocks per CE, leaving the two clocks per
	// CE optimization(s) unfulfilled.

//	fprintf(fp,
//"\tend else if (CKPCI == 2'b01)\n\tbegin\n");

	///////////////////////////////////////////
	///
	///	One clock per CE, so CE, CE, CE, CE, CE is possible
	///
	fprintf(fp,
"\tgenerate if (CKPCE <= 2'b01)\n\tbegin : CKPCE_ONE\n");

	fprintf(fp,
	"\t\t// Coefficient multiply inputs\n"
	"\t\treg\tsigned	[(CWIDTH-1):0]	p1c_in, p2c_in;\n"
	"\t\t// Data multiply inputs\n"
	"\t\treg\tsigned	[(IWIDTH):0]	p1d_in, p2d_in;\n"
	"\t\t// Product 3, coefficient input\n"
	"\t\treg\tsigned	[(CWIDTH):0]	p3c_in;\n"
	"\t\t// Product 3, data input\n"
	"\t\treg\tsigned	[(IWIDTH+1):0]	p3d_in;\n"
"\n");
	fprintf(fp,
	"\t\treg\tsigned	[((IWIDTH+1)+(CWIDTH)-1):0]	rp_one, rp_two;\n"
	"\t\treg\tsigned	[((IWIDTH+2)+(CWIDTH+1)-1):0]	rp_three;\n"
"\n");

	fprintf(fp,
	"\t\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\tbegin\n"
		"\t\t\t// Second clock, pipeline = 1\n"
		"\t\t\tp1c_in <= ir_coef_r;\n"
		"\t\t\tp2c_in <= ir_coef_i;\n"
		"\t\t\tp1d_in <= r_dif_r;\n"
		"\t\t\tp2d_in <= r_dif_i;\n"
		"\t\t\tp3c_in <= ir_coef_i + ir_coef_r;\n"
		"\t\t\tp3d_in <= r_dif_r + r_dif_i;\n"
"\n"
"\n"
		"\t\t\t// Third clock, pipeline = 3\n"
		"\t\t\t//   As desired, each of these lines infers a DSP48\n"
		"\t\t\trp_one   <= p1c_in * p1d_in;\n"
		"\t\t\trp_two   <= p2c_in * p2d_in;\n"
		"\t\t\trp_three <= p3c_in * p3d_in;\n"
	"\t\tend\n"
"\n"
	"\t\tassign\tp_one   = rp_one;\n"
	"\t\tassign\tp_two   = rp_two;\n"
	"\t\tassign\tp_three = rp_three;\n"
"\n");

	///////////////////////////////////////////
	///
	///	Two clocks per CE, so CE, no-ce, CE, no-ce, etc
	///
	fprintf(fp,
	"\tend else if (CKPCE <= 2'b10)\n"
	"\tbegin : CKPCE_TWO\n"
		"\t\t// Coefficient multiply inputs\n"
		"\t\treg		[2*(CWIDTH)-1:0]	mpy_pipe_c;\n"
		"\t\t// Data multiply inputs\n"
		"\t\treg		[2*(IWIDTH+1)-1:0]	mpy_pipe_d;\n"
		"\t\twire	signed	[(CWIDTH-1):0]	mpy_pipe_vc;\n"
		"\t\twire	signed	[(IWIDTH):0]	mpy_pipe_vd;\n"
		"\t\t//\n"
		"\t\treg	signed	[(CWIDTH)-1:0]		mpy_cof_sum;\n"
		"\t\treg	signed	[(IWIDTH+1)-1:0]	mpy_dif_sum;\n"
"\n"
		"\t\tassign	mpy_pipe_vc =  mpy_pipe_c[2*(CWIDTH)-1:CWIDTH];\n"
		"\t\tassign	mpy_pipe_vd =  mpy_pipe_d[2*(IWIDTH+1)-1:IWIDTH+1];\n"
"\n"
		"\t\treg			mpy_pipe_v;\n"
		"\t\treg			ce_phase;\n"
"\n"
		"\t\treg	signed	[(CWIDTH+IWIDTH+1)-1:0]	mpy_pipe_out;\n"
		"\t\treg	signed [IWIDTH+CWIDTH+3-1:0]	longmpy;\n"
"\n"
"\n"
		"\t\tinitial	ce_phase = 1'b1;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_reset)\n"
			"\t\t\tce_phase <= 1'b1;\n"
		"\t\telse if (i_ce)\n"
			"\t\t\tce_phase <= 1'b0;\n"
		"\t\telse\n"
			"\t\t\tce_phase <= 1'b1;\n"
"\n"
		"\t\talways @(*)\n"
			"\t\t\tmpy_pipe_v = (i_ce)||(!ce_phase);\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (!ce_phase)\n"
		"\t\tbegin\n"
			"\t\t\t// Pre-clock\n"
			"\t\t\tmpy_pipe_c[2*CWIDTH-1:0] <=\n"
				"\t\t\t\t\t{ ir_coef_r, ir_coef_i };\n"
			"\t\t\tmpy_pipe_d[2*(IWIDTH+1)-1:0] <=\n"
				"\t\t\t\t\t{ r_dif_r, r_dif_i };\n"
"\n"
			"\t\t\tmpy_cof_sum  <= ir_coef_i + ir_coef_r;\n"
			"\t\t\tmpy_dif_sum <= r_dif_r + r_dif_i;\n"
"\n"
		"\t\tend else if (i_ce)\n"
		"\t\tbegin\n"
			"\t\t\t// First clock\n"
			"\t\t\tmpy_pipe_c[2*(CWIDTH)-1:0] <= {\n"
				"\t\t\t\tmpy_pipe_c[(CWIDTH)-1:0], {(CWIDTH){1'b0}} };\n"
			"\t\t\tmpy_pipe_d[2*(IWIDTH+1)-1:0] <= {\n"
				"\t\t\t\tmpy_pipe_d[(IWIDTH+1)-1:0], {(IWIDTH+1){1'b0}} };\n"
		"\t\tend\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) // First clock\n"
			"\t\t\tlongmpy <= mpy_cof_sum * mpy_dif_sum;\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (mpy_pipe_v)\n"
			"\t\t\tmpy_pipe_out <= mpy_pipe_vc * mpy_pipe_vd;\n"
"\n"
		"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]	rp_one;\n"
		"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]	rp2_one,\n"
				"\t\t\t\t\t\t\t\trp_two;\n"
		"\t\treg\tsigned\t[((IWIDTH+2)+(CWIDTH+1)-1):0]	rp_three;\n"
"\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (!ce_phase) // 1.5 clock\n"
			"\t\t\trp_one <= mpy_pipe_out;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) // two clocks\n"
			"\t\t\trp_two <= mpy_pipe_out;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce) // Second clock\n"
			"\t\t\trp_three<= longmpy;\n"
		"\t\talways @(posedge i_clk)\n"
		"\t\tif (i_ce)\n"
			"\t\t\trp2_one<= rp_one;\n"
"\n"
		"\t\tassign	p_one	= rp2_one;\n"
		"\t\tassign	p_two	= rp_two;\n"
		"\t\tassign	p_three	= rp_three;\n"
"\n");

	/////////////////////////
	///
	///	Three clock per CE, so CE, no-ce, no-ce*, CE
	///
	fprintf(fp,
"\tend else if (CKPCE <= 2'b11)\n\tbegin : CKPCE_THREE\n");

	fprintf(fp,
	"\t\t// Coefficient multiply inputs\n"
	"\t\treg\t\t[3*(CWIDTH+1)-1:0]\tmpy_pipe_c;\n"
	"\t\t// Data multiply inputs\n"
	"\t\treg\t\t[3*(IWIDTH+2)-1:0]\tmpy_pipe_d;\n"
	"\t\twire\tsigned	[(CWIDTH):0]	mpy_pipe_vc;\n"
	"\t\twire\tsigned	[(IWIDTH+1):0]	mpy_pipe_vd;\n"
	"\n"
	"\t\tassign\tmpy_pipe_vc =  mpy_pipe_c[3*(CWIDTH+1)-1:2*(CWIDTH+1)];\n"
	"\t\tassign\tmpy_pipe_vd =  mpy_pipe_d[3*(IWIDTH+2)-1:2*(IWIDTH+2)];\n"
	"\n"
	"\t\treg\t\t\tmpy_pipe_v;\n"
	"\t\treg\t\t[2:0]\tce_phase;\n"
	"\n"
	"\t\treg\tsigned	[  (CWIDTH+IWIDTH+3)-1:0]	mpy_pipe_out;\n"
"\n");
	fprintf(fp,
	"\t\tinitial\tce_phase = 3'b011;\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif (i_reset)\n"
		"\t\t\tce_phase <= 3'b011;\n"
	"\t\telse if (i_ce)\n"
		"\t\t\tce_phase <= 3'b000;\n"
	"\t\telse if (ce_phase != 3'b011)\n"
		"\t\t\tce_phase <= ce_phase + 1'b1;\n"
"\n"
	"\t\talways @(*)\n"
		"\t\t\tmpy_pipe_v = (i_ce)||(ce_phase < 3'b010);\n"
"\n");

	fprintf(fp,
	"\t\talways @(posedge i_clk)\n"
		"\t\t\tif (ce_phase == 3\'b000)\n"
		"\t\t\tbegin\n"
			"\t\t\t\t// Second clock\n"
			"\t\t\t\tmpy_pipe_c[3*(CWIDTH+1)-1:(CWIDTH+1)] <= {\n"
			"\t\t\t\t\tir_coef_r[CWIDTH-1], ir_coef_r,\n"
			"\t\t\t\t\tir_coef_i[CWIDTH-1], ir_coef_i };\n"
			"\t\t\t\tmpy_pipe_c[CWIDTH:0] <= ir_coef_i + ir_coef_r;\n"
			"\t\t\t\tmpy_pipe_d[3*(IWIDTH+2)-1:(IWIDTH+2)] <= {\n"
			"\t\t\t\t\tr_dif_r[IWIDTH], r_dif_r,\n"
			"\t\t\t\t\tr_dif_i[IWIDTH], r_dif_i };\n"
			"\t\t\t\tmpy_pipe_d[(IWIDTH+2)-1:0] <= r_dif_r + r_dif_i;\n"
"\n"
		"\t\t\tend else if (mpy_pipe_v)\n"
		"\t\t\tbegin\n"
			"\t\t\t\tmpy_pipe_c[3*(CWIDTH+1)-1:0] <= {\n"
			"\t\t\t\t\tmpy_pipe_c[2*(CWIDTH+1)-1:0], {(CWIDTH+1){1\'b0}} };\n"
			"\t\t\t\tmpy_pipe_d[3*(IWIDTH+2)-1:0] <= {\n"
			"\t\t\t\t\tmpy_pipe_d[2*(IWIDTH+2)-1:0], {(IWIDTH+2){1\'b0}} };\n"
		"\t\t\tend\n"
"\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\t\tif (mpy_pipe_v)\n"
			"\t\t\t\tmpy_pipe_out <= mpy_pipe_vc * mpy_pipe_vd;\n"
"\n");

	fprintf(fp,
	"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]\trp_one, rp_two;\n"
	"\t\treg\tsigned\t[((IWIDTH+1)+(CWIDTH)-1):0]\trp2_one, rp2_two;\n"
	"\t\treg\tsigned\t[((IWIDTH+2)+(CWIDTH+1)-1):0]\trp_three, rp2_three;\n"

"\n");

	fprintf(fp,
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(i_ce)\n"
		"\t\t\trp_one <= mpy_pipe_out[(CWIDTH+IWIDTH+3)-3:0];\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(ce_phase == 3'b000)\n"
		"\t\t\trp_two <= mpy_pipe_out[(CWIDTH+IWIDTH+3)-3:0];\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif(ce_phase == 3'b001)\n"
		"\t\t\trp_three <= mpy_pipe_out;\n"
	"\t\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\tbegin\n"
		"\t\t\trp2_one<= rp_one;\n"
		"\t\t\trp2_two<= rp_two;\n"
		"\t\t\trp2_three<= rp_three;\n"
	"\t\tend\n");
	fprintf(fp,

	"\t\tassign\tp_one\t= rp2_one;\n"
	"\t\tassign\tp_two\t= rp2_two;\n"
	"\t\tassign\tp_three\t= rp2_three;\n"
"\n");

	fprintf(fp,
"\tend endgenerate\n");

	fprintf(fp,
	"\twire\tsigned	[((IWIDTH+2)+(CWIDTH+1)-1):0]	w_one, w_two;\n"
	"\tassign\tw_one = { {(2){p_one[((IWIDTH+1)+(CWIDTH)-1)]}}, p_one };\n"
	"\tassign\tw_two = { {(2){p_two[((IWIDTH+1)+(CWIDTH)-1)]}}, p_two };\n"
"\n");

	fprintf(fp,
	"\t// These values are held in memory and delayed during the\n"
	"\t// multiply.  Here, we recover them.  During the multiply,\n"
	"\t// values were multiplied by 2^(CWIDTH-2)*exp{-j*2*pi*...},\n"
	"\t// therefore, the left_x values need to be right shifted by\n"
	"\t// CWIDTH-2 as well.  The additional bits come from a sign\n"
	"\t// extension.\n"
	"\twire\taux_s;\n"
	"\twire\tsigned\t[(IWIDTH+CWIDTH):0]	left_si, left_sr;\n"
	"\treg\t\t[(2*IWIDTH+2):0]	left_saved;\n"
	"\tassign\tleft_sr = { {2{left_saved[2*(IWIDTH+1)-1]}}, left_saved[(2*(IWIDTH+1)-1):(IWIDTH+1)], {(CWIDTH-2){1\'b0}} };\n"
	"\tassign\tleft_si = { {2{left_saved[(IWIDTH+1)-1]}}, left_saved[((IWIDTH+1)-1):0], {(CWIDTH-2){1\'b0}} };\n"
	"\tassign\taux_s = left_saved[2*IWIDTH+2];\n"
"\n"
"\n");

	fprintf(fp,
	"\tinitial left_saved = 0;\n"
	"\tinitial o_aux      = 1\'b0;\n");
	if (async_reset)
		fprintf(fp, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fp, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fp,
	"\t\tbegin\n"
		"\t\t\tleft_saved <= 0;\n"
		"\t\t\to_aux <= 1\'b0;\n"
	"\t\tend else if (i_ce)\n"
	"\t\tbegin\n"
		"\t\t\t// First clock, recover all values\n"
		"\t\t\tleft_saved <= leftvv;\n"
"\n"
		"\t\t\t// Second clock, round and latch for final clock\n"
		"\t\t\to_aux <= aux_s;\n"
	"\t\tend\n"
	"\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\tbegin\n"
		"\t\t\t// These values are IWIDTH+CWIDTH+3 bits wide\n"
		"\t\t\t// although they only need to be (IWIDTH+1)\n"
		"\t\t\t// + (CWIDTH) bits wide.  (We've got two\n"
		"\t\t\t// extra bits we need to get rid of.)\n"
		"\n"
		"\t\t\t// These two lines also infer DSP48\'s.\n"
		"\t\t\t// To keep from using extra DSP48 resources,\n"
		"\t\t\t// they are prevented from using DSP48\'s\n"
		"\t\t\t// by the (* use_dsp48 ... *) comment above.\n"
		"\t\t\tmpy_r <= w_one - w_two;\n"
		"\t\t\tmpy_i <= p_three - w_one - w_two;\n"
	"\t\tend\n"
	"\n");

	fprintf(fp,
	"\t// Round the results\n"
	"\t(* use_dsp48=\"no\" *)\n"
	"\treg	signed	[(CWIDTH+IWIDTH+3-1):0]	mpy_r, mpy_i;\n");
	fprintf(fp,
	"\twire\tsigned\t[(OWIDTH-1):0]\trnd_left_r, rnd_left_i, rnd_right_r, rnd_right_i;\n\n");
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+1,OWIDTH,SHIFT+2) do_rnd_left_r(i_clk, i_ce,\n"
	"\t\t\t\tleft_sr, rnd_left_r);\n\n",
		rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+1,OWIDTH,SHIFT+2) do_rnd_left_i(i_clk, i_ce,\n"
	"\t\t\t\tleft_si, rnd_left_i);\n\n",
		rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_right_r(i_clk, i_ce,\n"
	"\t\t\t\tmpy_r, rnd_right_r);\n\n", rnd_string);
	fprintf(fp,
	"\t%s #(CWIDTH+IWIDTH+3,OWIDTH,SHIFT+4) do_rnd_right_i(i_clk, i_ce,\n"
	"\t\t\t\tmpy_i, rnd_right_i);\n\n", rnd_string);


	fprintf(fp,
	"\t// As a final step, we pack our outputs into two packed two's\n"
	"\t// complement numbers per output word, so that each output word\n"
	"\t// has (2*OWIDTH) bits in it, with the top half being the real\n"
	"\t// portion and the bottom half being the imaginary portion.\n"
	"\tassign\to_left = { rnd_left_r, rnd_left_i };\n"
	"\tassign\to_right= { rnd_right_r,rnd_right_i};\n"
"\n"
"endmodule\n");

}

void	gen_coeffs(FILE *cmem, int stage, int cbits,
			int nwide, int offset, bool inv) {

	assert(nwide > 0);
	assert(stage / nwide >  1);
	assert(stage % nwide == 0);

	for(int i=0; i<stage/nwide; i++) {
		int k = nwide*i+offset;
		double	W = ((inv)?1:-1)*2.0*M_PI*k/(double)(nwide*stage);
		double	c, s;
		long long ic, is, vl;

		c = cos(W); s = sin(W);
		ic = (long long)llround((1ll<<(cbits-2)) * c);
		is = (long long)llround((1ll<<(cbits-2)) * s);
		vl = (ic & (~(-1ll << (cbits))));
		vl <<= (cbits);
		vl |= (is & (~(-1ll << (cbits))));
		fprintf(cmem, "%0*llx\n", ((cbits*2+3)/4), vl);
		//
	} fclose(cmem);
}

std::string	gen_coeff_fname(const char *coredir,
			int stage, int nwide, int offset, bool inv) {
	std::string	result;
	char	*memfile;

	assert((nwide == 1)||(nwide == 2));

	memfile = new char[strlen(coredir)+3+10+strlen(".hex")+64];
	if (nwide == 2) {
		if (coredir[0] == '\0') {
			sprintf(memfile, "%scmem_%c%d.hex",
				(inv)?"i":"", (offset==1)?'o':'e', stage*nwide);
		} else {
			sprintf(memfile, "%s/%scmem_%c%d.hex",
				coredir, (inv)?"i":"",
				(offset==1)?'o':'e', stage*nwide);
		}
	} else if (coredir[0] == '\0') // if (nwide == 1)
		sprintf(memfile, "%scmem_%d.hex",
			(inv)?"i":"", stage);
	else
		sprintf(memfile, "%s/%scmem_%d.hex",
			coredir, (inv)?"i":"", stage);

	result = std::string(memfile);
	delete[] memfile;
	return	result;
}

FILE	*gen_coeff_open(const char *fname) {
	FILE	*cmem;

	cmem = fopen(fname, "w");
	if (NULL == cmem) {
		fprintf(stderr, "Could not open/write \'%s\' with FFT coefficients.\n", fname);
		perror("Err from O/S:");
		exit(EXIT_FAILURE);
	}

	return cmem;
}

void	gen_coeff_file(const char *coredir, const char *fname, int stage, int cbits,
			int nwide, int offset, bool inv) {
	std::string	fstr;
	FILE	*cmem;

	fstr= gen_coeff_fname(coredir, stage, nwide, offset, inv);
	cmem = gen_coeff_open(fstr.c_str());
	gen_coeffs(cmem, stage,  cbits, nwide, offset, inv);
}

void	build_stage(const char *fname,
		int stage, int nwide, int offset,
		int nbits, bool inv, int xtra,
		const bool async_reset = false,
		const bool dbg=false) {
	FILE	*fstage = fopen(fname, "w");
	int	cbits = nbits + xtra;

	std::string	resetw("i_reset");
	if (async_reset)
		resetw = std::string("i_areset_n");

	if (((unsigned)cbits * 2u) >= sizeof(long long)*8) {
		fprintf(stderr, "ERROR: CMEM Coefficient precision requested overflows long long data type.\n");
		exit(-1);
	}

	if (fstage == NULL) {
		fprintf(stderr, "ERROR: Could not open %s for writing!\n", fname);
		perror("O/S Err was:");
		fprintf(stderr, "Attempting to continue, but this file will be missing.\n");
		return;
	}

	fprintf(fstage,
SLASHLINE
"//\n"
"// Filename:\t%sfftstage%s.v\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:\tThis file is (almost) a Verilog source file.  It is meant to\n"
"//		be used by a FFT core compiler to generate FFTs which may be\n"
"//	used as part of an FFT core.  Specifically, this file encapsulates\n"
"//	the options of an FFT-stage.  For any 2^N length FFT, there shall be\n"
"//	(N-1) of these stages.\n"
"//\n%s"
"//\n",
		(inv)?"i":"", (dbg)?"_dbg":"", prjname, creator);
	fprintf(fstage, "%s", cpyleft);
	fprintf(fstage, "//\n//\n`default_nettype\tnone\n//\n");
	fprintf(fstage, "module\t%sfftstage%s(i_clk, %s, i_ce, i_sync, i_data, o_data, o_sync%s);\n",
		(inv)?"i":"", (dbg)?"_dbg":"", resetw.c_str(),
		(dbg)?", o_dbg":"");
	// These parameter values are useless at this point--they are to be
	// replaced by the parameter values in the calling program.  Only
	// problem is, the CWIDTH needs to match exactly!
	fprintf(fstage, "\tparameter\tIWIDTH=%d,CWIDTH=%d,OWIDTH=%d;\n",
		nbits, cbits, nbits+1);
	fprintf(fstage,
"\t// Parameters specific to the core that should be changed when this\n"
"\t// core is built ... Note that the minimum LGSPAN (the base two log\n"
"\t// of the span, or the base two log of the current FFT size) is 3.\n"
"\t// Smaller spans (i.e. the span of 2) must use the dblstage module.\n"
"\tparameter\tLGWIDTH=11, LGSPAN=9, LGBDLY=5, BFLYSHIFT=0;\n"
"\tparameter\t[0:0]	OPT_HWMPY = 1\'b1;\n");
	fprintf(fstage,
"\t// Clocks per CE.  If your incoming data rate is less than 50%% of your\n"
"\t// clock speed, you can set CKPCE to 2\'b10, make sure there's at least\n"
"\t// one clock between cycles when i_ce is high, and then use two\n"
"\t// multiplies instead of three.  Setting CKPCE to 2\'b11, and insisting\n"
"\t// on at least two clocks with i_ce low between cycles with i_ce high,\n"
"\t// then the hardware optimized butterfly code will used one multiply\n"
"\t// instead of two.\n"
"\tparameter\t[1:0]	CKPCE = 2'b1;\n");

	fprintf(fstage,
"\t// The COEFFILE parameter contains the name of the file containing the\n"
"\t// FFT twiddle factors\n");
	if (nwide == 2) {
		fprintf(fstage, "\tparameter\tCOEFFILE=\"%scmem_%c%d.hex\";\n",
			(inv)?"i":"", (offset)?'o':'e', stage*2);
	} else
		fprintf(fstage, "\tparameter\tCOEFFILE=\"%scmem_%d.hex\";\n",
			(inv)?"i":"", stage);
	fprintf(fstage,
"\tinput					i_clk, %s, i_ce, i_sync;\n"
"\tinput		[(2*IWIDTH-1):0]	i_data;\n"
"\toutput	reg	[(2*OWIDTH-1):0]	o_data;\n"
"\toutput	reg				o_sync;\n"
"\n", resetw.c_str());
	if (dbg) { fprintf(fstage, "\toutput\twire\t[33:0]\t\t\to_dbg;\n"
		"\tassign\to_dbg = { ((o_sync)&&(i_ce)), i_ce, o_data[(2*OWIDTH-1):(2*OWIDTH-16)],\n"
			"\t\t\t\t\to_data[(OWIDTH-1):(OWIDTH-16)] };\n"
"\n");
	}
	fprintf(fstage,
"\treg	wait_for_sync;\n"
"\treg	[(2*IWIDTH-1):0]	ib_a, ib_b;\n"
"\treg	[(2*CWIDTH-1):0]	ib_c;\n"
"\treg	ib_sync;\n"
"\n"
"\treg	b_started;\n"
"\twire	ob_sync;\n"
"\twire	[(2*OWIDTH-1):0]\tob_a, ob_b;\n");
	fprintf(fstage,
"\n"
"\t// cmem is defined as an array of real and complex values,\n"
"\t// where the top CWIDTH bits are the real value and the bottom\n"
"\t// CWIDTH bits are the imaginary value.\n"
"\t//\n"
"\t// cmem[i] = { (2^(CWIDTH-2)) * cos(2*pi*i/(2^LGWIDTH)),\n"
"\t//		(2^(CWIDTH-2)) * sin(2*pi*i/(2^LGWIDTH)) };\n"
"\t//\n"
"\treg	[(2*CWIDTH-1):0]	cmem [0:((1<<LGSPAN)-1)];\n"
"\tinitial\t$readmemh(COEFFILE,cmem);\n\n");

	// gen_coeff_file(coredir, fname, stage, cbits, nwide, offset, inv);

	fprintf(fstage,
"\treg	[(LGWIDTH-2):0]		iaddr;\n"
"\treg	[(2*IWIDTH-1):0]	imem	[0:((1<<LGSPAN)-1)];\n"
"\n"
"\treg	[LGSPAN:0]		oB;\n"
"\treg	[(2*OWIDTH-1):0]	omem	[0:((1<<LGSPAN)-1)];\n"
"\n"
"\tinitial wait_for_sync = 1\'b1;\n"
"\tinitial iaddr = 0;\n");
	if (async_reset)
		fprintf(fstage, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fstage, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");

	fprintf(fstage,
	"\t\tbegin\n"
		"\t\t\twait_for_sync <= 1\'b1;\n"
		"\t\t\tiaddr <= 0;\n"
	"\t\tend\n"
	"\t\telse if ((i_ce)&&((!wait_for_sync)||(i_sync)))\n"
	"\t\tbegin\n"
		"\t\t\t//\n"
		"\t\t\t// First step: Record what we\'re not ready to use yet\n"
		"\t\t\t//\n"
		"\t\t\tiaddr <= iaddr + { {(LGWIDTH-2){1\'b0}}, 1\'b1 };\n"
		"\t\t\twait_for_sync <= 1\'b0;\n"
	"\t\tend\n"
"\talways @(posedge i_clk) // Need to make certain here that we don\'t read\n"
	"\t\tif ((i_ce)&&(!iaddr[LGSPAN])) // and write the same address on\n"
		"\t\t\timem[iaddr[(LGSPAN-1):0]] <= i_data; // the same clk\n"
	"\n");

	fprintf(fstage,
	"\t//\n"
	"\t// Now, we have all the inputs, so let\'s feed the butterfly\n"
	"\t//\n"
	"\tinitial ib_sync = 1\'b0;\n");
	if (async_reset)
		fprintf(fstage, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fstage, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fstage,
			"\t\t\tib_sync <= 1\'b0;\n"
		"\t\telse if ((i_ce)&&(iaddr[LGSPAN]))\n"
			"\t\t\tbegin\n"
				"\t\t\t\t// Set the sync to true on the very first\n"
				"\t\t\t\t// valid input in, and hence on the very\n"
				"\t\t\t\t// first valid data out per FFT.\n"
				"\t\t\t\tib_sync <= (iaddr==(1<<(LGSPAN)));\n"
			"\t\t\tend\n"
	"\talways\t@(posedge i_clk)\n"
		"\t\tif ((i_ce)&&(iaddr[LGSPAN]))\n"
		"\t\t\tbegin\n"
			"\t\t\t\t// One input from memory, ...\n"
			"\t\t\t\tib_a <= imem[iaddr[(LGSPAN-1):0]];\n"
			"\t\t\t\t// One input clocked in from the top\n"
			"\t\t\t\tib_b <= i_data;\n"
			"\t\t\t\t// and the coefficient or twiddle factor\n"
			"\t\t\t\tib_c <= cmem[iaddr[(LGSPAN-1):0]];\n"
		"\t\t\tend\n\n");

	fprintf(fstage,
"\tgenerate if (OPT_HWMPY)\n"
"\tbegin : HWBFLY\n"
"\t\thwbfly #(.IWIDTH(IWIDTH),.CWIDTH(CWIDTH),.OWIDTH(OWIDTH),\n"
			"\t\t\t\t.CKPCE(CKPCE), .SHIFT(BFLYSHIFT))\n"
		"\t\t\tbfly(i_clk, %s, i_ce, ib_c,\n"
			"\t\t\t\tib_a, ib_b, ib_sync, ob_a, ob_b, ob_sync);\n"
"\tend else begin : FWBFLY\n"
"\t\tbutterfly #(.IWIDTH(IWIDTH),.CWIDTH(CWIDTH),.OWIDTH(OWIDTH),\n"
		"\t\t\t\t.MPYDELAY(%d\'d%d),.LGDELAY(LGBDLY),\n"
		"\t\t\t\t.CKPCE(CKPCE),.SHIFT(BFLYSHIFT))\n"
	"\t\t\tbfly(i_clk, %s, i_ce, ib_c,\n"
		"\t\t\t\tib_a, ib_b, ib_sync, ob_a, ob_b, ob_sync);\n"
"\tend endgenerate\n\n",
			resetw.c_str(), lgdelay(nbits, xtra),
			bflydelay(nbits, xtra),
			resetw.c_str());

	fprintf(fstage,
	"\t//\n"
	"\t// Next step: recover the outputs from the butterfly\n"
	"\t//\n"
	"\tinitial oB        = 0;\n"
	"\tinitial o_sync    = 0;\n"
	"\tinitial b_started = 0;\n");
	if (async_reset)
		fprintf(fstage, "\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else
		fprintf(fstage, "\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	fprintf(fstage,
	"\t\tbegin\n"
		"\t\t\toB <= 0;\n"
		"\t\t\to_sync <= 0;\n"
		"\t\t\tb_started <= 0;\n"
	"\t\tend else if (i_ce)\n"
	"\t\tbegin\n"
	"\t\t\to_sync <= (!oB[LGSPAN])?ob_sync : 1\'b0;\n"
	"\t\t\tif (ob_sync||b_started)\n"
		"\t\t\t\toB <= oB + { {(LGSPAN){1\'b0}}, 1\'b1 };\n"
	"\t\t\tif ((ob_sync)&&(!oB[LGSPAN]))\n"
		"\t\t\t// A butterfly output is available\n"
			"\t\t\t\tb_started <= 1\'b1;\n"
	"\t\tend\n\n");
	fprintf(fstage,
	"\treg	[(LGSPAN-1):0]\t\tdly_addr;\n"
	"\treg	[(2*OWIDTH-1):0]\tdly_value;\n"
	"\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\tbegin\n"
	"\t\t\tdly_addr <= oB[(LGSPAN-1):0];\n"
	"\t\t\tdly_value <= ob_b;\n"
	"\t\tend\n"
	"\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
		"\t\t\tomem[dly_addr] <= dly_value;\n"
"\n");
	fprintf(fstage,
	"\talways @(posedge i_clk)\n"
	"\t\tif (i_ce)\n"
	"\t\t\to_data <= (!oB[LGSPAN])?ob_a : omem[oB[(LGSPAN-1):0]];\n"
"\n");
	fprintf(fstage, "endmodule\n");
}

void	usage(void) {
	fprintf(stderr,
"USAGE:\tfftgen [-f <size>] [-d dir] [-c cbits] [-n nbits] [-m mxbits] [-s]\n"
// "\tfftgen -i\n"
"\t-1\tBuild a normal FFT, running at one clock per complex sample, or\n"
"\t\t(for a real FFT) at one clock per two real input samples.\n"
"\t-c <cbits>\tCauses all internal complex coefficients to be\n"
"\t\tlonger than the corresponding data bits, to help avoid\n"
"\t\tcoefficient truncation errors.  The default is %d bits longer\n"
"\t\tthan the data bits.\n"
"\t-d <dir>\tPlaces all of the generated verilog files into <dir>.\n"
"\t\tThe default is a subdirectory of the current directory named %s.\n"
"\t-f <size>\tSets the size of the FFT as the number of complex\n"
"\t\tsamples input to the transform.  (No default value, this is\n"
"\t\ta required parameter.)\n"
"\t-i\tAn inverse FFT, meaning that the coefficients are\n"
"\t\tgiven by e^{ j 2 pi k/N n }.  The default is a forward FFT, with\n"
"\t\tcoefficients given by e^{ -j 2 pi k/N n }.\n"
"\t-k #\tSets # clocks per sample, used to minimize multiplies.  Also\n"
"\t\tsets one sample in per i_ce clock (opt -1)\n"
"\t-m <mxbits>\tSets the maximum bit width that the FFT should ever\n"
"\t\tproduce.  Internal values greater than this value will be\n"
"\t\ttruncated to this value.  (The default value grows the input\n"
"\t\tsize by one bit for every two FFT stages.)\n"
"\t-n <nbits>\tSets the bitwidth for values coming into the (i)FFT.\n"
"\t\tThe default is %d bits input for each component of the two\n"
"\t\tcomplex values into the FFT.\n"
"\t-p <nmpy>\tSets the number of hardware multiplies (DSPs) to use, versus\n"
"\t\tshift-add emulation.  The default is not to use any hardware\n"
"\t\tmultipliers.\n"
"\t-r\tBuild a real-FFT at four input points per sample, rather than a\n"
"\t\tcomplex FFT.  (Default is a Complex FFT.)\n"
"\t-s\tSkip the final bit reversal stage.  This is useful in\n"
"\t\talgorithms that need to apply a filter without needing to do\n"
"\t\tbin shifting, as these algorithms can, with this option, just\n"
"\t\tmultiply by a bit reversed correlation sequence and then\n"
"\t\tinverse FFT the (still bit reversed) result.  (You would need\n"
"\t\ta decimation in time inverse to do this, which this program does\n"
"\t\tnot yet provide.)\n"
"\t-S\tInclude the final bit reversal stage (default).\n"
"\t-x <xtrabits>\tUse this many extra bits internally, before any final\n"
"\t\trounding or truncation of the answer to the final number of bits.\n"
"\t\tThe default is to use %d extra bits internally.\n",
/*
"\t-0\tA forward FFT (default), meaning that the coefficients are\n"
"\t\tgiven by e^{-j 2 pi k/N n }.\n"
"\t-1\tAn inverse FFT, meaning that the coefficients are\n"
"\t\tgiven by e^{ j 2 pi k/N n }.\n",
*/
	DEF_XTRACBITS, DEF_COREDIR, DEF_NBITSIN, DEF_XTRAPBITS);
}

// Features still needed:
//	Interactivity.
int main(int argc, char **argv) {
	int	fftsize = -1, lgsize = -1;
	int	nbitsin = DEF_NBITSIN, xtracbits = DEF_XTRACBITS,
			nummpy=DEF_NMPY, nmpypstage=6, mpy_stages;
	int	nbitsout, maxbitsout = -1, xtrapbits=DEF_XTRAPBITS, ckpce = 0;
	const char *EMPTYSTR = "";
	bool	bitreverse = true, inverse=false,
		verbose_flag = false,
		single_clock = false,
		real_fft = false,
		async_reset = false;
	FILE	*vmain;
	std::string	coredir = DEF_COREDIR, cmdline = "", hdrname = "";
	ROUND_T	rounding = RND_CONVERGENT;
	// ROUND_T	rounding = RND_HALFUP;

	bool	dbg = false;
	int	dbgstage = 128;

	if (argc <= 1)
		usage();

	// Copy the original command line before we mess with it
	cmdline = argv[0];
	for(int argn=1; argn<argc; argn++) {
		cmdline += " ";
		cmdline += argv[argn];
	}

	{ int c;
	while((c = getopt(argc, argv, "12Aa:c:d:D:f:hik:m:n:p:rsSx:v")) != -1) {
		switch(c) {
		case '1':	single_clock = true;  break;
		case '2':	single_clock = false; break;
		case 'A':	async_reset  = true;  break;
		case 'a':	hdrname = strdup(optarg);	break;
		case 'c':	xtracbits = atoi(optarg);	break;
		case 'd':	coredir = std::string(optarg);	break;
		case 'D':	dbgstage = atoi(optarg);	break;
		case 'f':	fftsize = atoi(optarg);	
				{ int sln = strlen(optarg);
				if (!isdigit(optarg[sln-1])){
					switch(optarg[sln-1]) {
					case 'k': case 'K':
						fftsize <<= 10;
						break;
					case 'm': case 'M':
						fftsize <<= 20;
						break;
					case 'g': case 'G':
						fftsize <<= 30;
						break;
					default:
						printf("ERR: Unknown FFT size, %s!\n", optarg);
						exit(EXIT_FAILURE);
					}
				}} break;
		case 'h':	usage(); exit(EXIT_SUCCESS);	break;
		case 'i':	inverse = true;			break;
		case 'k':	ckpce = atoi(optarg);
				single_clock = true;
				break;
		case 'm':	maxbitsout = atoi(optarg);	break;
		case 'n':	nbitsin = atoi(optarg);		break;
		case 'p':	nummpy = atoi(optarg);		break;
		case 'r':	real_fft = true;		break;
		case 'S':	bitreverse = true;		break;
		case 's':	bitreverse = false;		break;
		case 'x':	xtrapbits = atoi(optarg);	break;
		case 'v':	verbose_flag = true;		break;
		// case 'z':	variable_size = true;		break;
		default:
			printf("Unknown argument, -%c\n", c);
			usage();
			exit(EXIT_FAILURE);
		}
	}}

	if (verbose_flag) {
		if (inverse)
			printf("Building a %d point inverse FFT module, with %s outputs\n",
				fftsize,
				(real_fft)?"real ":"complex");
		else
			printf("Building a %d point %sforward FFT module\n",
				fftsize,
				(real_fft)?"real ":"");
		if (!single_clock)
			printf("  that accepts two inputs per clock\n");
		if (async_reset)
			printf("  using a negative logic ASYNC reset\n");

		printf("The core will be placed into the %s/ directory\n", coredir.c_str());

		if (hdrname[0])
			printf("A C header file, %s, will be written capturing these\n"
				"options for a Verilator testbench\n", 
					hdrname.c_str());
		// nummpy
		// xtrapbits
	}

	if (real_fft) {
		printf("The real FFT option is not implemented yet, but still on\nmy to do list.  Please try again later.\n");
		exit(EXIT_FAILURE);
	}

	if (single_clock) {
		printf(
"WARNING: The single clock FFT option is not fully tested yet, but instead\n"
"represents a work in progress.  Feel free to use it at your own risk.\n");
	} if (ckpce >= 1) {
		printf(
"WARNING: The non-hw optimized butterfly that uses multiple clocks per CE has\n"
" not yet been fully tested, but rather represent a work in progress.  Feel\n"
" free to use the ckpce option(s) at your own risk.\n");
	} else 
		ckpce = 1;
	if (!bitreverse) {
		printf("WARNING: While I can skip the bit reverse stage, the code to do\n");
		printf("an inverse FFT on a bit--reversed input has not yet been\n");
		printf("built.\n");
	}

	if ((lgsize < 0)&&(fftsize > 1)) {
		for(lgsize=1; (1<<lgsize) < fftsize; lgsize++)
			;
	}

	if ((fftsize <= 0)||(nbitsin < 1)||(nbitsin>48)) {
		printf("INVALID PARAMETERS!!!!\n");
		exit(EXIT_FAILURE);
	}


	if (nextlg(fftsize) != fftsize) {
		fprintf(stderr, "ERR: FFTSize (%d) *must* be a power of two\n",
				fftsize);
		exit(EXIT_FAILURE);
	} else if (fftsize < 2) {
		fprintf(stderr, "ERR: Minimum FFTSize is 2, not %d\n",
				fftsize);
		if (fftsize == 1) {
			fprintf(stderr, "You do realize that a 1 point FFT makes very little sense\n");
			fprintf(stderr, "in an FFT operation that handles two samples per clock?\n");
			fprintf(stderr, "If you really need to do an FFT of this size, the output\n");
			fprintf(stderr, "can be connected straight to the input.\n");
		} else {
			fprintf(stderr, "Indeed, a size of %d doesn\'t make much sense to me at all.\n", fftsize);
			fprintf(stderr, "Is such an operation even defined?\n");
		}
		exit(EXIT_FAILURE);
	}

	// Calculate how many output bits we'll have, and what the log
	// based two size of our FFT is.
	{
		int	tmp_size = fftsize;

		// The first stage always accumulates one bit, regardless
		// of whether you need to or not.
		nbitsout = nbitsin + 1;
		tmp_size >>= 1;

		while(tmp_size > 4) {
			nbitsout += 1;
			tmp_size >>= 2;
		}

		if (tmp_size > 1)
			nbitsout ++;

		if (fftsize <= 2)
			bitreverse = false;
	} if ((maxbitsout > 0)&&(nbitsout > maxbitsout))
		nbitsout = maxbitsout;

	if (verbose_flag) {
		printf("Output samples will be %d bits wide\n", nbitsout);
		printf("This %sFFT will take %d-bit samples in, and produce %d samples out\n", (inverse)?"i":"", nbitsin, nbitsout);
		if (maxbitsout > 0)
			printf("  Internally, it will allow items to accumulate to %d bits\n", maxbitsout);
		printf("  Twiddle-factors of %d bits will be used\n",
			nbitsin+xtracbits);
		if (!bitreverse)
		printf("  The output will be left in bit-reversed order\n");
	}

	// Figure out how many multiply stages to use, and how many to skip
	if (!single_clock) {
		nmpypstage = 6;
	} else if (ckpce <= 1) {
		nmpypstage = 3;
	} else if (ckpce == 2) {
		nmpypstage = 2;
	} else
		nmpypstage = 1;

	mpy_stages = nummpy / nmpypstage;
	if (mpy_stages > lgval(fftsize)-2)
		mpy_stages = lgval(fftsize)-2;

	{
		struct stat	sbuf;
		if (lstat(coredir.c_str(), &sbuf)==0) {
			if (!S_ISDIR(sbuf.st_mode)) {
				fprintf(stderr, "\'%s\' already exists, and is not a directory!\n", coredir.c_str());
				fprintf(stderr, "I will stop now, lest I overwrite something you care about.\n");
				fprintf(stderr, "To try again, please remove this file.\n");
				exit(EXIT_FAILURE);
			}
		} else
			mkdir(coredir.c_str(), 0755);
		if (access(coredir.c_str(), X_OK|W_OK) != 0) {
			fprintf(stderr, "I have no access to the directory \'%s\'.\n", coredir.c_str());
			exit(EXIT_FAILURE);
		}
	}

	if (hdrname.length() > 0) {
		FILE	*hdr = fopen(hdrname.c_str(), "w");
		if (hdr == NULL) {
			fprintf(stderr, "ERROR: Cannot open %s to create header file\n", hdrname.c_str());
			perror("O/S Err:");
			exit(EXIT_FAILURE);
		}

		fprintf(hdr,
SLASHLINE
"//\n"
"// Filename:\t%s\n"
"//\n"
"// Project:\t%s\n"
"//\n"
"// Purpose:	This simple header file captures the internal constants\n"
"//		within the FFT that were used to build it, for the purpose\n"
"//	of making C++ integration (and test bench testing) simpler.  That is,\n"
"//	should the FFT change size, this will note that size change and thus\n"
"//	any test bench or other C++ program dependent upon either the size of\n"
"//	the FFT, the number of bits in or out of it, etc., can pick up the\n"
"//	changes in the defines found within this file.\n"
"//\n",
		hdrname.c_str(), prjname);
		fprintf(hdr, "%s", creator);
		fprintf(hdr, "//\n");
		fprintf(hdr, "%s", cpyleft);
		fprintf(hdr, "//\n"
		"//\n"
		"#ifndef %sFFTHDR_H\n"
		"#define %sFFTHDR_H\n"
		"\n"
		"#define\t%sFFT_IWIDTH\t%d\n"
		"#define\t%sFFT_OWIDTH\t%d\n"
		"#define\t%sFFT_LGWIDTH\t%d\n"
		"#define\t%sFFT_SIZE\t(1<<%sFFT_LGWIDTH)\n\n",
			(inverse)?"I":"", (inverse)?"I":"",
			(inverse)?"I":"", nbitsin,
			(inverse)?"I":"", nbitsout,
			(inverse)?"I":"", lgsize,
			(inverse)?"I":"", (inverse)?"I":"");
		if (ckpce > 0)
			fprintf(hdr, "#define\t%sFFT_CKPCE\t%d\t// Clocks per CE\n",
				(inverse)?"I":"", ckpce);
		else
			fprintf(hdr, "// Two samples per i_ce\n");
		if (!bitreverse)
			fprintf(hdr, "#define\t%sFFT_SKIPS_BIT_REVERSE\n",
				(inverse)?"I":"");
		if (real_fft)
			fprintf(hdr, "#define\tRL%sFFT\n\n", (inverse)?"I":"");
		if (!single_clock)
			fprintf(hdr, "#define\tDBLCLK%sFFT\n\n", (inverse)?"I":"");
		else
			fprintf(hdr, "// #define\tDBLCLK%sFFT // this FFT takes one input sample per clock\n\n", (inverse)?"I":"");
		if (USE_OLD_MULTIPLY)
			fprintf(hdr, "#define\tUSE_OLD_MULTIPLY\n\n");

		fprintf(hdr, "// Parameters for testing the longbimpy\n");
		fprintf(hdr, "#define\tTST_LONGBIMPY_AW\t%d\n", TST_LONGBIMPY_AW);
#ifdef	TST_LONGBIMPY_BW
		fprintf(hdr, "#define\tTST_LONGBIMPY_BW\t%d\n\n", TST_LONGBIMPY_BW);
#else
		fprintf(hdr, "#define\tTST_LONGBIMPY_BW\tTST_LONGBIMPY_AW\n\n");
#endif

		fprintf(hdr, "// Parameters for testing the shift add multiply\n");
		fprintf(hdr, "#define\tTST_SHIFTADDMPY_AW\t%d\n", TST_SHIFTADDMPY_AW);
#ifdef	TST_SHIFTADDMPY_BW
		fprintf(hdr, "#define\tTST_SHIFTADDMPY_BW\t%d\n\n", TST_SHIFTADDMPY_BW);
#else
		fprintf(hdr, "#define\tTST_SHIFTADDMPY_BW\tTST_SHIFTADDMPY_AW\n\n");
#endif

#define	TST_SHIFTADDMPY_AW	16
#define	TST_SHIFTADDMPY_BW	20	// Leave undefined to match AW
		fprintf(hdr, "// Parameters for testing the butterfly\n");
		fprintf(hdr, "#define\tTST_BUTTERFLY_IWIDTH\t%d\n", TST_BUTTERFLY_IWIDTH);
		fprintf(hdr, "#define\tTST_BUTTERFLY_CWIDTH\t%d\n", TST_BUTTERFLY_CWIDTH);
		fprintf(hdr, "#define\tTST_BUTTERFLY_OWIDTH\t%d\n", TST_BUTTERFLY_OWIDTH);
		fprintf(hdr, "#define\tTST_BUTTERFLY_MPYDELAY\t%d\n\n",
				bflydelay(TST_BUTTERFLY_IWIDTH,
					TST_BUTTERFLY_CWIDTH-TST_BUTTERFLY_IWIDTH));

		fprintf(hdr, "// Parameters for testing the quarter stage\n");
		fprintf(hdr, "#define\tTST_QTRSTAGE_IWIDTH\t%d\n", TST_QTRSTAGE_IWIDTH);
		fprintf(hdr, "#define\tTST_QTRSTAGE_LGWIDTH\t%d\n\n", TST_QTRSTAGE_LGWIDTH);

		fprintf(hdr, "// Parameters for testing the double stage\n");
		fprintf(hdr, "#define\tTST_DBLSTAGE_IWIDTH\t%d\n", TST_DBLSTAGE_IWIDTH);
		fprintf(hdr, "#define\tTST_DBLSTAGE_SHIFT\t%d\n\n", TST_DBLSTAGE_SHIFT);

		fprintf(hdr, "// Parameters for testing the bit reversal stage\n");
		fprintf(hdr, "#define\tTST_DBLREVERSE_LGSIZE\t%d\n\n", TST_DBLREVERSE_LGSIZE);
		fprintf(hdr, "\n" "#endif\n\n");
		fclose(hdr);
	}

	{
		std::string	fname_string;

		fname_string = coredir;
		fname_string += "/";
		if (inverse) fname_string += "i";
		if (!single_clock)
			fname_string += "dbl";
		fname_string += "fftmain.v";

		vmain = fopen(fname_string.c_str(), "w");
		if (NULL == vmain) {
			fprintf(stderr, "Could not open \'%s\' for writing\n", fname_string.c_str());
			perror("Err from O/S:");
			exit(EXIT_FAILURE);
		}

		if (verbose_flag)
			printf("Opened %s\n", fname_string.c_str());
	}

	fprintf(vmain,
SLASHLINE
"//\n"
"// Filename:\t%s%sfftmain.v\n"
"//\n"
"// Project:	%s\n"
"//\n"
"// Purpose:	This is the main module in the General Purpose FPGA FFT\n"
"//		implementation.  As such, all other modules are subordinate\n"
"//	to this one.  This module accomplish a fixed size Complex FFT on\n"
"//	%d data points.\n",
		(inverse)?"i":"", (single_clock)?"":"dbl",prjname, fftsize);
	if (single_clock) {
	fprintf(vmain,
"//	The FFT is fully pipelined, and accepts as inputs one complex two\'s\n"
"//	complement sample per clock.\n");
	} else {
	fprintf(vmain,
"//	The FFT is fully pipelined, and accepts as inputs two complex two\'s\n"
"//	complement samples per clock.\n");
	}

	fprintf(vmain,
"//\n"
"// Parameters:\n"
"//	i_clk\tThe clock.  All operations are synchronous with this clock.\n"
"//	i_%sreset%s\tSynchronous reset, active high.  Setting this line will\n"
"//	\t\tforce the reset of all of the internals to this routine.\n"
"//	\t\tFurther, following a reset, the o_sync line will go\n"
"//	\t\thigh the same time the first output sample is valid.\n",
		(async_reset)?"a":"", (async_reset)?"_n":"");
	if (single_clock) {
		fprintf(vmain,
"//	i_ce\tA clock enable line.  If this line is set, this module\n"
"//	\t\twill accept one complex input value, and produce\n"
"//	\t\tone (possibly empty) complex output value.\n"
"//	i_sample\tThe complex input sample.  This value is split\n"
"//	\t\tinto two two\'s complement numbers, %d bits each, with\n"
"//	\t\tthe real portion in the high order bits, and the\n"
"//	\t\timaginary portion taking the bottom %d bits.\n"
"//	o_result\tThe output result, of the same format as i_sample,\n"
"//	\t\tonly having %d bits for each of the real and imaginary\n"
"//	\t\tcomponents, leading to %d bits total.\n"
"//	o_sync\tA one bit output indicating the first sample of the FFT frame.\n"
"//	\t\tIt also indicates the first valid sample out of the FFT\n"
"//	\t\ton the first frame.\n", nbitsin, nbitsin, nbitsout, nbitsout*2);
	} else {
		fprintf(vmain,
"//	i_ce\tA clock enable line.  If this line is set, this module\n"
"//	\t\twill accept two complex values as inputs, and produce\n"
"//	\t\ttwo (possibly empty) complex values as outputs.\n"
"//	i_left\tThe first of two complex input samples.  This value is split\n"
"//	\t\tinto two two\'s complement numbers, %d bits each, with\n"
"//	\t\tthe real portion in the high order bits, and the\n"
"//	\t\timaginary portion taking the bottom %d bits.\n"
"//	i_right\tThis is the same thing as i_left, only this is the second of\n"
"//	\t\ttwo such samples.  Hence, i_left would contain input\n"
"//	\t\tsample zero, i_right would contain sample one.  On the\n"
"//	\t\tnext clock i_left would contain input sample two,\n"
"//	\t\ti_right number three and so forth.\n"
"//	o_left\tThe first of two output samples, of the same format as i_left,\n"
"//	\t\tonly having %d bits for each of the real and imaginary\n"
"//	\t\tcomponents, leading to %d bits total.\n"
"//	o_right\tThe second of two output samples produced each clock.  This has\n"
"//	\t\tthe same format as o_left.\n"
"//	o_sync\tA one bit output indicating the first valid sample produced by\n"
"//	\t\tthis FFT following a reset.  Ever after, this will\n"
"//	\t\tindicate the first sample of an FFT frame.\n",
	nbitsin, nbitsin, nbitsout, nbitsout*2);
	}

	fprintf(vmain,
"//\n"
"// Arguments:\tThis file was computer generated using the following command\n"
"//\t\tline:\n"
"//\n");
	fprintf(vmain, "//\t\t%% %s\n", cmdline.c_str());
	fprintf(vmain, "//\n");
	fprintf(vmain, "%s", creator);
	fprintf(vmain, "//\n");
	fprintf(vmain, "%s", cpyleft);
	fprintf(vmain, "//\n//\n`default_nettype\tnone\n//\n");


	std::string	resetw("i_reset");
	if (async_reset)
		resetw = "i_areset_n";

	fprintf(vmain, "//\n");
	fprintf(vmain, "//\n");
	fprintf(vmain, "module %s%sfftmain(i_clk, %s, i_ce,\n",
		(inverse)?"i":"", (single_clock)?"":"dbl", resetw.c_str());
	if (single_clock) {
		fprintf(vmain, "\t\ti_sample, o_result, o_sync%s);\n",
			(dbg)?", o_dbg":"");
	} else {
		fprintf(vmain, "\t\ti_left, i_right,\n");
		fprintf(vmain, "\t\to_left, o_right, o_sync%s);\n",
			(dbg)?", o_dbg":"");
	}
	fprintf(vmain, "\tparameter\tIWIDTH=%d, OWIDTH=%d, LGWIDTH=%d;\n\t//\n", nbitsin, nbitsout, lgsize);
	assert(lgsize > 0);
	fprintf(vmain, "\tinput\t\t\t\t\ti_clk, %s, i_ce;\n\t//\n",
		resetw.c_str());
	if (single_clock) {
	fprintf(vmain, "\tinput\t\t[(2*IWIDTH-1):0]\ti_sample;\n");
	fprintf(vmain, "\toutput\treg\t[(2*OWIDTH-1):0]\to_result;\n");
	} else {
	fprintf(vmain, "\tinput\t\t[(2*IWIDTH-1):0]\ti_left, i_right;\n");
	fprintf(vmain, "\toutput\treg\t[(2*OWIDTH-1):0]\to_left, o_right;\n");
	}
	fprintf(vmain, "\toutput\treg\t\t\t\to_sync;\n");
	if (dbg)
		fprintf(vmain, "\toutput\twire\t[33:0]\t\to_dbg;\n");
	fprintf(vmain, "\n\n");

	fprintf(vmain, "\t// Outputs of the FFT, ready for bit reversal.\n");
	if (single_clock)
		fprintf(vmain, "\twire\t[(2*OWIDTH-1):0]\tbr_sample;\n");
	else
		fprintf(vmain, "\twire\t[(2*OWIDTH-1):0]\tbr_left, br_right;\n");
	fprintf(vmain, "\n\n");

	int	tmp_size = fftsize, lgtmp = lgsize;
	if (fftsize == 2) {
		if (bitreverse) {
			fprintf(vmain, "\treg\tbr_start;\n");
			fprintf(vmain, "\tinitial br_start = 1\'b0;\n");
			if (async_reset) {
				fprintf(vmain, "\talways @(posedge i_clk, negedge i_arese_n)\n");
				fprintf(vmain, "\t\tif (!i_areset_n)\n");
			} else {
				fprintf(vmain, "\talways @(posedge i_clk)\n");
				fprintf(vmain, "\t\tif (i_reset)\n");
			}
			fprintf(vmain, "\t\t\tbr_start <= 1\'b0;\n");
			fprintf(vmain, "\t\telse if (i_ce)\n");
			fprintf(vmain, "\t\t\tbr_start <= 1\'b1;\n");
		}
		fprintf(vmain, "\n\n");
		fprintf(vmain, "\tdblstage\t#(IWIDTH)\tstage_2(i_clk, %s, i_ce,\n", resetw.c_str());
		fprintf(vmain, "\t\t\t(%s%s), i_left, i_right, br_left, br_right);\n",
			(async_reset)?"":"!", resetw.c_str());
		fprintf(vmain, "\n\n");
	} else {
		int	nbits = nbitsin, dropbit=0;
		int	obits = nbits+1+xtrapbits;
		std::string	cmem;
		FILE	*cmemfp;

		if ((maxbitsout > 0)&&(obits > maxbitsout))
			obits = maxbitsout;

		// Always do a first stage
		{
			bool	mpystage;

			// Last two stages are always non-multiply stages
			// since the multiplies can be done by adds
			mpystage = ((lgtmp-2) <= mpy_stages);

			if (mpystage)
				fprintf(vmain, "\t// A hardware optimized FFT stage\n");
			fprintf(vmain, "\n\n");
			fprintf(vmain, "\twire\t\tw_s%d;\n", fftsize);
			if (single_clock) {
				fprintf(vmain, "\twire\t[%d:0]\tw_d%d;\n", 2*(obits+xtrapbits)-1, fftsize);
				cmem = gen_coeff_fname(EMPTYSTR, fftsize, 1, 0, inverse);
				cmemfp = gen_coeff_open(cmem.c_str());
				gen_coeffs(cmemfp, fftsize,  nbitsin+xtracbits, 1, 0, inverse);
				fprintf(vmain, "\t%sfftstage%s\t#(IWIDTH,IWIDTH+%d,%d,%d,%d,%d,0,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_e%d(i_clk, %s, i_ce,\n",
					(inverse)?"i":"",
					((dbg)&&(dbgstage == fftsize))?"_dbg":"",
					xtracbits, obits+xtrapbits,
					lgsize, lgtmp-2, lgdelay(nbits,xtracbits),
					(mpystage)?1:0, ckpce, cmem.c_str(),
					fftsize, resetw.c_str());
				fprintf(vmain, "\t\t\t(%s%s), i_sample, w_d%d, w_s%d%s);\n",
					(async_reset)?"":"!", resetw.c_str(),
					fftsize, fftsize,
					((dbg)&&(dbgstage == fftsize))
						? ", o_dbg":"");
			} else {
				fprintf(vmain, "\t// verilator lint_off UNUSED\n\twire\t\tw_os%d;\n\t// verilator lint_on  UNUSED\n", fftsize);
				fprintf(vmain, "\twire\t[%d:0]\tw_e%d, w_o%d;\n", 2*(obits+xtrapbits)-1, fftsize, fftsize);
				cmem = gen_coeff_fname(EMPTYSTR, fftsize, 2, 0, inverse);
				cmemfp = gen_coeff_open(cmem.c_str());
				gen_coeffs(cmemfp, fftsize,  nbitsin+xtracbits, 2, 0, inverse);
				fprintf(vmain, "\t%sfftstage%s\t#(IWIDTH,IWIDTH+%d,%d,%d,%d,%d,0,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_e%d(i_clk, %s, i_ce,\n",
					(inverse)?"i":"",
					((dbg)&&(dbgstage == fftsize))?"_dbg":"",
					xtracbits, obits+xtrapbits,
					lgsize, lgtmp-2, lgdelay(nbits,xtracbits),
					(mpystage)?1:0, ckpce, cmem.c_str(),
					fftsize, resetw.c_str());
				fprintf(vmain, "\t\t\t(%s%s), i_left, w_e%d, w_s%d%s);\n",
					(async_reset)?"":"!", resetw.c_str(),
					fftsize, fftsize,
					((dbg)&&(dbgstage == fftsize))?", o_dbg":"");
				cmem = gen_coeff_fname(EMPTYSTR, fftsize, 2, 1, inverse);
				cmemfp = gen_coeff_open(cmem.c_str());
				gen_coeffs(cmemfp, fftsize,  nbitsin+xtracbits, 2, 1, inverse);
				fprintf(vmain, "\t%sfftstage\t#(IWIDTH,IWIDTH+%d,%d,%d,%d,%d,0,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_o%d(i_clk, %s, i_ce,\n",
					(inverse)?"i":"",
					xtracbits, obits+xtrapbits,
					lgsize, lgtmp-2, lgdelay(nbits,xtracbits),
					(mpystage)?1:0, ckpce, cmem.c_str(),
					fftsize, resetw.c_str());
				fprintf(vmain, "\t\t\t(%s%s), i_right, w_o%d, w_os%d);\n",
					(async_reset)?"":"!",resetw.c_str(),
					fftsize, fftsize);
			}
			fprintf(vmain, "\n\n");


			std::string	fname;

			fname = coredir + "/";
			if (inverse)
				fname += "i";
			fname += "fftstage";
			if ((dbg)&&(dbgstage == fftsize))
				fname += "_dbg";
			fname += ".v";
			if ((dbg)&&(dbgstage == fftsize)) {
				if (single_clock)
					build_stage(fname.c_str(), fftsize, 1, 0, nbits, inverse, xtracbits, async_reset, false);
				else
					build_stage(fname.c_str(), fftsize/2, 2, 1, nbits, inverse, xtracbits, async_reset, false);
			} else if (single_clock) {
				build_stage(fname.c_str(), fftsize, 1, 0, nbits, inverse, xtracbits, async_reset, false);
			} else {
				build_stage(fname.c_str(), fftsize/2, 2, 0, nbits, inverse, xtracbits, async_reset, false);
				build_stage(fname.c_str(), fftsize/2, 2, 1, nbits, inverse, xtracbits, async_reset, false);
			}
		}

		nbits = obits;	// New number of input bits
		tmp_size >>= 1; lgtmp--;
		dropbit = 0;
		fprintf(vmain, "\n\n");
		while(tmp_size >= 8) {
			obits = nbits+((dropbit)?0:1);

			if ((maxbitsout > 0)&&(obits > maxbitsout))
				obits = maxbitsout;

			{
				bool		mpystage;

				mpystage = ((lgtmp-2) <= mpy_stages);

				if (mpystage)
					fprintf(vmain, "\t// A hardware optimized FFT stage\n");
				fprintf(vmain, "\twire\t\tw_s%d;\n",
					tmp_size);
				if (single_clock) {
					fprintf(vmain,"\twire\t[%d:0]\tw_d%d;\n",
						2*(obits+xtrapbits)-1,
						tmp_size);
					cmem = gen_coeff_fname(EMPTYSTR, tmp_size, 1, 0, inverse);
					cmemfp = gen_coeff_open(cmem.c_str());
					gen_coeffs(cmemfp, tmp_size,  nbitsin+xtracbits, 1, 0, inverse);
					fprintf(vmain, "\t%sfftstage%s\t#(%d,%d,%d,%d,%d,%d,%d,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_%d(i_clk, %s, i_ce,\n",
						(inverse)?"i":"",
						((dbg)&&(dbgstage==tmp_size))?"_dbg":"",
						nbits+xtrapbits,
						nbits+xtracbits+xtrapbits,
						obits+xtrapbits,
						lgsize, lgtmp-2,
						lgdelay(nbits+xtrapbits,xtracbits),
						(dropbit)?0:0,
						(mpystage)?1:0, ckpce,
						cmem.c_str(), tmp_size,
						resetw.c_str());
					fprintf(vmain, "\t\t\tw_s%d, w_d%d, w_d%d, w_s%d%s);\n",
						tmp_size<<1, tmp_size<<1,
						tmp_size, tmp_size,
						((dbg)&&(dbgstage == tmp_size))
							?", o_dbg":"");
				} else {
					fprintf(vmain, "\t// verilator lint_off UNUSED\n\twire\t\tw_os%d;\n\t// verilator lint_on  UNUSED\n",
						tmp_size);
					fprintf(vmain,"\twire\t[%d:0]\tw_e%d, w_o%d;\n",
						2*(obits+xtrapbits)-1,
						tmp_size, tmp_size);
					cmem = gen_coeff_fname(EMPTYSTR, tmp_size, 2, 0, inverse);
					cmemfp = gen_coeff_open(cmem.c_str());
					gen_coeffs(cmemfp, tmp_size,  nbitsin+xtracbits, 2, 0, inverse);
					fprintf(vmain, "\t%sfftstage%s\t#(%d,%d,%d,%d,%d,%d,%d,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_e%d(i_clk, %s, i_ce,\n",
						(inverse)?"i":"",
						((dbg)&&(dbgstage==tmp_size))?"_dbg":"",
						nbits+xtrapbits,
						nbits+xtracbits+xtrapbits,
						obits+xtrapbits,
						lgsize, lgtmp-2,
						lgdelay(nbits+xtrapbits,xtracbits),
						(dropbit)?0:0,
						(mpystage)?1:0, ckpce,
						cmem.c_str(), tmp_size,
						resetw.c_str());
					fprintf(vmain, "\t\t\tw_s%d, w_e%d, w_e%d, w_s%d%s);\n",
						tmp_size<<1, tmp_size<<1,
						tmp_size, tmp_size,
						((dbg)&&(dbgstage == tmp_size))
							?", o_dbg":"");
					cmem = gen_coeff_fname(EMPTYSTR, tmp_size, 2, 1, inverse);
					cmemfp = gen_coeff_open(cmem.c_str());
					gen_coeffs(cmemfp, tmp_size,  nbitsin+xtracbits, 2, 1, inverse);
					fprintf(vmain, "\t%sfftstage\t#(%d,%d,%d,%d,%d,%d,%d,\n\t\t\t1\'b%d, %d, \"%s\")\n\t\tstage_o%d(i_clk, %s, i_ce,\n",
						(inverse)?"i":"",
						nbits+xtrapbits,
						nbits+xtracbits+xtrapbits,
						obits+xtrapbits,
						lgsize, lgtmp-2,
						lgdelay(nbits+xtrapbits,xtracbits),
						(dropbit)?0:0, (mpystage)?1:0,
						ckpce, cmem.c_str(), tmp_size,
						resetw.c_str());
					fprintf(vmain, "\t\t\tw_s%d, w_o%d, w_o%d, w_os%d);\n",
						tmp_size<<1, tmp_size<<1,
						tmp_size, tmp_size);
				}
				fprintf(vmain, "\n\n");
			}


			dropbit ^= 1;
			nbits = obits;
			tmp_size >>= 1; lgtmp--;
		}

		if (tmp_size == 4) {
			obits = nbits+((dropbit)?0:1);

			if ((maxbitsout > 0)&&(obits > maxbitsout))
				obits = maxbitsout;

			fprintf(vmain, "\twire\t\tw_s4;\n");
			if (single_clock) {
				fprintf(vmain, "\twire\t[%d:0]\tw_d4;\n",
					2*(obits+xtrapbits)-1);
				fprintf(vmain, "\tqtrstage%s\t#(%d,%d,%d,0,%d,%d)\tstage_4(i_clk, %s, i_ce,\n",
					((dbg)&&(dbgstage==4))?"_dbg":"",
					nbits+xtrapbits, obits+xtrapbits, lgsize,
					(inverse)?1:0, (dropbit)?0:0,
					resetw.c_str());
				fprintf(vmain, "\t\t\t\t\t\tw_s8, w_d8, w_d4, w_s4%s);\n",
					((dbg)&&(dbgstage==4))?", o_dbg":"");
			} else {
				fprintf(vmain, "\t// verilator lint_off UNUSED\n\twire\t\tw_os4;\n\t// verilator lint_on  UNUSED\n");
				fprintf(vmain, "\twire\t[%d:0]\tw_e4, w_o4;\n", 2*(obits+xtrapbits)-1);
				fprintf(vmain, "\tqtrstage%s\t#(%d,%d,%d,0,%d,%d)\tstage_e4(i_clk, %s, i_ce,\n",
					((dbg)&&(dbgstage==4))?"_dbg":"",
					nbits+xtrapbits, obits+xtrapbits, lgsize,
					(inverse)?1:0, (dropbit)?0:0,
					resetw.c_str());
				fprintf(vmain, "\t\t\t\t\t\tw_s8, w_e8, w_e4, w_s4%s);\n",
					((dbg)&&(dbgstage==4))?", o_dbg":"");
				fprintf(vmain, "\tqtrstage\t#(%d,%d,%d,1,%d,%d)\tstage_o4(i_clk, %s, i_ce,\n",
					nbits+xtrapbits, obits+xtrapbits, lgsize, (inverse)?1:0, (dropbit)?0:0,
	
					resetw.c_str());
				fprintf(vmain, "\t\t\t\t\t\tw_s8, w_o8, w_o4, w_os4);\n");
			}
			dropbit ^= 1;
			nbits = obits;
			tmp_size >>= 1; lgtmp--;
		}

		{
			obits = nbits+((dropbit)?0:1);
			if (obits > nbitsout)
				obits = nbitsout;
			if ((maxbitsout>0)&&(obits > maxbitsout))
				obits = maxbitsout;
			fprintf(vmain, "\twire\t\tw_s2;\n");
			if (single_clock) {
				fprintf(vmain, "\twire\t[%d:0]\tw_d2;\n",
					2*obits-1);
			} else {
				fprintf(vmain, "\twire\t[%d:0]\tw_e2, w_o2;\n",
					2*obits-1);
			}
			if ((nbits+xtrapbits+1 == obits)&&(!dropbit))
				printf("WARNING: SCALING OFF BY A FACTOR OF TWO--should\'ve dropped a bit in the last stage.\n");

			if (single_clock) {
				fprintf(vmain, "\tsngllast\t#(%d,%d,%d)\tstage_2(i_clk, %s, i_ce,\n",
					nbits+xtrapbits, obits,(dropbit)?0:1,
					resetw.c_str());
				fprintf(vmain, "\t\t\t\t\tw_s4, w_d4, w_d2, w_s2);\n");
			} else {
				fprintf(vmain, "\tdblstage\t#(%d,%d,%d)\tstage_2(i_clk, %s, i_ce,\n",
					nbits+xtrapbits, obits,(dropbit)?0:1,
					resetw.c_str());
				fprintf(vmain, "\t\t\t\t\tw_s4, w_e4, w_o4, w_e2, w_o2, w_s2);\n");
			}

			fprintf(vmain, "\n\n");
			nbits = obits;
		}

		fprintf(vmain, "\t// Prepare for a (potential) bit-reverse stage.\n");
		if (single_clock)
			fprintf(vmain, "\tassign\tbr_sample= w_d2;\n");
		else {
			fprintf(vmain, "\tassign\tbr_left  = w_e2;\n");
			fprintf(vmain, "\tassign\tbr_right = w_o2;\n");
		}
		fprintf(vmain, "\n");
		if (bitreverse) {
			fprintf(vmain, "\twire\tbr_start;\n");
			fprintf(vmain, "\treg\tr_br_started;\n");
			fprintf(vmain, "\tinitial\tr_br_started = 1\'b0;\n");
			if (async_reset) {
				fprintf(vmain, "\talways @(posedge i_clk, negedge i_areset_n)\n");
				fprintf(vmain, "\t\tif (!i_areset_n)\n");
			} else {
				fprintf(vmain, "\talways @(posedge i_clk)\n");
				fprintf(vmain, "\t\tif (i_reset)\n");
			}
			fprintf(vmain, "\t\t\tr_br_started <= 1\'b0;\n");
			fprintf(vmain, "\t\telse if (i_ce)\n");
			fprintf(vmain, "\t\t\tr_br_started <= r_br_started || w_s2;\n");
			fprintf(vmain, "\tassign\tbr_start = r_br_started || w_s2;\n");
		}
	}


	fprintf(vmain, "\n");
	fprintf(vmain, "\t// Now for the bit-reversal stage.\n");
	fprintf(vmain, "\twire\tbr_sync;\n");
	if (bitreverse) {
		if (single_clock) {
			fprintf(vmain, "\twire\t[(2*OWIDTH-1):0]\tbr_o_result;\n");
			fprintf(vmain, "\tsnglbrev\t#(%d,%d)\n\t\trevstage(i_clk, %s,\n", lgsize, nbitsout, resetw.c_str());
			fprintf(vmain, "\t\t\t(i_ce & br_start), br_sample,\n");
			fprintf(vmain, "\t\t\tbr_o_result, br_sync);\n");
		} else {
			fprintf(vmain, "\twire\t[(2*OWIDTH-1):0]\tbr_o_left, br_o_right;\n");
			fprintf(vmain, "\tdblreverse\t#(%d,%d)\n\t\trevstage(i_clk, %s,\n", lgsize, nbitsout, resetw.c_str());
			fprintf(vmain, "\t\t\t(i_ce & br_start), br_left, br_right,\n");
			fprintf(vmain, "\t\t\tbr_o_left, br_o_right, br_sync);\n");
		}
	} else if (single_clock) {
		fprintf(vmain, "\tassign\tbr_o_result = br_result;\n");
		fprintf(vmain, "\tassign\tbr_sync     = w_s2;\n");
	} else {
		fprintf(vmain, "\tassign\tbr_o_left  = br_left;\n");
		fprintf(vmain, "\tassign\tbr_o_right = br_right;\n");
		fprintf(vmain, "\tassign\tbr_sync    = w_s2;\n");
	}

	fprintf(vmain,
"\n\n"
"\t// Last clock: Register our outputs, we\'re done.\n"
"\tinitial\to_sync  = 1\'b0;\n");
	if (async_reset)
		fprintf(vmain,
"\talways @(posedge i_clk, negedge i_areset_n)\n\t\tif (!i_areset_n)\n");
	else {
		fprintf(vmain,
"\talways @(posedge i_clk)\n\t\tif (i_reset)\n");
	}

	fprintf(vmain,
"\t\t\to_sync  <= 1\'b0;\n"
"\t\telse if (i_ce)\n"
"\t\t\to_sync  <= br_sync;\n"
"\n"
"\talways @(posedge i_clk)\n"
"\t\tif (i_ce)\n");
	if (single_clock) {
		fprintf(vmain, "\t\t\to_result  <= br_o_result;\n");
	} else {
		fprintf(vmain,
"\t\tbegin\n"
"\t\t\to_left  <= br_o_left;\n"
"\t\t\to_right <= br_o_right;\n"
"\t\tend\n");
	}

	fprintf(vmain,
"\n\n"
"endmodule\n");
	fclose(vmain);


	{
		std::string	fname;

		fname = coredir + "/butterfly.v";
		build_butterfly(fname.c_str(), xtracbits, rounding);

		if (mpy_stages > 0) {
			fname = coredir + "/hwbfly.v";
			build_hwbfly(fname.c_str(), xtracbits, rounding, ckpce, async_reset);
		}

		{
			// To make debugging easier, we build both of these
			fname = coredir + "/shiftaddmpy.v";
			build_multiply(fname.c_str());

			fname = coredir + "/longbimpy.v";
			build_longbimpy(fname.c_str());
			fname = coredir + "/bimpy.v";
			build_bimpy(fname.c_str());
		}

		if ((dbg)&&(dbgstage == 4)) {
			fname = coredir + "/qtrstage_dbg.v";
			build_quarters(fname.c_str(), rounding, async_reset, true);
		}
		fname = coredir + "/qtrstage.v";
		build_quarters(fname.c_str(), rounding, async_reset, false);


		if (single_clock) {
			fname = coredir + "/sngllast.v";
			build_sngllast(fname.c_str(), async_reset);
		} else {
			if ((dbg)&&(dbgstage == 2))
				fname = coredir + "/dblstage_dbg.v";
			else
				fname = coredir + "/dblstage.v";
			build_dblstage(fname.c_str(), rounding, async_reset, (dbg)&&(dbgstage==2));
		}

		if (bitreverse) {
			if (single_clock) {
				fname = coredir + "/snglbrev.v";
				build_snglbrev(fname.c_str(), async_reset);
			} else {
				fname = coredir + "/dblreverse.v";
				build_dblreverse(fname.c_str(), async_reset);
			}
		}

		const	char	*rnd_string = "";
		switch(rounding) {
			case RND_TRUNCATE:	rnd_string = "/truncate.v"; break;
			case RND_FROMZERO:	rnd_string = "/roundfromzero.v"; break;
			case RND_HALFUP:	rnd_string = "/roundhalfup.v"; break;
			default:
				rnd_string = "/convround.v"; break;
		} fname = coredir + rnd_string;
		switch(rounding) {
			case RND_TRUNCATE: build_truncator(fname.c_str()); break;
			case RND_FROMZERO: build_roundfromzero(fname.c_str()); break;
			case RND_HALFUP: build_roundhalfup(fname.c_str()); break;
			default:
				build_convround(fname.c_str()); break;
		}

	}

	if (verbose_flag)
		printf("All done -- success\n");
}
