// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Riecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>
#include <bignum.h>

#include <stdio.h>

#include <openssl/bn.h>
#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util.h>
#include <validation.h>

unsigned int ComputeMinWork(unsigned int nBase, int64_t nTime, const Consensus::Params& consensusParams)
{
    // Testnet has min-difficulty blocks after nTargetSpacing*2 time between blocks
    if (consensusParams.fPowAllowMinDifficultyBlocks && nTime > consensusParams.nPowTargetSpacing*2)
        return MinPrimeSizeCompacted;

    arith_uint256 bnResult;
    bnResult.SetCompact(nBase);
    while (nTime > 0 && bnResult > iMinPrimeSize)
    {
        // Maximum 400% adjustment...
        bnResult *=  55572;
        bnResult >>= 16;
        nTime -= consensusParams.nPowTargetTimespan*4;
    }
    if (bnResult < iMinPrimeSize)
        return MinPrimeSizeCompacted;
    return bnResult.GetCompact();
}

CBigNum nthRoot( CBigNum const & n, int root, CBigNum const & lowerBound )
{
    CBigNum result = lowerBound;
    CBigNum delta = lowerBound / 2;

    while( delta >= 1 )
    {
        result += delta;
        CBigNum aux = result;
        for( int i = 1; i < root; i++ )
            aux *= result;
        if( aux > n )
        {
            result -= delta;
            delta >>= 1;
        }
        else
            delta <<= 1;
    }
    return result;
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& consensusParams)
{
    // Genesis block
    if (pindexLast == NULL)
        return MinPrimeSizeCompacted;

    // Only change once per interval
    if ((pindexLast->nHeight+1) % consensusParams.DifficultyAdjustmentInterval() != 0)
    {
        LogPrintf("* PASSED once per interval test\n");
        if( isAfterFork1( pindexLast->nHeight+1, consensusParams ) )
        {
                LogPrintf("* PASSED isAfterFork test\n");
                if( isSuperblock(pindexLast->nHeight+1, consensusParams) )
                {
                        LogPrintf("* PASSED isSuperblock test\n");
                        CBigNum bnNewPow;
                        bnNewPow.SetCompact(pindexLast->nBits);
                        bnNewPow *= 95859; // superblock is 4168/136 times more difficult
                        bnNewPow >>= 16; // 95859/65536 ~= (4168/136) ^ 1/9
                        LogPrintf("GetNextWorkRequired superblock difficulty:  %08x  %s\n", bnNewPow.GetCompact(), bnNewPow.getuint256().ToString());
                        return bnNewPow.GetCompact();
                } else if( isSuperblock(pindexLast->nHeight+1-1, consensusParams) ) {
                LogPrintf("* PASSED fell through to non isSuperblock\n"); 
                //debug
                LogPrintf("diff = %08X\n", pindexLast->pprev->nBits);
                return pindexLast->pprev->nBits;
            }
        }

        if (consensusParams.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->nTime > pindexLast->nTime + consensusParams.nPowTargetSpacing*2)
                return MinPrimeSizeCompacted;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && (pindex->nHeight % consensusParams.DifficultyAdjustmentInterval()) != 0 && pindex->nBits == iMinPrimeSize)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        LogPrintf("* PASSED fell through to DiffAdjInterval fail\n");
        //debug
        LogPrintf("diff = %08X\n", pindexLast->nBits);
        return pindexLast->nBits;
    }

    LogPrintf("* PASSED made it here\n");

    // Go back by what we want to be nTargetTimespan worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    int i = 0;
    if( pindexLast->nHeight+1 == consensusParams.DifficultyAdjustmentInterval() ) // do not include genesis block
        i++;
    for( ; pindexFirst && i < consensusParams.DifficultyAdjustmentInterval()-1; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if( pindexLast->nHeight+1 >= consensusParams.DifficultyAdjustmentInterval() * 2)
    {
      if (nActualTimespan < consensusParams.nPowTargetTimespan/4)
          nActualTimespan = consensusParams.nPowTargetTimespan/4;
      if (nActualTimespan > consensusParams.nPowTargetTimespan*4)
          nActualTimespan = consensusParams.nPowTargetTimespan*4;
    }

    // Retarget
    CBigNum bnNew;
    CBigNum bnNewPow;
    bnNew.SetCompact(pindexLast->nBits);
    // 9th power (3+constellationSize)
    bnNewPow = pindexLast->GetBlockWork();

    bnNewPow *= consensusParams.nPowTargetTimespan;
    bnNewPow /= nActualTimespan;

    if( isAfterFork1( pindexLast->nHeight+1, consensusParams) )
    {
        if( isInSuperblockInterval(pindexLast->nHeight+1, consensusParams) ) // once per week, our interval contains a superblock
        {
            bnNewPow *= 68; // * 136/150 to compensate for difficult superblock
            bnNewPow /= 75;
            LogPrintf("Adjusted because has superblock\n");
        }
        else if( isInSuperblockInterval(pindexLast->nHeight, consensusParams) )
        {
            bnNewPow *= 75; // * 150/136 to compensate for previous adj
            bnNewPow /= 68;
            LogPrintf("Adjusted because had superblock\n");
        }
    }

    bnNew = nthRoot( bnNewPow, 3+constellationSize, bnNew / 2 );

    if (bnNew < iMinPrimeSize)
        bnNew = iMinPrimeSize;
    else if( bnNew > (unsigned long long)-1 )
        bnNew = (unsigned long long)-1;

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("nTargetTimespan = %d    nActualTimespan = %d\n", consensusParams.nPowTargetTimespan, nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, CBigNum().SetCompact(pindexLast->nBits).getuint256().ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.getuint256().ToString());

    return bnNew.GetCompact();
}

