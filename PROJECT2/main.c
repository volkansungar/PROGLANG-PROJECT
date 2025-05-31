#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "lexer.h"


int main(int argc, char *argv[]) {

	// INPUT OUTPUT FILE MANIPULATION 
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <input_filename>\n", argv[0]);
		return 1;
	}
	char *input_filename = argv[1]; // NAME OF THE CODE FILE
	char base_filename[256];
	char output_filename[256];
	char *dot_position;
	FILE *inputFile, *outputFile;
	dot_position = strrchr(input_filename, '.');

	size_t base_length = dot_position - input_filename;
	strncpy(base_filename, input_filename, base_length);
	base_filename[base_length] = 0; // NULL TERMINATION
	
	sprintf(output_filename, "%s.lx", base_filename); // lx file name is stored in output_filename
	// END OF I/O FILE MANIPULATION

	inputFile = fopen(input_filename, "r");
	outputFile = fopen(output_filename, "w");

    Token* tokens = lexer(inputFile, input_filename);

    

	fclose(inputFile);
	fclose(outputFile);

	printf("Lexical analysis completed. Output written to %s\n", output_filename);

	return 0;
}
