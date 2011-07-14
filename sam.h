/*
 * sam.h
 *
 *  Created on: Sep 23, 2009
 *      Author: Ben Langmead
 */

#ifndef SAM_H_
#define SAM_H_

#include <string>
#include "ds.h"
#include "read.h"
#include "util.h"

enum {
	// Comments use language from v1.4-r962 spec
	SAM_FLAG_PAIRED         = 1,   // templ. having mult. frag.s in sequencing
	SAM_FLAG_MAPPED_PAIRED  = 2,   // each frag properly aligned
	SAM_FLAG_UNMAPPED       = 4,   // fragment unmapped
	SAM_FLAG_MATE_UNMAPPED  = 8,   // next fragment in template unmapped
	SAM_FLAG_QUERY_STRAND   = 16,  // SEQ is reverse comp'ed from original
	SAM_FLAG_MATE_STRAND    = 32,  // next fragment SEQ reverse comp'ed
	SAM_FLAG_FIRST_IN_PAIR  = 64,  // first fragment in template
	SAM_FLAG_SECOND_IN_PAIR = 128, // last fragment in template
	SAM_FLAG_NOT_PRIMARY    = 256, // secondary alignment
	SAM_FLAG_FAILS_CHECKS   = 512, // not passing quality controls
	SAM_FLAG_DUPLICATE      = 1024 // PCR or optical duplicate
};

class OutFileBuf;
class AlnRes;
class AlnFlags;
class AlnSetSumm;

/**
 * Encapsulates all the various ways that a user may wish to customize SAM
 * output.
 */
class SamConfig {

	typedef EList<std::string> StrList;
	typedef EList<size_t> LenList;

public:

	SamConfig(
		const StrList& refnames,  // reference sequence names
		const LenList& reflens,   // reference sequence lengths
		bool truncQname,          // truncate read name to 255?
		bool omitsec,             // omit secondary SEQ/QUAL
		const std::string& pg_id, // id
		const std::string& pg_pn, // name
		const std::string& pg_vn, // version
		const std::string& pg_cl, // command-line
		bool print_as,
		bool print_xs,
		bool print_xn,
		bool print_cs,
		bool print_cq,
		bool print_x0,
		bool print_x1,
		bool print_xm,
		bool print_xo,
		bool print_xg,
		bool print_nm,
		bool print_md,
		bool print_yf,
		bool print_ym,
		bool print_yp,
		bool print_yt,
		bool print_ys) :
		truncQname_(truncQname),
		omitsec_(omitsec),
		pg_id_(pg_id),
		pg_pn_(pg_pn),
		pg_vn_(pg_vn),
		pg_cl_(pg_cl),
		refnames_(refnames),
		reflens_(reflens),
		print_as_(print_as),
		print_xs_(print_xs),
		print_xn_(print_xn),
		print_cs_(print_cs),
		print_cq_(print_cq),
		print_x0_(print_x0),
		print_x1_(print_x1),
		print_xm_(print_xm),
		print_xo_(print_xo),
		print_xg_(print_xg),
		print_nm_(print_nm),
		print_md_(print_md),
		print_yf_(print_yf),
		print_ym_(print_ym),
		print_yp_(print_yp),
		print_yt_(print_yt),
		print_ys_(print_ys)
	{
		assert_eq(refnames_.size(), reflens_.size());
	}

	/**
	 * Print a reference name in a way that doesn't violate SAM's character
	 * constraints. \*|[!-()+-<>-~][!-~]*
	 */
	void printRefName(
		OutFileBuf& o,
		const std::string& name)
		const;
	
	/**
	 * Print a read name in a way that doesn't violate SAM's character
	 * constraints. [!-?A-~]{1,255} (i.e. [33, 63], [65, 126])
	 */
	template<typename TStr>
	void printReadName(
		OutFileBuf& o,
		const TStr& name)
		const
	{
		size_t namelen = name.length();
		if( namelen >= 2 &&
			name[namelen-2] == '/' &&
		   (name[namelen-1] == '1' || name[namelen-1] == '2'))
		{
			namelen -= 2;
		}
		if(truncQname_ && namelen > 255) {
			namelen = 255;
		}
		for(size_t i = 0; i < namelen; i++) {
			if(isspace(name[i])) {
				return;
			}
			o.write(name[i]);
		}
	}