unsigned int generatePrimeBase( CBigNum &bnTarget, uint256 hash, bitsType compactBits )
{
    bnTarget = 1;
    bnTarget <<= zeroesBeforeHashInPrime;

    for ( int i = 0; i < 256; i++ )
    {
        bnTarget = (bnTarget << 1) + (hash.GetLow32() & 1);
        hash = (ArithToUint256(UintToArith256(hash) >>= 1));
    }
    CBigNum nBits;
    nBits.SetCompact(compactBits);
    if( nBits > nBits.getuint() ) // the protocol stores a compact big int so it supports larger values, but this version of the client does not
    {
        nBits = (unsigned int)-1; // saturate diff at (2**32) - 1, this should be enough for some years ;)
    }
    const unsigned int significativeDigits =  1 + zeroesBeforeHashInPrime + 256;
    unsigned int trailingZeros = nBits.getuint();
    if( trailingZeros < significativeDigits )
        return 0;
    trailingZeros -= significativeDigits;
    bnTarget <<= trailingZeros;
    return trailingZeros;
}

bool CheckProofOfWork(uint256 hash, bitsType compactBits, uint256 delta, const Consensus::Params& consensusParams)
{
    if( hash == uint256S("26d0466d5a0eab0ebf171eacb98146b26143d143463514f26b28d3cded81c1bb") )
        return true;

    CBigNum bnTarget;
    unsigned int trailingZeros = generatePrimeBase( bnTarget, hash, compactBits );

    if (trailingZeros < 256)
    {
        uint256 deltaLimit = ArithToUint256(1);
        deltaLimit = ArithToUint256(UintToArith256(deltaLimit) <<= trailingZeros);
        if( UintToArith256(delta) >= UintToArith256(deltaLimit) )
            return error("CheckProofOfWork() : candidate larger than allowed %s of %s", delta.ToString().c_str(), deltaLimit.ToString().c_str() );
    }

    CBigNum bigDelta = CBigNum(delta);
    bnTarget += bigDelta;

    if( (bnTarget % 210) != 97 )
        return error("CheckProofOfWork() : not valid pow");

    // first we do a single test to quickly discard most of the bogus cases
    if( BN_is_prime_fasttest( &bnTarget, 1, NULL, NULL, NULL, 1) != 1 )
    {
        LogPrintf("CheckProofOfWork fail  hash: %s  \ntarget: %d nOffset: %s\n", hash.GetHex().c_str(), compactBits, delta.GetHex().c_str());
        LogPrintf("CheckProofOfWork fail  target: %s  \n", bnTarget.GetHex().c_str());
        return error("CheckProofOfWork() : n not prime");
    }

    bnTarget += 4;
    if( BN_is_prime_fasttest( &bnTarget, 1, NULL, NULL, NULL, 1) != 1 )
    {
        return error("CheckProofOfWork() : n+4 not prime");
    }
    bnTarget += 2;
    if( BN_is_prime_fasttest( &bnTarget, 1, NULL, NULL, NULL, 1) != 1 )
    {
        return error("CheckProofOfWork() : n+6 not prime");
    }
    bnTarget += 4;
    if( BN_is_prime_fasttest( &bnTarget, 1, NULL, NULL, NULL, 1) != 1 )
    {
        return error("CheckProofOfWork() : n+10 not prime");
    }
    bnTarget += 2;
    if( BN_is_prime_fasttest( &bnTarget, 1, NULL, NULL, NULL, 1) != 1 )
    {
        return error("CheckProofOfWork() : n+12 not prime");
    }
    bnTarget += 4;
    if( BN_is_prime_fasttest( &bnTarget, 4, NULL, NULL, NULL, 1) != 1 )
    {
        return error("CheckProofOfWork() : n+16 not prime");
    }
    bnTarget -= 4;
    if( BN_is_prime_fasttest( &bnTarget, 3, NULL, NULL, NULL, 0) != 1 )
    {
        return error("CheckProofOfWork() : n+12 not prime");
    }
    bnTarget -= 2;
    if( BN_is_prime_fasttest( &bnTarget, 3, NULL, NULL, NULL, 0) != 1 )
    {
        return error("CheckProofOfWork() : n+10 not prime");
    }
    bnTarget -= 4;
    if( BN_is_prime_fasttest( &bnTarget, 3, NULL, NULL, NULL, 0) != 1 )
    {
        return error("CheckProofOfWork() : n+6 not prime");
    }
    bnTarget -= 2;
    if( BN_is_prime_fasttest( &bnTarget, 3, NULL, NULL, NULL, 0) != 1 )
    {
        return error("CheckProofOfWork() : n+4 not prime");
    }
    bnTarget -= 4;
    if( BN_is_prime_fasttest( &bnTarget, 3, NULL, NULL, NULL, 0) != 1 )
    {
        return error("CheckProofOfWork() : n not prime");
    }
    arith_uint256 bnBestChainLastDiff;
    bnBestChainLastDiff.SetCompact(compactBits);
    return true;
}

