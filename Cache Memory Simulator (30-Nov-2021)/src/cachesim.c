#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long int ulong;

// Struct type to store information needed for a cache line.
typedef struct _ {
    int valid, relativeAge, lastAccessed;
    ulong tag;
} cacheLine_t;

// Struct type to store information needed for a cache.
typedef struct {
    cacheLine_t ***cache;
    int numHits, numMisses, numMemReads, numMemWrites;
} cache_t;

// Function to initialize the cache.
void initCache(cache_t *cache, int numSets, int numWays) {
    cache->cache = (cacheLine_t ***) malloc(numSets * sizeof(cacheLine_t **));
    for (int i = 0; i < numSets; i++) {
        cache->cache[i] = (cacheLine_t **) malloc(numWays * sizeof(cacheLine_t *));
        for (int j = 0; j < numWays; j++) {
            cache->cache[i][j] = (cacheLine_t *) malloc(sizeof(cacheLine_t));
            cache->cache[i][j]->valid = 0;
            cache->cache[i][j]->tag = 0;
            cache->cache[i][j]->relativeAge = 0;
            cache->cache[i][j]->lastAccessed = 0;
        }
    }
    cache->numHits = 0;
    cache->numMisses = 0;
    cache->numMemReads = 0;
    cache->numMemWrites = 0;
}

// Utility function to calculate log base 2 of a number.
int _log2(int n) {
    int i = 0;
    while (n >>= 1) i++;
    return i;
}

