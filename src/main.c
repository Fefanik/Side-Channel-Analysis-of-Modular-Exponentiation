// Turn off compiler optimizations
// Try to turn on compiler optimizations and execute again
// Measure time of operation (available to an attacker)
// Measure counts of operation (usually not available to an atatcker, but gives us insight)
// Try profiler to obtain count of operations

// What is "secret" information stored at server? What is data that attacker can choose?

// Plot.ly - Traces->Range/bins 1

#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <memory.h>
#include "bignum.h"

typedef unsigned long ULONG;
const int ULONG_LENGTH = sizeof(ULONG);

typedef enum {
	NAIVE = 1,
	SQUAREANDMULTIPLY = 2,
	SQUAREANDMULTIPLYMASKED = 3,
	SLIDINGWINDOWEXPMOD = 4,
	SQUAREANDMULTIPLY_PROTECTED = 5,
	squareAndMultiplyMPI_IF_ = 6,
	SQUAREANDMULTIPLY_CMOVE = 7
} ExpType;

void naiveExponentiationMPI(const mpi* message, const mpi* exponent, const mpi* modulus, mpi* result) {
	mpi_copy(result, message);

	mpi counter; mpi_init(&counter);
	mpi_copy(&counter, exponent);

	while (mpi_cmp_int(&counter, 1) > 0) {
		mpi_mul_mpi(result, result, message);
		mpi_mod_mpi(result, result, modulus);
		mpi_sub_int(&counter, &counter, 1);
	}
}

void squareAndMultiplyMPI(const mpi* message, const mpi* exponent, const mpi* modulus, mpi* result) {
	// Obtain length of exponent in bits (shoudl be ideally pre-computed)
	int sizeExponent = 0;
	int bit = 0;
	int maxBitLength = mpi_size(exponent) * 8;
	for (int i = 0; i < maxBitLength; i++) {
		bit = mpi_get_bit(exponent, i);
		if (bit != 0) { sizeExponent = i + 1; }
	}

	// Compute square and multiply algorithm
	mpi_lset(result, 1);
	for (int i = sizeExponent - 1; i >= 0; i--) {
		mpi_mul_mpi(result, result, result); // result *= result;
		mpi_mod_mpi(result, result, modulus); // result %= modulus;
		if (mpi_get_bit(exponent, i) != 0) { // given bit is not 0
    		// result = (result * message) % modulus;
			mpi_mul_mpi(result, result, message);
			mpi_mod_mpi(result, result, modulus);
		}
	}
}


void squareAndMultiplyProtectedMPI(const mpi* message, const mpi* exponent,
						  const mpi* modulus, mpi* result)
{
	// Compute length of exponent in bits
	int sizeExponent = 0;
	int maxBitLength = mpi_size(exponent) * 8;

	for (int i = 0; i < maxBitLength; i++) {
		if (mpi_get_bit(exponent, i) != 0)
			sizeExponent = i + 1;
	}

	mpi bogus;
	mpi_init(&bogus);
	mpi_lset(&bogus, 1);
	mpi_lset(result, 1);

	for (int i = sizeExponent - 1; i >= 0; i--) {

		// Always square
		mpi_mul_mpi(result, result, result);
		mpi_mod_mpi(result, result, modulus);

		int bit = mpi_get_bit(exponent, i);

		if (bit != 0) {
			mpi_mul_mpi(result, result, message);
		} else {
			mpi_mul_mpi(result, result, &bogus);
		}

		mpi_mod_mpi(result, result, modulus);
	}

	mpi_free(&bogus);
}

void squareAndMultiplyMPI_IF(const mpi* message, const mpi* exponent,
								   const mpi* modulus, mpi* result)
{
	int sizeExponent = 0;
	int maxBitLength = mpi_size(exponent) * 8;

	for (int i = 0; i < maxBitLength; i++) {
		if (mpi_get_bit(exponent, i))
			sizeExponent = i + 1;
	}

	mpi bogus;
	mpi_init(&bogus);
	mpi_lset(&bogus, 1);

	const mpi* table[2];
	table[0] = &bogus;
	table[1] = message;
	mpi_lset(result, 1);

	for (int i = sizeExponent - 1; i >= 0; i--) {
		mpi_mul_mpi(result, result, result);
		mpi_mod_mpi(result, result, modulus);

		int bit = mpi_get_bit(exponent, i) & 1;

		mpi_mul_mpi(result, result, table[bit]);

		mpi_mod_mpi(result, result, modulus);
	}

	mpi_free(&bogus);
}

