/* Hash128_256.hpp
Literally combines cityhash 128 bit and spookyhash 128 bit to make a fast 256 bit hash, plus 4-SHA256.
(C) 2013 Niall Douglas http://www.nedprod.com
Created: Feb 2013
*/

#ifndef BOOST_AFIO_HASH256_H
#define BOOST_AFIO_HASH256_H

/*! \file Hash128_256.hpp
\brief Provides the Hash128 and Hash256 hardware accelerated types
*/

#include "Int128_256.hpp"
#include <tuple>
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "impl/hashes/cityhash/src/city.cc"
#include "impl/hashes/spookyhash/SpookyV2.cpp"
#include "impl/hashes/4-sha256/sha256-ref.c"
#if BOOST_AFIO_HAVE_M128
#include "impl/hashes/4-sha256/sha256-sse.c"
#endif
#if BOOST_AFIO_HAVE_NEON128
#include "impl/hashes/sha256/sha256-neon.c"
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
#include <random>
#ifdef WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

#if ALLOW_UNALIGNED_READS
#error ALLOW_UNALIGNED_READS needs to be zero for ARM compatibility
#endif

namespace boost { namespace afio { namespace detail {

template<class Hash> struct HashOp
{
	size_t no;
	Hash *hashs;
	enum class HashType
	{
		Unknown,
		FastHash,
		SHA256
	} hashType;
	struct BOOST_AFIO_TYPEALIGNMENT(32) Scratch
	{
		char d[64];
		size_t pos, length;
		char ___pad[32-2*sizeof(size_t)];
	};
	aligned_allocator<Scratch, 32> alloc;
	Scratch *scratch; // Only used for SHA-256
	HashOp(size_t _no, Hash *_hashs) : no(_no), hashs(_hashs), hashType(HashType::Unknown), scratch(0) { }
	void make_scratch()
	{
		if(!scratch)
		{
			scratch=alloc.allocate(no);
			memset(scratch, 0, no*sizeof(Scratch));
		}
	}
	~HashOp()
	{
		if(scratch)
			alloc.deallocate(scratch, no);
	}
};

/*! \class Hash128
\brief Provides a 128 bit hash.

To use this you must compile Int128_256.cpp.

Intel Ivy Bridge: Fasthash (SpookyHash) performance on 32 bit is approx. 1.17 cycles/byte. Performance on 64 bit is approx. 0.31 cycles/byte.

Intel Atom: Performance on 32 bit is approx. 3.38 cycles/byte

ARM Cortex-A15: Performance on 32 bit is approx. 1.49 cycles/byte.
*/
class Hash128 : public Int128
{
	static Int128 int_init()
	{
		// First 32 bits of the fractional parts of the square roots of the first 8 primes 2..19
		static BOOST_AFIO_TYPEALIGNMENT(16) const unsigned int_iv[]={0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		return *((Int128 *) int_iv);
	}
public:
	//! Constructs an instance
	Hash128() : Int128(int_init()) { }
	explicit Hash128(const char *bytes) : Int128(bytes) { }
	//! Adds fast hashed data to this hash.
	void AddFastHashTo(const char *data, size_t length)
	{
		uint64 *spookyhash=(uint64 *) const_cast<unsigned long long *>(asLongLongs());
		SpookyHash::Hash128(data, length, spookyhash, spookyhash+1);
	}
	//! Batch adds fast hashed data to hashes.
	static void BatchAddFastHashTo(size_t no, Hash128 *hashs, const char **data, size_t *length)
	{
		// TODO: Implement a SIMD version of SpookyHash, and parallelise that too :)
#pragma omp parallel for schedule(dynamic)
		for(ptrdiff_t n=0; n<(ptrdiff_t) no; n++)
			hashs[n].AddFastHashTo(data[n], length[n]);
	}
};

/*! \class Hash256
\brief Provides a 256 bit hash.

To use this you must compile Int128_256.cpp.

Intel Ivy Bridge: Fasthash (combined SpookyHash + CityHash) performance on 32 bit is approx. 2.71 cycles/byte. Performance on 64 bit is approx. 0.46 cycles/byte.

Intel Atom (single hyperthreaded core): Fasthash (combined SpookyHash + CityHash) performance on 32 bit is approx. 9.31 cycles/byte.

ARM Cortex-A15: Fasthash (combined SpookyHash + CityHash) performance on 32 bit is approx. 2.96 cycles/byte.


Intel Ivy Bridge: SHA-256 performance on 32 bit is approx. 17.23 cycles/byte (batch 6.89 cycles/byte). Performance on 64 bit is approx. 14.89 cycles/byte (batch 4.23 cycles/byte).

Intel Atom (single hyperthreaded core): SHA-256 performance on 32 bit is approx. 40.35 cycles/byte (batch 24.46 cycles/byte).

ARM Cortex-A15: SHA-256 performance on 32 bit is approx. 21.24 cycles/byte (batch 15.11 cycles/byte).


SHA-256, being cryptographically secure, requires a setup, data contribution and finalisation stage in order to produce FIPS compliant
output (mainly because the total bits hashed must be appended at the end). Only AddSHA256ToBatch() can therefore correctly handle
incremental hashing if you want the "correct" hash to be output. Internally, this implies having to construct scratch space and having
to cope with non-block multiple incremental data. For speed, and for ease of programming the SSE2 SIMD units, the batch implementation
currently throws an exception if you supply non-64 byte multiples to AddSHA256ToBatch() except as the final increment before FinishBatch().
*/
class Hash256 : public Int256
{
	static Int256 int_init()
	{
		// First 32 bits of the fractional parts of the square roots of the first 8 primes 2..19
		static BOOST_AFIO_TYPEALIGNMENT(32) const unsigned int_iv[]={0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		return *((Int256 *) int_iv);
	}
	static void _FinishBatch(HashOp<Hash256> *h)
	{
		switch(h->hashType)
		{
		case HashOp<Hash256>::HashType::FastHash:
			{
			    break;
			}
		case HashOp<Hash256>::HashType::SHA256:
			{
				// Terminate all remaining hashes
				__sha256_block_t emptyblk;
				const __sha256_block_t *blks[4]={ &emptyblk, &emptyblk, &emptyblk, &emptyblk };
				__sha256_hash_t emptyout;
				__sha256_hash_t *out[4]={ &emptyout, &emptyout, &emptyout, &emptyout };
				int inuse=0;
				// First run is to find all hashes with scratchpos>=56 as these need an extra round
				for(size_t n=0; n<h->no; n++)
				{
					if(h->scratch[n].pos>=56)
					{
						memset(h->scratch[n].d+h->scratch[n].pos, 0, sizeof(__sha256_block_t) -h->scratch[n].pos);
						h->scratch[n].d[h->scratch[n].pos]=(unsigned char) 0x80;
						blks[inuse]=(const __sha256_block_t *) h->scratch[n].d;
						out[inuse]=(__sha256_hash_t *) h->hashs[n].asInts();
						if(4==++inuse)
						{
#if HAVE_M128 || defined(HAVE_NEON128)
							__sha256_int(blks, out);
#else
							//#pragma omp parallel for
							for(size_t z=0; z<4; z++)
								__sha256_osol(*blks[z], *out[z]);
#endif
							inuse=0;
						}
						h->scratch[n].pos=0;
					}
				}
				if(inuse)
				{
					for(size_t n=inuse; n<4; n++)
					{
						blks[n]=&emptyblk;
						out[n]=&emptyout;
					}
#if HAVE_M128 || defined(HAVE_NEON128)
					__sha256_int(blks, out);
#else
					//#pragma omp parallel for
					for(size_t z=0; z<4; z++)
						__sha256_osol(*blks[z], *out[z]);
#endif
					inuse=0;
				}
				// First run is to find all hashes with scratchpos>=56 as these need an extra round
				for(size_t n=0; n<h->no; n++)
				{
					BOOST_AFIO_PACKEDTYPE(struct termination_t
					{
						char data[56];
						uint64_t length;
					});
					termination_t *termination=(termination_t *) h->scratch[n].d;
					static_assert(sizeof(*termination)==64, "termination_t is not sized exactly 64 bytes!");
					memset(termination->data+h->scratch[n].pos, 0, sizeof(__sha256_block_t) -h->scratch[n].pos);
					termination->data[h->scratch[n].pos]=(unsigned char) 0x80;
					termination->length=bswap_64(8*h->scratch[n].length);
					blks[inuse]=(const __sha256_block_t *) h->scratch[n].d;
					out[inuse]=(__sha256_hash_t *) h->hashs[n].asInts();
					if(4==++inuse)
					{
#if BOOST_AFIO_HAVE_M128 || defined(BOOST_AFIO_HAVE_NEON128)
						__sha256_int(blks, out);
#else
						//#pragma omp parallel for
						for(size_t z=0; z<4; z++)
							__sha256_osol(*blks[z], *out[z]);
#endif
						inuse=0;
					}
				}
				if(inuse)
				{
					for(size_t n=inuse; n<4; n++)
					{
						blks[n]=&emptyblk;
						out[n]=&emptyout;
					}
#if BOOST_AFIO_HAVE_M128 || defined(BOOST_AFIO_HAVE_NEON128)
					__sha256_int(blks, out);
#else
					//#pragma omp parallel for
					for(size_t z=0; z<4; z++)
						__sha256_osol(*blks[z], *out[z]);
#endif
					inuse=0;
				}
				// As we're little endian flip back the words
				for(size_t n=0; n<h->no; n++)
				{
					for(int m=0; m<8; m++)
						*const_cast<unsigned int *>(h->hashs[n].asInts()+m)=LOAD_BIG_32(h->hashs[n].asInts()+m);
				}
				break;
			}
		}
	}
public:
	//! Constructs an instance
	Hash256() : Int256(int_init()) { }
	explicit Hash256(const char *bytes) : Int256(bytes) { }
	//! Adds fast hashed data to this hash. Uses two threads if given >=1024 bytes and OpenMP support.
	void AddFastHashTo(const char *data, size_t length)
	{
		uint64 *spookyhash=(uint64 *) const_cast<unsigned long long *>(asLongLongs());
		uint128 cityhash=*(uint128 *) (asLongLongs()+2);
#pragma omp parallel for if(length>=1024)
		for(int n=0; n<2; n++)
		{
			if(!n)
				SpookyHash::Hash128(data, length, spookyhash, spookyhash+1);
			else
				cityhash=CityHash128WithSeed(data, length, cityhash);
		}
		*(uint128 *) (asLongLongs()+2)=cityhash;
	}
	//! Adds SHA-256 data to this hash as a single operation.
	void AddSHA256To(const char *data, size_t length)
	{
		const __sha256_block_t *blks=(const __sha256_block_t *) data;
		size_t no=length/sizeof(__sha256_block_t);
		size_t remaining=length-(no*sizeof(__sha256_block_t));
		for(size_t n=0; n<no; n++)
			__sha256_osol(*blks++, const_cast<unsigned int *>(asInts()));

		// Do termination
		__sha256_block_t temp;
		memset(temp, 0, sizeof(temp));
		memcpy(temp, blks, remaining);
		// Pad to 56 bytes
		if(remaining<56)
			temp[remaining]=0x80;
		else
		{
			temp[remaining]=0x80;
			// Insufficient space for termination, so another round
			__sha256_osol(temp, const_cast<unsigned int *>(asInts()));
			memset(temp, 0, sizeof(temp));
		}
		*(uint64_t *) (temp+56)=bswap_64(8*length);
		__sha256_osol(temp, const_cast<unsigned int *>(asInts()));
		// Finally, as we're little endian flip back the words
		for(int n=0; n<8; n++)
			const_cast<unsigned int *>(asInts())[n]=LOAD_BIG_32(asInts()+n);
	}

	//! A handle to an ongoing batch hash operation
	typedef HashOp<Hash256> *BatchHashOp;
	//! Specifies which batch item this data is for. Format is hash idx, data, length of data.
	typedef std::tuple<size_t, const char *, size_t> BatchItem;
	//! Begins an incremental batch hash. Tip: use FinishBatch(h, false) to avoid recreating this.
	static BatchHashOp BeginBatch(size_t no, Hash256 *hashs)
	{
		return new HashOp<Hash256>(no, hashs);
	}
	//! Adds data to an incremental fast hash operation. Don't mix this with AddSHA256ToBatch() on the same BatchHashOp.
	static void AddFastHashToBatch(BatchHashOp h, size_t items, const BatchItem *datas)
	{
		if(h->hashType==HashOp<Hash256>::HashType::Unknown)
			h->hashType=HashOp<Hash256>::HashType::FastHash;
		else if(h->hashType!=HashOp<Hash256>::HashType::FastHash)
			throw std::runtime_error("You can't add a fast hash to a SHA-256 hash");
#pragma omp parallel for schedule(dynamic)
		for(ptrdiff_t n=0; n<(ptrdiff_t) items; n++)
		{
			auto &data=datas[n];
			h->hashs[get<0>(data)].AddFastHashTo(get<1>(data), get<2>(data));
		}
	}
	//! Adds data to an incremental SHA-256 operation. Don't mix this with AddSHA256ToBatch() on the same BatchHashOp.
	static void AddSHA256ToBatch(BatchHashOp h, size_t no, const BatchItem *datas)
	{
		if(h->hashType==HashOp<Hash256>::HashType::Unknown)
			h->hashType=HashOp<Hash256>::HashType::SHA256;
		else if(h->hashType!=HashOp<Hash256>::HashType::SHA256)
			throw std::runtime_error("You can't add a SHA-256 hash to a fast hash");
		h->make_scratch();
		// TODO: No reason this can't OpenMP parallelise given sufficient no
		__sha256_block_t emptyblk;
		size_t hashidxs[4]={ 0 };
		const __sha256_block_t *blks[4]={ &emptyblk, &emptyblk, &emptyblk, &emptyblk };
		size_t togos[4]={ 0 };
		__sha256_hash_t emptyout;
		__sha256_hash_t *out[4]={ &emptyout, &emptyout, &emptyout, &emptyout };
		int inuse=0;
		auto retire=[h, &hashidxs, &emptyblk, &blks, &togos, &emptyout, &out](size_t n){
			size_t hashidx=hashidxs[n];
			memcpy(h->scratch[hashidx].d, blks[n], togos[n]);
			h->scratch[hashidx].pos=togos[n];
			blks[n]=&emptyblk;
			out[n]=&emptyout;
		};
		do
		{
			// Fill SHA streams with work
			if(no)
			{
				for(size_t n=0; n<4; n++)
				{
					while(&emptyblk==blks[n] && no)
					{
						auto &data=*datas;
						hashidxs[n]=get<0>(data);
						if(h->scratch[hashidxs[n]].pos)
							throw std::runtime_error("Feeding SHA-256 with chunks not exactly divisible by 64 bytes, except as the very final chunk, is currently not supported.");
						else
							blks[n]=(const __sha256_block_t *) get<1>(data);
						out[n]=(__sha256_hash_t *) const_cast<unsigned int *>(h->hashs[hashidxs[n]].asInts());
						h->scratch[hashidxs[n]].length=togos[n]=get<2>(data);
						datas++;
						no--;
						// Too small, so retire instantly
						if(togos[n]<sizeof(__sha256_block_t))
							retire(n);
						else
							inuse++;
					}
				}
			}
#if BOOST_AFIO_HAVE_M128 || defined(BOOST_AFIO_HAVE_NEON128)
			__sha256_int(blks, out);
#else
			//#pragma omp parallel for
			for(size_t n=0; n<4; n++)
				__sha256_osol(*blks[n], *out[n]);
#endif
			for(size_t n=0; n<4; n++)
			{
				if(&emptyblk==blks[n]) continue;
				blks[n]++;
				togos[n]-=sizeof(__sha256_block_t);
				if(togos[n]<sizeof(__sha256_block_t))
				{
					retire(n);
					inuse--;
				}
			}
			// We know from benchmarking that the above can push 3.5 streams in the time of a single stream,
			// so keep going if there are at least two streams remaining
		} while(inuse>1);
		if(inuse)
		{
			for(size_t n=0; n<4; n++)
			{
				if(&emptyblk!=blks[n])
				{
					do
					{
						__sha256_osol(*blks[n], *out[n]);
						blks[n]++;
						togos[n]-=sizeof(__sha256_block_t);
					} while(togos[n]>=sizeof(__sha256_block_t));
					retire(n);
					inuse--;
				}
			}
		}
	}
	//! Finishes an incremental batch hash
	static void FinishBatch(BatchHashOp h, bool free=true)
	{
		_FinishBatch(h);
		if(free)
			delete h;
		else
			h->hashType=HashOp<Hash256>::HashType::Unknown;
	}

	//! Batch adds fast hashed data to hashes as a single operation.
	static void BatchAddFastHashTo(size_t no, Hash256 *hashs, const char **data, size_t *length)
	{
		HashOp<Hash256> h(no, hashs);
		BatchItem *items=(BatchItem *) alloca(sizeof(BatchItem) *no);
		for(size_t n=0; n<no; n++)
			items[n]=BatchItem(n, data[n], length[n]);
		AddFastHashToBatch(&h, no, items);
		_FinishBatch(&h);
	}
	//! Batch adds SHA-256 data to hashes as a single operation.
	static void BatchAddSHA256To(size_t no, Hash256 *hashs, const char **data, size_t *length)
	{
		HashOp<Hash256> h(no, hashs);
		BatchItem *items=(BatchItem *) alloca(sizeof(BatchItem) *no);
		for(size_t n=0; n<no; n++)
			items[n]=BatchItem(n, data[n], length[n]);
		AddSHA256ToBatch(&h, no, items);
		_FinishBatch(&h);
	}
};

} } } //namespace

namespace std
{
#define BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE boost::afio::detail::Hash128
#include "incl_stl_allocator_override.hpp"
#undef BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE
#define BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE boost::afio::detail::Hash256
#include "incl_stl_allocator_override.hpp"
#undef BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE
}

#endif