// Function to process a memory access.
void processTransaction(cache_t *cache, 
                        int associativity, 
                        int numSets, 
                        ulong setIndex, 
                        ulong tag, 
                        ulong prefetchSetIndex, 
                        ulong prefetchTag, 
                        int isWrite, 
                        int isFifo, 
                        int prefetch, 
                        FILE *debugFile) {
    // Direct mapped cache.
    if (associativity == 1) {
        // Handle hits.
        if (cache->cache[setIndex][0]->valid && cache->cache[setIndex][0]->tag == tag) {
            if (debugFile) fprintf(debugFile, "Hit, ");

            cache->numHits++;
        }
        // Handle misses.
        else {
            if (debugFile) fprintf(debugFile, "Miss, ");

            // Increment the number of misses.
            cache->numMisses++;
            // Read the block from memory.
            cache->numMemReads++;

            cache->cache[setIndex][0]->valid = 1;
            cache->cache[setIndex][0]->tag = tag;
            cache->cache[setIndex][0]->relativeAge = 0;
            cache->cache[setIndex][0]->lastAccessed = 0;

            if (prefetch) {
                // If the prefetching block is not in the cache, read it from memory and load it into the cache.
                if (!cache->cache[prefetchSetIndex][0]->valid || cache->cache[prefetchSetIndex][0]->tag != prefetchTag) {
                    cache->numMemReads++;

                    cache->cache[prefetchSetIndex][0]->valid = 1;
                    cache->cache[prefetchSetIndex][0]->tag = prefetchTag;
                    cache->cache[prefetchSetIndex][0]->relativeAge = 0;
                    cache->cache[prefetchSetIndex][0]->lastAccessed = 0;
                }
            }
        }

        // Write the block to memory if the access type is a write.
        cache->numMemWrites += isWrite;

        return;
    }

    // n-way set associative cache.
    // Check if the address is in the cache.
    int hitIndex = -1;
    for (int i = 0; i < associativity; i++) {
        cacheLine_t *line = cache->cache[setIndex][i];
        if (line->valid && line->tag == tag) {
            cache->numHits++;
            hitIndex = i;
            break;
        }
    }

    // Handle misses in the cache.
    if (hitIndex == -1) {
        // Debugging
        if (debugFile) fprintf(debugFile, "MISS, ");

        // Increment the number of misses.
        cache->numMisses++;
        // Read the block from memory.
        cache->numMemReads++;
        // Check for an invalid line in the cache.
        int lineId = -1;
        for (int i = 0; i < associativity; i++)
            if (!cache->cache[setIndex][i]->valid) {
                lineId = i;
                break;
            }

        // If there is an invalid line, load the block into it.
        if (lineId != -1) {
            for (int i = 0; i < associativity; i++)
                if (cache->cache[setIndex][i]->valid) {
                    cache->cache[setIndex][i]->relativeAge++;
                    cache->cache[setIndex][i]->lastAccessed++;
                }

            cache->cache[setIndex][lineId]->valid = 1;
        }
        // If all lines in the cache set are valid, replace according to the replacement policy.
        else {
            // Replace the cache line that was loaded least recently.
            if (isFifo) {
                // Find the cache line that was loaded least recently.
                int maxAge = 0;
                lineId = 0;
                for (int i = 0; i < associativity; i++)
                    if (cache->cache[setIndex][i]->relativeAge > maxAge) {
                        maxAge = cache->cache[setIndex][i]->relativeAge;
                        lineId = i;
                    }
            }
            // Replace the cache line that was accessed least recently.
            else {
                // Find the cache line that was accessed least recently.
                int oldestAccess = 0;
                lineId = 0;
                for (int i = 0; i < associativity; i++) {
                    if (cache->cache[setIndex][i]->lastAccessed > oldestAccess) {
                        oldestAccess = cache->cache[setIndex][i]->lastAccessed;
                        lineId = i;
                    }
                }
            }

            // Update the relative last accessed times and relative ages of all cache lines in the set.
            for (int i = 0; i < associativity; i++)
                if (i != lineId) {
                    cache->cache[setIndex][i]->relativeAge++;
                    cache->cache[setIndex][i]->lastAccessed++;
                }
        }

        // Replace the cache line.
        cache->cache[setIndex][lineId]->tag = tag;
        cache->cache[setIndex][lineId]->relativeAge = 0;
        cache->cache[setIndex][lineId]->lastAccessed = 0;

        // Prefetching
        if (prefetch) {
            hitIndex = -1;
            for (int i = 0; i < associativity; i++)
                if (cache->cache[prefetchSetIndex][i]->valid && cache->cache[prefetchSetIndex][i]->tag == prefetchTag) {
                    hitIndex = i;
                    break;
                }

            // If it's not in the cache, load it into the cache.
            if (hitIndex == -1) {
                cache->numMemReads++;

                // Check for an invalid line in the cache.
                lineId = -1;
                for (int i = 0; i < associativity; i++) {
                    if (!cache->cache[prefetchSetIndex][i]->valid) {
                        lineId = i;
                        break;
                    }
                }

                // If there is an invalid line, load the block into it.
                if (lineId != -1) {
                    cache->cache[prefetchSetIndex][lineId]->tag = prefetchTag;

                    for (int i = 0; i < associativity; i++)
                        if (cache->cache[prefetchSetIndex][i]->valid) {
                            cache->cache[prefetchSetIndex][i]->relativeAge++;
                            cache->cache[prefetchSetIndex][i]->lastAccessed++;
                        }

                    cache->cache[prefetchSetIndex][lineId]->valid = 1;
                }
                // If all lines in the cache set are valid, replace according to the replacement policy.
                else {
                    // Replace the cache line that was loaded least recently.
                    if (isFifo) {
                        // Find the cache line that was loaded least recently.
                        int maxAge = 0;
                        lineId = 0;
                        for (int i = 0; i < associativity; i++) {
                            if (cache->cache[prefetchSetIndex][i]->relativeAge > maxAge) {
                                maxAge = cache->cache[prefetchSetIndex][i]->relativeAge;
                                lineId = i;
                            }
                        }
                    }
                    // Replace the cache line that was accessed least recently.
                    else {
                        // Find the cache line that was accessed least recently.
                        int oldestAccess = 0;
                        lineId = 0;
                        for (int i = 0; i < associativity; i++) {
                            if (cache->cache[prefetchSetIndex][i]->lastAccessed > oldestAccess) {
                                oldestAccess = cache->cache[prefetchSetIndex][i]->lastAccessed;
                                lineId = i;
                            }
                        }
                    }

                    // Update the relative last accessed times and relative ages of all cache lines in the set.
                    for (int i = 0; i < associativity; i++)
                        if (i != lineId) {
                            cache->cache[prefetchSetIndex][i]->relativeAge++;
                            cache->cache[prefetchSetIndex][i]->lastAccessed++;
                        }
                }

                // Replace the cache line.
                cache->cache[prefetchSetIndex][lineId]->tag = prefetchTag;
                cache->cache[prefetchSetIndex][lineId]->relativeAge = 0;
                cache->cache[prefetchSetIndex][lineId]->lastAccessed = 0;
            }
        }
    }
    // Handle hits in the cache.
    else {
        // Debugging.
        if (debugFile) fprintf(debugFile, "HIT, ");

        cache->cache[setIndex][hitIndex]->lastAccessed = 0;
        for (int i = 0; i < associativity; i++)
            if (i != hitIndex) cache->cache[setIndex][i]->lastAccessed++;
    }

    // Write the block to memory if the access type is a write.
    cache->numMemWrites += isWrite;

    // Debugging.
    if (debugFile) {
        for (int i = 0; i < numSets; i++) {
            for (int j = 0; j < associativity; j++) {
                if (cache->cache[i][j]->valid)
                    fprintf(debugFile, "%lx(%d)", cache->cache[i][j]->tag, cache->cache[i][j]->lastAccessed);
                else fprintf(debugFile, "-");
                if (j != associativity - 1) fprintf(debugFile, " + ");
            }
            fprintf(debugFile, " | ");
        }
        fprintf(debugFile, ", MemReads: %d, MemWrites: %d\n", cache->numMemReads, cache->numMemWrites);
    }
}

