#include <stdint.h>
#include <uint256.h>

typedef unsigned int bitsType;
typedef uint256 offsetType;

// const riecoin specific
static const bitsType iMinPrimeSize = 304;
static const uint32_t MinPrimeSizeCompacted = 0x02013000UL;
static const int constellationSize = 6;
static const int zeroesBeforeHashInPrime = 8;

