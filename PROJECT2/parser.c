volkan@DESKTOP-IKAUF1J:~/PL_PROJECT$ ./parser myscript.plus
Identifier   num               Line:1    Col:1
MinusAssign  -=                Line:1    Col:5
IntConstant  5                 Line:1    Col:8     Value: 5
EndOfLine    ;                 Line:1    Col:9
EndOfFile    EOF               Line:2    Col:0
Lexical analysis completed.
Total tokens lexed: 5
Computing FIRST and FOLLOW sets...

--- Computed FIRST Sets ---
FIRST(Program): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(S'): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(StatementList): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(Statement): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(Declaration): { NUMBER }
FIRST(Assignment): { IDENTIFIER }
FIRST(Increment): { IDENTIFIER }
FIRST(Decrement): { IDENTIFIER }
FIRST(WriteStatement): { WRITE }
FIRST(OutputList): { NEWLINE INTEGER STRING }
FIRST(ListElement): { NEWLINE INTEGER STRING }
FIRST(LoopStatement): { REPEAT }
FIRST(CodeBlock): { { }
---------------------------

--- Computed FOLLOW Sets ---
FOLLOW(Program): { $ }
FOLLOW(S'): { $ }
FOLLOW(StatementList): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(Statement): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(Declaration): { ; }
FOLLOW(Assignment): { ; }
FOLLOW(Increment): { ; }
FOLLOW(Decrement): { ; }
FOLLOW(WriteStatement): { ; }
FOLLOW(OutputList): { AND ; }
FOLLOW(ListElement): { AND ; }
FOLLOW(LoopStatement): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(CodeBlock): { $ IDENTIFIER WRITE REPEAT NUMBER } }
----------------------------
FIRST and FOLLOW sets computed.
Generating LR(1) item sets...

--- I0 (State 0) Contents ---
  S' -> .Program $ , EndOfFile
  Program -> .StatementList , EndOfFile
  StatementList -> .StatementList Statement , EndOfFile
  StatementList -> .Statement , EndOfFile
  StatementList -> .StatementList Statement , Identifier
  StatementList -> .StatementList Statement , Write
  StatementList -> .StatementList Statement , Repeat
  StatementList -> .StatementList Statement , NumberKeyword
  StatementList -> .Statement , Identifier
  StatementList -> .Statement , Write
  StatementList -> .Statement , Repeat
  StatementList -> .Statement , NumberKeyword
  Statement -> .Assignment ; , EndOfFile
  Statement -> .Declaration ; , EndOfFile
  Statement -> .Decrement ; , EndOfFile
  Statement -> .Increment ; , EndOfFile
  Statement -> .WriteStatement ; , EndOfFile
  Statement -> .LoopStatement , EndOfFile
  Statement -> .Assignment ; , Identifier
  Statement -> .Declaration ; , Identifier
  Statement -> .Decrement ; , Identifier
  Statement -> .Increment ; , Identifier
  Statement -> .WriteStatement ; , Identifier
  Statement -> .LoopStatement , Identifier
  Statement -> .Assignment ; , Write
  Statement -> .Declaration ; , Write
  Statement -> .Decrement ; , Write
  Statement -> .Increment ; , Write
  Statement -> .WriteStatement ; , Write
  Statement -> .LoopStatement , Write
  Statement -> .Assignment ; , Repeat
  Statement -> .Declaration ; , Repeat
  Statement -> .Decrement ; , Repeat
  Statement -> .Increment ; , Repeat
  Statement -> .WriteStatement ; , Repeat
  Statement -> .LoopStatement , Repeat
  Statement -> .Assignment ; , NumberKeyword
  Statement -> .Declaration ; , NumberKeyword
  Statement -> .Decrement ; , NumberKeyword
  Statement -> .Increment ; , NumberKeyword
  Statement -> .WriteStatement ; , NumberKeyword
  Statement -> .LoopStatement , NumberKeyword
  Assignment -> .IDENTIFIER := INTEGER , EndOfLine
  Declaration -> .NUMBER IDENTIFIER , EndOfLine
  Decrement -> .IDENTIFIER -= INTEGER , EndOfLine
  Increment -> .IDENTIFIER += INTEGER , EndOfLine
  WriteStatement -> .WRITE OutputList , EndOfLine
  LoopStatement -> .REPEAT INTEGER TIMES Statement , EndOfFile
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , EndOfFile
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Identifier
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Identifier
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Write
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Write
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Repeat
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Repeat
  LoopStatement -> .REPEAT INTEGER TIMES Statement , NumberKeyword
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , NumberKeyword
------------------------------
LR(1) item sets generated. Total states: 63
Building parsing tables...
Conflict detected (Shift/Reduce or Reduce/Reduce) in state 0 on terminal IDENTIFIER!
  Existing action type: 0, New action type: 0
volkan@DESKTOP-IKAUF1J:~/PL_PROJECT$ cat myscript.plus
num -= 5;
volkan@DESKTOP-IKAUF1J:~/PL_PROJECT$ gcc parser.c lexer.c main.c -o parser
volkan@DESKTOP-IKAUF1J:~/PL_PROJECT$ cat myscript.plus
num -= 5;
volkan@DESKTOP-IKAUF1J:~/PL_PROJECT$ ./parser myscript.plus
Identifier   num               Line:1    Col:1
MinusAssign  -=                Line:1    Col:5
IntConstant  5                 Line:1    Col:8     Value: 5
EndOfLine    ;                 Line:1    Col:9
EndOfFile    EOF               Line:2    Col:0
Lexical analysis completed.
Total tokens lexed: 5
Computing FIRST and FOLLOW sets...

--- Computed FIRST Sets ---
FIRST(Program): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(S'): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(StatementList): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(Statement): { IDENTIFIER WRITE REPEAT NUMBER }
FIRST(Declaration): { NUMBER }
FIRST(Assignment): { IDENTIFIER }
FIRST(Increment): { IDENTIFIER }
FIRST(Decrement): { IDENTIFIER }
FIRST(WriteStatement): { WRITE }
FIRST(OutputList): { NEWLINE INTEGER STRING }
FIRST(ListElement): { NEWLINE INTEGER STRING }
FIRST(LoopStatement): { REPEAT }
FIRST(CodeBlock): { { }
---------------------------

--- Computed FOLLOW Sets ---
FOLLOW(Program): { $ }
FOLLOW(S'): { $ }
FOLLOW(StatementList): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(Statement): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(Declaration): { ; }
FOLLOW(Assignment): { ; }
FOLLOW(Increment): { ; }
FOLLOW(Decrement): { ; }
FOLLOW(WriteStatement): { ; }
FOLLOW(OutputList): { AND ; }
FOLLOW(ListElement): { AND ; }
FOLLOW(LoopStatement): { $ IDENTIFIER WRITE REPEAT NUMBER } }
FOLLOW(CodeBlock): { $ IDENTIFIER WRITE REPEAT NUMBER } }
----------------------------
FIRST and FOLLOW sets computed.
Generating LR(1) item sets...

--- I0 (State 0) Contents ---
  S' -> .Program $ , EndOfFile
  Program -> .StatementList , EndOfFile
  StatementList -> .StatementList Statement , EndOfFile
  StatementList -> .Statement , EndOfFile
  StatementList -> .StatementList Statement , Identifier
  StatementList -> .StatementList Statement , Write
  StatementList -> .StatementList Statement , Repeat
  StatementList -> .StatementList Statement , NumberKeyword
  StatementList -> .Statement , Identifier
  StatementList -> .Statement , Write
  StatementList -> .Statement , Repeat
  StatementList -> .Statement , NumberKeyword
  Statement -> .Assignment ; , EndOfFile
  Statement -> .Declaration ; , EndOfFile
  Statement -> .Decrement ; , EndOfFile
  Statement -> .Increment ; , EndOfFile
  Statement -> .WriteStatement ; , EndOfFile
  Statement -> .LoopStatement , EndOfFile
  Statement -> .Assignment ; , Identifier
  Statement -> .Declaration ; , Identifier
  Statement -> .Decrement ; , Identifier
  Statement -> .Increment ; , Identifier
  Statement -> .WriteStatement ; , Identifier
  Statement -> .LoopStatement , Identifier
  Statement -> .Assignment ; , Write
  Statement -> .Declaration ; , Write
  Statement -> .Decrement ; , Write
  Statement -> .Increment ; , Write
  Statement -> .WriteStatement ; , Write
  Statement -> .LoopStatement , Write
  Statement -> .Assignment ; , Repeat
  Statement -> .Declaration ; , Repeat
  Statement -> .Decrement ; , Repeat
  Statement -> .Increment ; , Repeat
  Statement -> .WriteStatement ; , Repeat
  Statement -> .LoopStatement , Repeat
  Statement -> .Assignment ; , NumberKeyword
  Statement -> .Declaration ; , NumberKeyword
  Statement -> .Decrement ; , NumberKeyword
  Statement -> .Increment ; , NumberKeyword
  Statement -> .WriteStatement ; , NumberKeyword
  Statement -> .LoopStatement , NumberKeyword
  Assignment -> .IDENTIFIER := INTEGER , EndOfLine
  Declaration -> .NUMBER IDENTIFIER , EndOfLine
  Decrement -> .IDENTIFIER -= INTEGER , EndOfLine
  Increment -> .IDENTIFIER += INTEGER , EndOfLine
  WriteStatement -> .WRITE OutputList , EndOfLine
  LoopStatement -> .REPEAT INTEGER TIMES Statement , EndOfFile
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , EndOfFile
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Identifier
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Identifier
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Write
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Write
  LoopStatement -> .REPEAT INTEGER TIMES Statement , Repeat
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , Repeat
  LoopStatement -> .REPEAT INTEGER TIMES Statement , NumberKeyword
  LoopStatement -> .REPEAT INTEGER TIMES CodeBlock , NumberKeyword
------------------------------
LR(1) item sets generated. Total states: 63
Building parsing tables...

--- Debugging Action Table State 0, Token IDENTIFIER ---
Action[0][IDENTIFIER]: Type = 0 (SHIFT=0, REDUCE=1, ACCEPT=2, ERROR=3), Target = 10
----------------------------------------------------------
Parsing tables built.

Attempting to parse sample tokens...

--- Starting Parsing ---
State: 0, Current Token: Identifier ('num', Line:1 Col:1) | Action: SHIFT 10
State: 10, Current Token: MinusAssign ('-=', Line:1 Col:5) | Action: SHIFT 22
State: 22, Current Token: IntConstant ('5', Line:1 Col:8) | Action: SHIFT 32
State: 32, Current Token: EndOfLine (';', Line:1 Col:9) | Action: REDUCE by Assignment -> IDENTIFIER := INTEGER  (Production 11)
State: 4, Current Token: EndOfLine (';', Line:1 Col:9) | Action: SHIFT 16
State: 16, Current Token: EndOfFile ('EOF', Line:2 Col:0) | Action: REDUCE by StatementList -> Statement  (Production 3)
Segmentation fault (core dumped)
