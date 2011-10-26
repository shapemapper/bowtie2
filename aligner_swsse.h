/*
 * Copyright 2011, Ben Langmead <blangmea@jhsph.edu>
 *
 * This file is part of Bowtie 2.
 *
 * Bowtie 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bowtie 2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bowtie 2.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALIGNER_SWSSE_H_
#define ALIGNER_SWSSE_H_
#ifndef NO_SSE

#include "ds.h"
#include "mem_ids.h"
#include "random_source.h"
#include "scoring.h"
#include "mask.h"
#include <emmintrin.h>
#include <strings.h>

class EList_m128i : public EList<__m128i, 256> {
public:

	/**
	 * Allocate initial default of S elements.
	 */
	explicit EList_m128i(int cat = 0) :
		cat_(cat), list_(NULL), sz_(0), cur_(0)
	{
		assert_geq(cat, 0);
	}

	/**
	 * Destructor.
	 */
	~EList_m128i() { free(); }

	/**
	 * Return number of elements.
	 */
	inline size_t size() const { return cur_; }

	/**
	 * Return number of elements allocated.
	 */
	inline size_t capacity() const { return sz_; }
	
	/**
	 * Ensure that there is sufficient capacity to expand to include
	 * 'thresh' more elements without having to expand.
	 */
	inline void ensure(size_t thresh) {
		if(list_ == NULL) lazyInit();
		expandCopy(cur_ + thresh);
	}

	/**
	 * Ensure that there is sufficient capacity to include 'newsz' elements.
	 * If there isn't enough capacity right now, expand capacity to exactly
	 * equal 'newsz'.
	 */
	inline void reserveExact(size_t newsz) {
		if(list_ == NULL) lazyInitExact(newsz);
		expandCopyExact(newsz);
	}

	/**
	 * Return true iff there are no elements.
	 */
	inline bool empty() const { return cur_ == 0; }
	
	/**
	 * Return true iff list hasn't been initialized yet.
	 */
	inline bool null() const { return list_ == NULL; }

	/**
	 * If size is less than requested size, resize up to at least sz
	 * and set cur_ to requested sz.
	 */
	void resize(size_t sz) {
		if(sz > 0 && list_ == NULL) lazyInit();
		if(sz <= cur_) {
			cur_ = sz;
			return;
		}
		if(sz_ < sz) expandCopy(sz);
		cur_ = sz;
	}

	/**
	 * If size is less than requested size, resize up to exactly sz and set
	 * cur_ to requested sz.
	 */
	void resizeExact(size_t sz) {
		if(sz > 0 && list_ == NULL) lazyInitExact(sz);
		if(sz <= cur_) {
			cur_ = sz;
			return;
		}
		if(sz_ < sz) expandCopyExact(sz);
		cur_ = sz;
	}

	/**
	 * Make the stack empty.
	 */
	void clear() {
		cur_ = 0; // re-use stack memory
		// Don't clear heap; re-use it
	}

	/**
	 * Return a reference to the ith element.
	 */
	inline __m128i& operator[](size_t i) {
		assert_lt(i, cur_);
		return list_[i];
	}

	/**
	 * Return a reference to the ith element.
	 */
	inline __m128i operator[](size_t i) const {
		assert_lt(i, cur_);
		return list_[i];
	}

	/**
	 * Return a reference to the ith element.
	 */
	inline __m128i& get(size_t i) {
		return operator[](i);
	}
	
	/**
	 * Return a reference to the ith element.
	 */
	inline __m128i get(size_t i) const {
		return operator[](i);
	}

	/**
	 * Return a pointer to the beginning of the buffer.
	 */
	__m128i *ptr() { return list_; }

	/**
	 * Return a const pointer to the beginning of the buffer.
	 */
	const __m128i *ptr() const { return list_; }

	/**
	 * Return memory category.
	 */
	int cat() const { return cat_; }