/* Select between two non-negative MPIs without a secret-indexed load.
 * The loop width is the maximum candidate width, which depends only on the
 * public message input. This is a conditional-move experiment, not a claim
 * that the legacy MPI arithmetic as a whole is constant-time.
 */
static int mpi_conditional_move(mpi* destination, const mpi* when_zero,
								const mpi* when_one, unsigned int bit)
{
	size_t width = when_zero->n > when_one->n ? when_zero->n : when_one->n;
	t_uint mask = (t_uint) 0 - (t_uint) (bit & 1U);

	if (mpi_grow(destination, width) != 0)
		return POLARSSL_ERR_MPI_MALLOC_FAILED;

	for (size_t i = 0; i < width; ++i) {
		t_uint zero_limb = i < when_zero->n ? when_zero->p[i] : 0;
		t_uint one_limb = i < when_one->n ? when_one->p[i] : 0;
		destination->p[i] = (zero_limb & ~mask) | (one_limb & mask);
	}
	for (size_t i = width; i < destination->n; ++i)
		destination->p[i] = 0;
	destination->s = 1;
	return 0;
}

void squareAndMultiplyMPICMove(const mpi* message, const mpi* exponent,
							   const mpi* modulus, mpi* result)
{
	int sizeExponent = 0;
	int maxBitLength = mpi_size(exponent) * 8;
	for (int i = 0; i < maxBitLength; ++i) {
		if (mpi_get_bit(exponent, i) != 0)
			sizeExponent = i + 1;
	}

	mpi bogus, selected;
	mpi_init(&bogus);
	mpi_init(&selected);
	mpi_lset(&bogus, 1);
	mpi_lset(result, 1);

	for (int i = sizeExponent - 1; i >= 0; --i) {
		mpi_mul_mpi(result, result, result);
		mpi_mod_mpi(result, result, modulus);
		mpi_conditional_move(&selected, &bogus, message,
							 (unsigned int) mpi_get_bit(exponent, i));
		mpi_mul_mpi(result, result, &selected);
		mpi_mod_mpi(result, result, modulus);
	}

	mpi_free(&selected);
	mpi_free(&bogus);
}



long measureExponentiationMPI(const mpi* message, const mpi* exponent, const mpi* modulus, ExpType exponentiationType) {
	mpi result;
	mpi_init(&result);
	clock_t begin = clock();
	switch (exponentiationType) {
	case NAIVE: {
		naiveExponentiationMPI(message, exponent, modulus, &result);
		break;
	}
	case SQUAREANDMULTIPLY: {
		squareAndMultiplyMPI(message, exponent, modulus, &result);
		break;
	}
	case SQUAREANDMULTIPLY_PROTECTED: {
		squareAndMultiplyProtectedMPI(message,exponent,modulus,&result);
		break;
	}
	case squareAndMultiplyMPI_IF_: {
		squareAndMultiplyMPI_IF(message, exponent, modulus, &result);
		break;
	}
	case SQUAREANDMULTIPLY_CMOVE: {
		squareAndMultiplyMPICMove(message, exponent, modulus, &result);
		break;
	}
	case SLIDINGWINDOWEXPMOD: {
		// Native MPI function
		mpi_exp_mod(&result, message, exponent, modulus, NULL);
		break;
	}

/*
	case SQUAREANDMULTIPLYMASKED: {
		result = squareAndMultiplyMaskedMPI(message, exponent, modulus);
		break;
	}
*/
	}
	clock_t end = clock();
	long elapsed = end - begin;

	// printf("\n######################################\n");
	// if (exponentiationType == NAIVE) { printf("Method: NAIVE MPI\n"); }
	// if (exponentiationType == SQUAREANDMULTIPLY) { printf("Method: SQUAREANDMULTIPLY MPI\n"); }
	// if (exponentiationType == SQUAREANDMULTIPLYMASKED) { printf("Method: SQUAREANDMULTIPLYMASKED MPI\n"); }
	// if (exponentiationType == SLIDINGWINDOWEXPMOD) { printf("Method: SLIDINGWINDOWEXPMOD MPI\n"); }

	// print results
	// mpi_write_file("Message\t= ", message, 16, stdout);
	// mpi_write_file("Exponent= ", exponent, 16, stdout);
	// mpi_write_file("Modulus\t= ", modulus, 16, stdout);
	// mpi_write_file("Result\t= ", &result, 16, stdout);

	// printf("Elapsed CPU ticks: %d\n", elapsed);
	printf("%ld\n", elapsed);

	return elapsed;
}




