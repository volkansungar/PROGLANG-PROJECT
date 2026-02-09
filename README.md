# **Plus++ Language Interpreter & Lexical Analyzer**

## **📖 Overview**

**Plus++** is a custom programming language designed specifically for handling **Big Integer arithmetic** (up to 100 decimal digits) and control flow structures.

This project is a complete implementation of the language's toolchain in **C**, consisting of:

1. **Lexical Analyzer (Scanner):** A Finite State Machine (FSM) based tokenizer.  
2. **Parser:** A powerful LALR(1) bottom-up parser that generates an Abstract Syntax Tree (AST).  
3. **Interpreter:** A runtime engine that executes the AST, handling custom memory structures for large numbers.

This project was developed as part of the Programming Languages Course at Ege University.

## **✨ Key Features**

### **🧠 Core Architecture**

* **Three-Stage Pipeline:** Lexical Analysis ![][image1] Parsing ![][image1] Code Generation/Execution.  
* **BigInt Support:** Custom C structs and algorithms to handle integers up to **100 digits**, overcoming standard C data type limitations.  
* **AST Generation:** Constructs a dynamic Abstract Syntax Tree for code traversal.

### **🔍 Lexical Analyzer**

* **FSM Implementation:** Uses a Deterministic Finite Automaton (DFA) for efficient token recognition.  
* **Symbol Table:** Hash-based lookup for identifiers and keywords.  
* **Buffered I/O:** Efficient file reading for large scripts.  
* **Error Handling:** Precise location tracking (Line:Col) for lexical errors (e.g., unclosed strings, invalid characters).

### **⚙️ Parser (LALR 1\)**

* **Robust Grammar:** Handles complex nesting and eliminates shift-reduce conflicts.  
* **Dynamic Table Generation:** Generates FIRST sets, FOLLOW sets, and LR(1) items at runtime (educational design choice).  
* **Recovery:** Detailed syntax error reporting with state analysis.

## **📝 The Plus++ Language Specification**

Plus++ is designed to be simple yet powerful for arithmetic sequences.

### **Keywords**

number, write, repeat, times, newline, and

### **Operators**

* Assignment: :=  
* Increment: \+=  
* Decrement: \-=

### **Syntax Example**

\* This is a comment in Plus++ \*  
number counter;  
number result;

counter := 5;  
result := 0;

write "Starting Loop..." and newline;

repeat counter times {  
    result \+= 10;  
    write "Current result: " and result and newline;  
}

write "Final Calculation: " and result;

## **🛠️ Technical Implementation Details**

### **1\. The Lexer**

The lexer reads the source file .ppp and converts raw characters into a stream of tokens. It handles:

* **Identifiers:** var\_name, count1  
* **String Constants:** "Hello World"  
* **Integer Constants:** 123, \-99  
* **Comments:** \* comment \*

### **2\. The Parser**

The parser uses a **Look-Ahead Left-to-Right Rightmost Derivation (LALR 1\)** strategy.

* **Structs:** Uses GrammarSymbol, Production, and LR1\_Item structs.  
* **Logic:** Implements CLOSURE and GOTO algorithms to build the state machine.  
* **Output:** Returns a pointer to the root of the Abstract Syntax Tree (AST).

### **3\. BigInt Arithmetic**

Since standard long long in C cannot hold 100 digits, the project uses a custom BigInt structure:

* Numbers are stored as arrays of "limbs".  
* Custom add and subtract functions handle carry/borrow logic manually.

## **🚀 Building and Running**

### **Prerequisites**

* GCC Compiler  
* Make (optional, if Makefile is included)

### **Compilation**

To compile the entire interpreter suite:

gcc main.c lexer.c parser.c interpreter.c \-o plusplus

*(Note: Adjust filenames based on the actual source file organization).*

### **Usage**

Create a file named script.ppp with Plus++ code, then run:

./plusplus script.ppp

## **🧪 Example Programs**

### **1\. Simple Calculator**

number a;  
number b;  
number res;

a := 100000000000000000000; \* Big Integer \*  
b := 5;

res := a;  
res \+= b;

write "Result is: " and res;

### **2\. Nested Loops**

number i;  
number j;  
i := 3;  
j := 2;

repeat i times {  
    repeat j times {  
        write "Outer: " and i and " Inner: " and j and newline;  
    }  
}  


[image1]: <data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABMAAAAXCAYAAADpwXTaAAAAeUlEQVR4XmNgGAWjgHpAQUFBAF2MbCAnJ7dTXV2dF12cLAA0rByI56OLkwWkpaWFgV49hC5ONpCVlTUBYj8gkxFFQl5e/j+F+BuKgeQAoCE3gHgyyNvockQDZWVlMaAhp9HFyQJUjU2gQSeBLrNEFycLUDUHjALqAQBTliW9QFvnvQAAAABJRU5ErkJggg==>