int main(int argc, char const *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <cache size> <associativity> <replacement policy> <block size> <trace file>\n", argv[0]);
        return 1;
    }

    // Initially assume fully associative cache for simplicity of parsing.
    int cacheSize = atoi(argv[1]), blockSize = atoi(argv[4]), associativity = cacheSize / blockSize;

    if (cacheSize & !cacheSize & (cacheSize - 1) || blockSize & !blockSize & (blockSize - 1)) {
        printf("Cache size and block size must be powers of 2\n");
        return 1;
    }

    if (strcmp(argv[2], "direct") == 0) associativity = 1;
    else if (strcmp(argv[2], "assoc")) {
        char *dup = strdup(argv[2]);
        associativity = atoi(strtok(dup, "assoc:"));
        free(dup);

        if (associativity & !associativity & (associativity - 1)) {
            printf("Associativity must be a power of 2\n");
            return 1;
        }
    }

    int isFifo = strcmp(argv[3], "fifo") == 0, numSets = cacheSize / (blockSize * associativity);

    FILE *traceFile = fopen(argv[5], "r");
    if (!traceFile) {
        printf("Could not open trace file\n");
        return 1;
    }

    int s = _log2(numSets), b = _log2(blockSize);
    cache_t *prefetchingCache = (cache_t *) malloc(sizeof(cache_t));
    cache_t *nonPrefetchingCache = (cache_t *) malloc(sizeof(cache_t));

    // Initialize caches
    initCache(prefetchingCache, numSets, associativity);
    initCache(nonPrefetchingCache, numSets, associativity);

    char accessType;
    ulong address;

    // char debugFileName[100];
    // sprintf(debugFileName, "%s.%s.%d.%s.%d-debug.csv", argv[5], argv[3], cacheSize, argv[2], blockSize);
    // FILE *debugFile = fopen(debugFileName, "w+");

    while (fscanf(traceFile, "%*x: %c %lx", &accessType, &address) == 2) {
        ulong setIndex = (address >> b) & ((1 << s) - 1);
        ulong tag = address >> (b + s);
        ulong prefetchAddress = address + blockSize;
        ulong prefetchSetIndex = (prefetchAddress >> b) & ((1 << s) - 1);
        ulong prefetchTag = prefetchAddress >> (b + s);

        // Debugging
        // fprintf(debugFile, "%c, %lx, %lx, %ld, ", accessType, address, tag, setIndex);

        processTransaction(nonPrefetchingCache, associativity, numSets, setIndex, tag, prefetchSetIndex, prefetchTag, accessType == 'W', isFifo, 0, NULL);
        processTransaction(prefetchingCache, associativity, numSets, setIndex, tag, prefetchSetIndex, prefetchTag, accessType == 'W', isFifo, 1, NULL);
    }

    fclose(traceFile);
    // fclose(debugFile);

    // Print the results.
    printf("Prefetch 0\n");
    printf("Memory reads: %d\n", nonPrefetchingCache->numMemReads);
    printf("Memory writes: %d\n", nonPrefetchingCache->numMemWrites);
    printf("Cache hits: %d\n", nonPrefetchingCache->numHits);
    printf("Cache misses: %d\n", nonPrefetchingCache->numMisses);
    printf("Prefetch 1\n");
    printf("Memory reads: %d\n", prefetchingCache->numMemReads);
    printf("Memory writes: %d\n", prefetchingCache->numMemWrites);
    printf("Cache hits: %d\n", prefetchingCache->numHits);
    printf("Cache misses: %d\n", prefetchingCache->numMisses);

    // Free memory.
    for (int i = 0; i < numSets; i++) {
        for (int j = 0; j < associativity; j++) {
            free(prefetchingCache->cache[i][j]);
            free(nonPrefetchingCache->cache[i][j]);
        }
        free(prefetchingCache->cache[i]);
        free(nonPrefetchingCache->cache[i]);
    }
    free(prefetchingCache->cache);
    free(nonPrefetchingCache->cache);
    free(prefetchingCache);
    free(nonPrefetchingCache);

    return 0;
}