void measureExponentiationRepeatMPI(mpi* message, mpi* exponent, mpi* modulus, ExpType exponentiationType, int numRepeats, const char* fileName) {
	FILE* outFile = fopen(fileName, "w");
	if (outFile == NULL) {
		perror(fileName);
		return;
	}
	long elapsed = 0;
	for (int i = 0; i < numRepeats; i++) {
		elapsed = measureExponentiationMPI(message, exponent, modulus, exponentiationType);
		fprintf(outFile, "%ld\n", elapsed);
	}
	fclose(outFile);
}

/**
	Toy random number generation function. Not suitable for rea crypto use due to weak RNG generator!!!
*/
int generateRNG(void *, unsigned char * buffer, size_t numBytes) {
	for (size_t i = 0; i < numBytes; i++) {
		buffer[i] = rand() % 256;
	}
	return 0;
}

void scenario1(char* path) {
	char file1[512];
	char file2[512];
	char file3[512];
	char file4[512];

	snprintf(file1, sizeof(file1), "%s/scenario1_original.txt", path);
	snprintf(file2, sizeof(file2), "%s/scenario1_protected.txt", path);
	snprintf(file3, sizeof(file3), "%s/scenario1_table_lookup.txt", path);
	snprintf(file4, sizeof(file4), "%s/scenario1_conditional_move.txt", path);

	mpi message;
	mpi exponent;
	mpi modulus;

	mpi_init(&message);
	mpi_init(&exponent);
	mpi_init(&modulus);

	mpi_lset(&message, 12345678L);
	mpi_lset(&exponent, 268435455L);
	mpi_lset(&modulus, 1000000007L);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY,
								   1000, file1);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_PROTECTED,
								   1000, file2);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   squareAndMultiplyMPI_IF_,
								   1000, file3);
	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_CMOVE,
								   1000, file4);
}

void scenario2(char* path) {
	mpi message;
	mpi exponent;
	mpi modulus;

	mpi_init(&message);
	mpi_init(&exponent);
	mpi_init(&modulus);

	mpi_lset(&message, 1);
	mpi_lset(&exponent, 268435455L);
	mpi_lset(&modulus, 1000000007L);

	char file1[512];
	char file2[512];
	char file3[512];
	char file4[512];

	snprintf(file1, sizeof(file1), "%s/scenario2_original.txt", path);
	snprintf(file2, sizeof(file2), "%s/scenario2_protected.txt", path);
	snprintf(file3, sizeof(file3), "%s/scenario2_table_lookup.txt", path);
	snprintf(file4, sizeof(file4), "%s/scenario2_conditional_move.txt", path);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY,
								   1000, file1);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_PROTECTED,
								   1000, file2);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   squareAndMultiplyMPI_IF_,
								   1000, file3);
	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_CMOVE,
								   1000, file4);

	mpi_free(&message);
	mpi_free(&exponent);
	mpi_free(&modulus);
}