private:

	/**
	 * Initialize memory for EList.
	 */
	void lazyInit() {
		assert(list_ == NULL);
		list_ = alloc(sz_);
	}

	/**
	 * Initialize exactly the prescribed number of elements for EList.
	 */
	void lazyInitExact(size_t sz) {
		assert_gt(sz, 0);
		assert(list_ == NULL);
		sz_ = sz;
		list_ = alloc(sz);
	}

	/**
	 * Allocate a T array of length sz_ and store in list_.  Also,
	 * tally into the global memory tally.
	 */
	__m128i *alloc(size_t sz) {
		__m128i* tmp = new __m128i[sz + 2];
		size_t tmpint = (size_t)tmp;
		// Align it!
		if((tmpint & 0xf) != 0) {
			tmpint += 15;
			tmpint &= (~0xf);
			tmp = reinterpret_cast<__m128i*>(tmpint);
		}
		assert_eq(0, (tmpint & 0xf)); // should be 16-byte aligned
		assert(tmp != NULL);
		gMemTally.add(cat_, sz);
		return tmp;
	}

	/**
	 * Allocate a T array of length sz_ and store in list_.  Also,
	 * tally into the global memory tally.
	 */
	void free() {
		if(list_ != NULL) {
			delete[] list_;
			gMemTally.del(cat_, sz_);
			list_ = NULL;
			sz_ = cur_ = 0;
		}
	}

	/**
	 * Expand the list_ buffer until it has at least 'thresh' elements.  Size
	 * increases quadratically with number of expansions.  Copy old contents
	 * into new buffer using operator=.
	 */
	void expandCopy(size_t thresh) {
		if(thresh <= sz_) return;
		size_t newsz = (sz_ * 2)+1;
		while(newsz < thresh) newsz *= 2;
		expandCopyExact(newsz);
	}

	/**
	 * Expand the list_ buffer until it has exactly 'newsz' elements.  Copy
	 * old contents into new buffer using operator=.
	 */
	void expandCopyExact(size_t newsz) {
		if(newsz <= sz_) return;
		__m128i* tmp = alloc(newsz);
		assert(tmp != NULL);
		size_t cur = cur_;
		if(list_ != NULL) {
 			for(size_t i = 0; i < cur_; i++) {
				// Note: operator= is used
				tmp[i] = list_[i];
			}
			free();
		}
		list_ = tmp;
		sz_ = newsz;
		cur_ = cur;
	}

	int      cat_;   // memory category, for accounting purposes
	__m128i *list_;  // list pointer, returned from new[]
	size_t   sz_;    // capacity
	size_t   cur_;   // occupancy (AKA size)
};

struct SSEMetrics {
	
	SSEMetrics() { reset(); }

	void clear() { reset(); }
	void reset() {
		dp = dpsat = dpfail = dpsucc = 
		col = cell = inner = fixup =
		gathcell = gathsol = bt = btfail = btsucc = btcell =
		corerej = nrej = 0;
	}
	
	void merge(const SSEMetrics& o, bool getLock = false) {
		ThreadSafe ts(&lock, getLock);
		dp       += o.dp;
		dpsat    += o.dpsat;
		dpfail   += o.dpfail;
		dpsucc   += o.dpsucc;
		col      += o.col;
		cell     += o.cell;
		inner    += o.inner;
		fixup    += o.fixup;
		gathcell += o.gathcell;
		gathsol  += o.gathsol;
		bt       += o.bt;
		btfail   += o.btfail;
		btsucc   += o.btsucc;
		btcell   += o.btcell;
		corerej  += o.corerej;
		nrej     += o.nrej;
	}

	uint64_t dp;       // DPs tried
	uint64_t dpsat;    // DPs saturated
	uint64_t dpfail;   // DPs failed
	uint64_t dpsucc;   // DPs succeeded
	uint64_t col;      // DP columns
	uint64_t cell;     // DP cells
	uint64_t inner;    // DP inner loop iters
	uint64_t fixup;    // DP fixup loop iters
	uint64_t gathcell; // DP gather cells examined
	uint64_t gathsol;  // DP gather solution cells found
	uint64_t bt;       // DP backtraces
	uint64_t btfail;   // DP backtraces failed
	uint64_t btsucc;   // DP backtraces succeeded
	uint64_t btcell;   // DP backtrace cells traversed
	uint64_t corerej;  // DP backtrace core rejections
	uint64_t nrej;     // DP backtrace N rejections
	MUTEX_T  lock;
};

