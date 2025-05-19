#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define MAX_VAR_LENGTH 20
#define MAX_NUM_LENGTH 100


typedef enum {
	Keyword,
	Identifier,
	EndOfLine,
	Operator,
	IntConstant,
	OpenBlock,
	CloseBlock,
	StringConstant,
	EndOfFile,
	Error
} TokenType;

typedef struct {
	TokenType type;
	char value[256];
} Token;

int isAlNum (char c) {
	return isalpha(c) || isdigit(c);
}


TokenType getKeywordType(const char *str) {
	if (strcmp(str, "number") == 0) return Keyword;
	if (strcmp(str, "write") == 0) return Keyword;
	if (strcmp(str, "and") == 0) return Keyword;
	if (strcmp(str, "newline") == 0) return Keyword;
	if (strcmp(str, "repeat") == 0) return Keyword;
	if (strcmp(str, "times") == 0) return Keyword;
	return Identifier;
}

Token getNextToken(FILE *inputFile) {
	Token token;
	int character;
	int index = 0;
	character = fgetc(inputFile);
	while (isspace(character) || character == '*') {
		if (character == '*') {
			while ((character = fgetc(inputFile)) != EOF && character != '*')
				;
			if (character == EOF) {
				token.type = Error;
				strcpy(token.value, "Unterminated comment");
		 		return token;
			}
		}
		character = fgetc(inputFile);
	}
	if (character == EOF) {
		token.type = EndOfFile;
		return token;
	}
	
	// VARIABLE KONTROLÜ
	if (isalpha(character)) {
		while (isAlNum(character) || character == '_') {
			token.value[index++] = (char)character;
			character = fgetc(inputFile);
		}
		ungetc(character, inputFile);
		token.value[index] = 0;
		token.type = getKeywordType(token.value);
	}

	else if (character == '-') {
		token.value[index++] = (char)character;
		character = fgetc(inputFile);

		if (isdigit(character)) {
			while (isdigit(character)) {
				token.value[index++] = (char)character;
				character = fgetc(inputFile);
			}
			ungetc(character, inputFile);
			token.value[index] = 0;
			token.type = IntConstant;
		} else if (character == '=') {
			token.value[index++] = (char)character;
			token.type = Operator;
		}
	}

	// OPERATOR
	else if (character == ':' || character == '+') {
		token.value[index++] = (char)character;
		character = fgetc(inputFile);
		if (character == '=') {
			token.value[index] = (char)character;
			token.value[index+1] = 0;
			token.type = Operator;
		}
	}

	// STRING CONSTANT
	else if (character == '"') {
		token.value[index++] = (char)character;
		character = fgetc(inputFile);
		while (character != '"') {
			token.value[index++] = (char)character;
			character = fgetc(inputFile);
		}
		token.value[index] = (char)character;
		token.value[index+1] = 0;
		token.type = StringConstant;
	}

	// END OF LINE
	else if (character == ';') {
		token.value[index++] = (char)character;
		token.value[index] = 0;
		token.type = EndOfLine;
	}

	// NUMBER
	else if (isdigit(character)) {
			while (isdigit(character)) {
				token.value[index++] = (char)character;
				character = fgetc(inputFile);
			}
			ungetc(character, inputFile);
			token.value[index] = 0;
			token.type = IntConstant;
	}
	else if (character == '{') {
		token.type = OpenBlock;
	}
	else if (character == '}') {
	token.type = CloseBlock;
	}

	return token;

}	
int main(int argc, char *argv[]) {


	// INPUT OUTPUT DOSYA MANIPULASYONU 
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <input_filename>\n", argv[0]);
		return 1;
	}
	char *input_filename = argv[1]; // KOD DOSYASININ ADI
	char base_filename[256];
	char output_filename[256];
	char *dot_position;
	FILE *inputFile, *outputFile;
	dot_position = strrchr(input_filename, '.');

	size_t base_length = dot_position - input_filename;
	strncpy(base_filename, input_filename, base_length);
	base_filename[base_length] = 0; // NULL TERMINATION
	
	sprintf(output_filename, "%s.lx", base_filename); // lx uzantılı dosya ismi output_filename'de depolanır
	// I/O DOSYA MANIPULASYONU SONU

	inputFile = fopen(input_filename, "r");
	outputFile = fopen(output_filename, "w");

	Token token;
    do {
        token = getNextToken(inputFile);

        switch (token.type) {
            case Keyword:
                fprintf(outputFile, "Keyword(%s)\n", token.value);
                break;
            case Identifier:
                fprintf(outputFile, "Identifier(%s)\n", token.value);
                break;
            case Operator:
                fprintf(outputFile, "Operator(%s)\n", token.value);
                break;
            case IntConstant:
                fprintf(outputFile, "IntConstant(%s)\n", token.value);
                break;
            case StringConstant:
                fprintf(outputFile, "StringConstant(%s)\n", token.value);
                break;
            case EndOfLine:
                fprintf(outputFile, "EndOfLine\n");
                break;
            case OpenBlock:
                fprintf(outputFile, "OpenBlock\n");
                break;
            case CloseBlock:
                fprintf(outputFile, "CloseBlock\n");
                break;
            case EndOfFile:
                break;
        }
    } while (token.type != EndOfFile);

	fclose(inputFile);
	fclose(outputFile);

	printf("Lexical analysis completed. Output written to %s\n", output_filename);

	return 0;
}