void scenario3(char* path) {
	mpi message;
	mpi exponent;
	mpi modulus;

	mpi_init(&message);
	mpi_init(&exponent);
	mpi_init(&modulus);

	mpi_lset(&message, 0xFFFFFFFF);
	mpi_lset(&exponent, 268435455L);
	mpi_lset(&modulus, 1000000007L);

	char file1[512];
	char file2[512];
	char file3[512];
	char file4[512];

	snprintf(file1, sizeof(file1), "%s/scenario3_original.txt", path);
	snprintf(file2, sizeof(file2), "%s/scenario3_protected.txt", path);
	snprintf(file3, sizeof(file3), "%s/scenario3_table_lookup.txt", path);
	snprintf(file4, sizeof(file4), "%s/scenario3_conditional_move.txt", path);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY,
								   1000, file1);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_PROTECTED,
								   1000, file2);

	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   squareAndMultiplyMPI_IF_,
								   1000, file3);
	measureExponentiationRepeatMPI(&message, &exponent, &modulus,
								   SQUAREANDMULTIPLY_CMOVE,
								   1000, file4);

	mpi_free(&message);
	mpi_free(&exponent);
	mpi_free(&modulus);
}

void scenario4(char* path) {
    mpi message;
    mpi exponent_low;
    mpi exponent_high;
    mpi modulus;

    mpi_init(&message);
    mpi_init(&exponent_low);
    mpi_init(&exponent_high);
    mpi_init(&modulus);

    mpi_fill_random(&message, 256, generateRNG, NULL);
    mpi_lset(&modulus, 1000000007L);

    mpi_lset(&exponent_low, 0);
    mpi_set_bit(&exponent_low, 200, 1);

    mpi_lset(&exponent_high, 0xFFFFFFFF);

    char file1[512];
    char file2[512];
    char file3[512];
    char file4[512];
    char file5[512];
    char file6[512];
    char file7[512];
    char file8[512];

    snprintf(file1, sizeof(file1), "%s/scenario4_low_original.txt", path);
    snprintf(file2, sizeof(file2), "%s/scenario4_low_protected.txt", path);
    snprintf(file3, sizeof(file3), "%s/scenario4_low_table_lookup.txt", path);

    snprintf(file4, sizeof(file4), "%s/scenario4_high_original.txt", path);
    snprintf(file5, sizeof(file5), "%s/scenario4_high_protected.txt", path);
    snprintf(file6, sizeof(file6), "%s/scenario4_high_table_lookup.txt", path);
    snprintf(file7, sizeof(file7), "%s/scenario4_low_conditional_move.txt", path);
    snprintf(file8, sizeof(file8), "%s/scenario4_high_conditional_move.txt", path);

    // Low-HW exponent
    measureExponentiationRepeatMPI(&message, &exponent_low, &modulus,
                                   SQUAREANDMULTIPLY,
                                   1000, file1);

    measureExponentiationRepeatMPI(&message, &exponent_low, &modulus,
                                   SQUAREANDMULTIPLY_PROTECTED,
                                   1000, file2);

    measureExponentiationRepeatMPI(&message, &exponent_low, &modulus,
                                   squareAndMultiplyMPI_IF_,
                                   1000, file3);
    measureExponentiationRepeatMPI(&message, &exponent_low, &modulus,
                                   SQUAREANDMULTIPLY_CMOVE,
                                   1000, file7);

    // High-HW exponent
    measureExponentiationRepeatMPI(&message, &exponent_high, &modulus,
                                   SQUAREANDMULTIPLY,
                                   1000, file4);

    measureExponentiationRepeatMPI(&message, &exponent_high, &modulus,
                                   SQUAREANDMULTIPLY_PROTECTED,
                                   1000, file5);

    measureExponentiationRepeatMPI(&message, &exponent_high, &modulus,
                                   squareAndMultiplyMPI_IF_,
                                   1000, file6);
    measureExponentiationRepeatMPI(&message, &exponent_high, &modulus,
                                   SQUAREANDMULTIPLY_CMOVE,
                                   1000, file8);

    mpi_free(&message);
    mpi_free(&exponent_low);
    mpi_free(&exponent_high);
    mpi_free(&modulus);
}
int main(int argc, char** argv){
	const char* output_path = argc > 1 ? argv[1] : ".";
	scenario1((char*) output_path);
	scenario2((char*) output_path);
	scenario3((char*) output_path);
	scenario4((char*) output_path);

	return 0;
}