	/**
	 * Print a reference name given a reference index.
	 */
	void printRefNameFromIndex(
		OutFileBuf& o,
		size_t i)
		const;
	
	/**
	 * Print SAM header to given output buffer.
	 */
	void printHeader(
		OutFileBuf& o,
		bool printSq,
		bool printPg)
		const;

	/**
	 * Print the @SQ header lines to the given OutFileBuf.
	 */
	void printSqLines(OutFileBuf& o) const;

	/**
	 * Print the @PG header line to the given OutFileBuf.
	 */
	void printPgLine(OutFileBuf& o) const;

	/**
	 * Print the optional flags to the given OutFileBuf.
	 */
	void printAlignedOptFlags(
		OutFileBuf& o,          // output buffer
		bool first,             // first opt flag printed is first overall?
		bool exEnds,            // exclude ends of sequence?
		const Read& rd,         // the read
		const AlnRes& res,      // individual alignment result
		const AlnFlags& flags,  // alignment flags
		const AlnSetSumm& summ) // summary of alignments for this read
		const;

	/**
	 * Print the optional flags to the given OutFileBuf.
	 */
	void printEmptyOptFlags(
		OutFileBuf& o,          // output buffer
		bool first,             // first opt flag printed is first overall?
		const AlnFlags& flags,  // alignment flags
		const AlnSetSumm& summ) // summary of alignments for this read
		const;
	
	/**
	 * Return true iff we should try to obey the SAM spec's recommendations
	 * that:
	 *
	 * SEQ and QUAL of secondary alignments should be set to ‘*’ to reduce the
	 * file size.
	 */
	bool omitSecondarySeqQual() const {
		return omitsec_;
	}

protected:

	bool truncQname_;   // truncate QNAME to 255 chars?
	bool omitsec_;      // omit secondary 
	
	std::string pg_id_; // @PG ID: Program record identifier
	std::string pg_pn_; // @PG PN: Program name
	std::string pg_vn_; // @PG VN: Program version
	std::string pg_cl_; // @PG CL: Program command-line
	const StrList& refnames_; // reference sequence names
	const LenList& reflens_;  // reference sequence lengths
	
	// Which alignment flags to print?

	// Following are printed by BWA-SW
	bool print_as_; // AS:i: Alignment score generated by aligner
	bool print_xs_; // XS:i: Suboptimal alignment score
	bool print_xn_; // XN:i: Number of ambiguous bases in the referenece

	// Other optional flags
	bool print_cs_; // CS:Z: Color read sequence on the original strand
	bool print_cq_; // CQ:Z: Color read quality on the original strand

	// Following are printed by BWA
	bool print_x0_; // X0:i: Number of best hits
	bool print_x1_; // X1:i: Number of sub-optimal best hits
	bool print_xm_; // XM:i: Number of mismatches in the alignment
	bool print_xo_; // XO:i: Number of gap opens
	bool print_xg_; // XG:i: Number of gap extensions (incl. opens)
	bool print_nm_; // NM:i: Edit dist. to the ref, Ns count, clipping doesn't
	bool print_md_; // MD:Z: String for mms. [0-9]+(([A-Z]|\^[A-Z]+)[0-9]+)*2

	// Following are Bowtie2-specific
	bool print_yf_; // YF:i: Read was filtered out?
	bool print_ym_; // YM:i: Read was repetitive when aligned unpaired?
	bool print_yp_; // YP:i: Read was repetitive when aligned paired?
	bool print_yt_; // YT:Z: String representing alignment type
	bool print_ys_; // YS:i: Score of other mate

	EList<char>   tmpmdop_;   // temporary holder for MD:Z ops
	EList<char>   tmpmdch_;   // temporary holder for MD:Z chars
	EList<size_t> tmpmdrun_;  // temporary holder for MD:Z runs
};

#endif /* SAM_H_ */