/**
 * Encapsulates matrix information calculated by the SSE aligner.
 *
 * Matrix memory is laid out as follows:
 *
 * - Elements (individual cell scores) are packed into __m128i vectors
 * - Vectors are packed into quartets, quartet elements correspond to: a vector
 *   from E, one from F, one from H, and one that's "reserved"
 * - Quartets are packed into columns, where the number of quartets is
 *   determined by the number of query characters divided by the number of
 *   elements per vector
 *
 * Regarding the "reserved" element of the vector quartet: we use it for two
 * things.  First, we use the first column of reserved vectors to stage the
 * initial column of H vectors.  Second, we use the "reserved" vectors during
 * the backtrace procedure to store information about (a) which cells have been
 * traversed, (b) whether the cell is "terminal" (in local mode), etc.
 */
struct SSEMatrix {

	// Each matrix element is a quartet of vectors.  These constants are used
	// to identify members of the quartet.
	const static size_t E   = 0;
	const static size_t F   = 1;
	const static size_t H   = 2;
	const static size_t TMP = 3;

	SSEMatrix(int cat = 0) : nvecPerCell_(4), buf_(cat) { }

	/**
	 * Return a pointer to the matrix buffer.
	 */
	inline __m128i *ptr() {
		assert(inited_);
		return buf_.ptr();
	}
	
	/**
	 * Return a pointer to the E vector at the given row and column.  Note:
	 * here row refers to rows of vectors, not rows of elements.
	 */
	inline __m128i* evec(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_lt(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + E;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}

	/**
	 * Like evec, but it's allowed to ask for a pointer to one column after the
	 * final one.
	 */
	inline __m128i* evecUnsafe(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_leq(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + E;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}

	/**
	 * Return a pointer to the F vector at the given row and column.  Note:
	 * here row refers to rows of vectors, not rows of elements.
	 */
	inline __m128i* fvec(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_lt(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + F;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}

	/**
	 * Return a pointer to the H vector at the given row and column.  Note:
	 * here row refers to rows of vectors, not rows of elements.
	 */
	inline __m128i* hvec(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_lt(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + H;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}

	/**
	 * Return a pointer to the TMP vector at the given row and column.  Note:
	 * here row refers to rows of vectors, not rows of elements.
	 */
	inline __m128i* tmpvec(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_lt(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + TMP;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}

	/**
	 * Like tmpvec, but it's allowed to ask for a pointer to one column after
	 * the final one.
	 */
	inline __m128i* tmpvecUnsafe(size_t row, size_t col) {
		assert_lt(row, nvecrow_);
		assert_leq(col, nveccol_);
		size_t elt = row * rowstride() + col * colstride() + TMP;
		assert_lt(elt, buf_.size());
		return ptr() + elt;
	}
	
	/**
	 * Given a number of rows (nrow), a number of columns (ncol), and the
	 * number of words to fit inside a single __m128i vector, initialize the
	 * matrix buffer to accomodate the needed configuration of vectors.
	 */
	void init(
		size_t nrow,
		size_t ncol,
		size_t wperv);
	
	/**
	 * Return the number of __m128i's you need to skip over to get from one
	 * cell to the cell one column over from it.
	 */
	inline size_t colstride() const { return colstride_; }

	/**
	 * Return the number of __m128i's you need to skip over to get from one
	 * cell to the cell one row down from it.
	 */
	inline size_t rowstride() const { return rowstride_; }

	/**
	 * Given a row, col and matrix (i.e. E, F or H), return the corresponding
	 * element.
	 */
	int eltSlow(size_t row, size_t col, size_t mat) const;
	
	/**
	 * Given a row, col and matrix (i.e. E, F or H), return the corresponding
	 * element.
	 */
	inline int elt(size_t row, size_t col, size_t mat) const {
		assert(inited_);
		assert_lt(row, nrow_);
		assert_lt(col, ncol_);
		assert_lt(mat, 3);
		// Move to beginning of column/row
		size_t rowelt = row / nvecrow_;
		size_t rowvec = row % nvecrow_;
		size_t eltvec = (col * colstride_) + (rowvec * rowstride_) + mat;
		assert_lt(eltvec, buf_.size());
		if(wperv_ == 16) {
			return (int)((uint8_t*)(buf_.ptr() + eltvec))[rowelt];
		} else {
			assert_eq(8, wperv_);
			return (int)((int16_t*)(buf_.ptr() + eltvec))[rowelt];
		}
	}

#if 0
	/**
	 * Given a row, col and matrix (i.e. E, F or H), return the corresponding
	 * element.
	 */
	inline void* eltptr(size_t row, size_t col, size_t mat) const {
		assert(inited_);
		assert_lt(row, nrow_);
		assert_lt(col, ncol_);
		assert_lt(mat, 3);
		// Move to beginning of column/row
		size_t rowelt = row / nvecrow_;
		size_t rowvec = row % nvecrow_;
		size_t eltvec = (col * colstride_) + (rowvec * rowstride_) + mat;
		assert_lt(eltvec, buf_.size());
		return &buf_[eltvec] + rowelt;
	}
#endif	

	/**
	 * Return the element in the E matrix at element row, col.
	 */
	inline int eelt(size_t row, size_t col) const {
		return elt(row, col, E);
	}

	/**
	 * Return the element in the F matrix at element row, col.
	 */
	inline int felt(size_t row, size_t col) const {
		return elt(row, col, F);
	}

	/**
	 * Return the element in the H matrix at element row, col.
	 */
	inline int helt(size_t row, size_t col) const {
		return elt(row, col, H);
	}
	
	/**
	 * Return true iff the given cell has its reportedThru bit set.
	 */
	inline bool reportedThrough(
		size_t row,          // current row
		size_t col) const    // current column
	{
		return ((masks_[row * ncol_ + col] & (1 << 0)) != 0);
	}

	/**
	 * Set the given cell's reportedThru bit.
	 */
	inline void setReportedThrough(
		size_t row,          // current row
		size_t col)          // current column
	{
		masks_[row * ncol_ + col] |= (1 << 0);
	}

	/**
	 * Return true iff the H mask has been set with a previous call to hMaskSet.
	 */
	bool isHMaskSet(
		size_t row,          // current row
		size_t col) const;   // current column

	/**
	 * Set the given cell's H mask.  This is the mask of remaining legal ways to
	 * backtrack from the H cell at this coordinate.  It's 5 bits long and has
	 * offset=2 into the 16-bit field.
	 */
	void hMaskSet(
		size_t row,          // current row
		size_t col,          // current column
		int mask);

	/**
	 * Return true iff the E mask has been set with a previous call to eMaskSet.
	 */
	bool isEMaskSet(
		size_t row,          // current row
		size_t col) const;   // current column

	/**
	 * Set the given cell's E mask.  This is the mask of remaining legal ways to
	 * backtrack from the E cell at this coordinate.  It's 2 bits long and has
	 * offset=8 into the 16-bit field.
	 */
	void eMaskSet(
		size_t row,          // current row
		size_t col,          // current column
		int mask);
	
	/**
	 * Return true iff the F mask has been set with a previous call to fMaskSet.
	 */
	bool isFMaskSet(
		size_t row,          // current row
		size_t col) const;   // current column

	/**
	 * Set the given cell's F mask.  This is the mask of remaining legal ways to
	 * backtrack from the F cell at this coordinate.  It's 2 bits long and has
	 * offset=11 into the 16-bit field.
	 */
	void fMaskSet(
		size_t row,          // current row
		size_t col,          // current column
		int mask);

	/**
	 * Analyze a cell in the SSE-filled dynamic programming matrix.  Determine &
	 * memorize ways that we can backtrack from the cell.  If there is at least one
	 * way to backtrack, select one at random and return the selection.
	 *
	 * There are a few subtleties to keep in mind regarding which cells can be at
	 * the end of a backtrace.  First of all: cells from which we can backtrack
	 * should not be at the end of a backtrace.  But have to distinguish between
	 * cells whose masks eventually become 0 (we shouldn't end at those), from
	 * those whose masks were 0 all along (we can end at those).
	 */
	void analyzeCell(
		size_t row,          // current row
		size_t col,          // current column
		size_t ct,           // current cell type: E/F/H
		int refc,
		int readc,
		int readq,
		const Scoring& sc,   // scoring scheme
		TAlScore offsetsc,   // offset to add to each score
		TAlScore floorsc,    // local-alignment score floor
		RandomSource& rand,  // rand gen for choosing among equal options
		bool& empty,         // out: =true iff no way to backtrace
		int& cur,            // out: =type of transition
		bool& branch,        // out: =true iff we chose among >1 options
		bool& canMoveThru,   // out: =true iff ...
		bool& reportedThru); // out: =true iff ...

	/**
	 * Initialize the matrix of masks and backtracking flags.
	 */
	void initMasks();

	/**
	 * Return the number of rows in the dynamic programming matrix.
	 */
	size_t nrow() const {
		return nrow_;
	}

	/**
	 * Return the number of columns in the dynamic programming matrix.
	 */
	size_t ncol() const {
		return ncol_;
	}

	bool             inited_;      // initialized?
	size_t           nrow_;        // # rows
	size_t           ncol_;        // # columns
	size_t           nvecrow_;     // # vector rows (<= nrow_)
	size_t           nveccol_;     // # vector columns (<= ncol_)
	size_t           wperv_;       // # words per vector
	size_t           vecshift_;    // # bits to shift to divide by words per vec
	size_t           nvecPerCol_;  // # vectors per column
	size_t           nvecPerCell_; // # vectors per matrix cell (4)
	size_t           colstride_;   // # vectors b/t adjacent cells in same row
	size_t           rowstride_;   // # vectors b/t adjacent cells in same col
	EList_m128i      buf_;         // buffer for holding vectors
	EList<uint16_t>  masks_;       // buffer for masks/backtracking flags
};

/**
 * All the data associated with the query profile and other data needed for SSE
 * alignment of a query.
 */
struct SSEData {
	SSEData(int cat = 0) : buf_(cat), mat_(cat) { }
	EList_m128i    buf_;         // buffer for query profile & temp vecs
	__m128i       *qprof_;       // query profile
	size_t         qprofStride_; // stride for query profile
	size_t         gbarStride_;  // gap barrier for query profile
	SSEMatrix      mat_;         // SSE matrix for holding all E, F, H vectors
	size_t         maxPen_;      // biggest penalty of all
	size_t         maxBonus_;    // biggest bonus of all
	size_t         lastIter_;    // which 128-bit striped word has final row?
	size_t         lastWord_;    // which word within 128-word has final row?
	int            bias_;        // all scores shifted up by this for unsigned
};

/**
 * Return true iff the H mask has been set with a previous call to hMaskSet.
 */
inline bool SSEMatrix::isHMaskSet(
	size_t row,          // current row
	size_t col) const    // current column
{
	return ((masks_[row * ncol_ + col] & (1 << 1)) != 0);
}

/**
 * Set the given cell's H mask.  This is the mask of remaining legal ways to
 * backtrack from the H cell at this coordinate.  It's 5 bits long and has
 * offset=2 into the 16-bit field.
 */
inline void SSEMatrix::hMaskSet(
	size_t row,          // current row
	size_t col,          // current column
	int mask)
{
	assert_lt(mask, 32);
	masks_[row * ncol_ + col] &= ~(31 << 1);
	masks_[row * ncol_ + col] |= (1 << 1 | mask << 2);
}

/**
 * Return true iff the E mask has been set with a previous call to eMaskSet.
 */
inline bool SSEMatrix::isEMaskSet(
	size_t row,          // current row
	size_t col) const    // current column
{
	return ((masks_[row * ncol_ + col] & (1 << 7)) != 0);
}

/**
 * Set the given cell's E mask.  This is the mask of remaining legal ways to
 * backtrack from the E cell at this coordinate.  It's 2 bits long and has
 * offset=8 into the 16-bit field.
 */
inline void SSEMatrix::eMaskSet(
	size_t row,          // current row
	size_t col,          // current column
	int mask)
{
	assert_lt(mask, 4);
	masks_[row * ncol_ + col] &= ~(7 << 7);
	masks_[row * ncol_ + col] |=  (1 << 7 | mask << 8);
}

/**
 * Return true iff the F mask has been set with a previous call to fMaskSet.
 */
inline bool SSEMatrix::isFMaskSet(
	size_t row,          // current row
	size_t col) const    // current column
{
	return ((masks_[row * ncol_ + col] & (1 << 10)) != 0);
}

/**
 * Set the given cell's F mask.  This is the mask of remaining legal ways to
 * backtrack from the F cell at this coordinate.  It's 2 bits long and has
 * offset=11 into the 16-bit field.
 */
inline void SSEMatrix::fMaskSet(
	size_t row,          // current row
	size_t col,          // current column
	int mask)
{
	assert_lt(mask, 4);
	masks_[row * ncol_ + col] &= ~(7 << 10);
	masks_[row * ncol_ + col] |=  (1 << 10 | mask << 11);
}

#endif /*ndef NO_SSE*/
#endif /*ndef ALIGNER_SWSSE_H_*/
