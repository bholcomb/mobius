# Mobius Language Grammar

Formal grammar for the Mobius scripting language. This document is the
canonical source of truth for the language syntax. The parser and scanner
implementations should conform to this specification.

**Semantic note:** Variables are type-locked. The type is inferred from the
first non-nil assignment and cannot change. The grammar itself is unchanged —
type locking is enforced at compile time and runtime, not through new syntax.

## Notation

| Symbol       | Meaning                            |
|--------------|------------------------------------|
| `::=`        | is defined as                      |
| `\|`         | alternatives                       |
| `( )`        | grouping                           |
| `[ ]`        | optional (zero or one)             |
| `{ }`        | repetition (zero or more)          |
| `"text"`     | literal terminal                   |
| `UPPER_CASE` | terminal token                     |
| `lower_case` | non-terminal                       |
| `<empty>`    | nothing (explicitly empty)         |

---

## Program

```
program             ::= { NEWLINE | declaration } EOF
```

## Declarations

```
declaration         ::= function_decl
                      | var_decl
                      | enum_decl
                      | statement

function_decl       ::= "func" IDENTIFIER "(" [ param_list ] ")" [ ":" func_type_name ] block

param_list          ::= param { "," param }

param               ::= IDENTIFIER [ ":" func_type_name ]

var_decl            ::= "var" IDENTIFIER [ type_annotation ] [ "=" expression ] terminator

type_annotation     ::= ":" type_name

type_name           ::= "int64" | "uint64" | "float64"

func_type_name      ::= "int" | "integer" | "int64"
                      | "uint64"
                      | "float" | "float64"
                      | "bool" | "boolean"
                      | "string"
                      | "array" | "table"
                      | "function"
                      | "nil"
                      | "userdata"
                      | "enum"

enum_decl           ::= "enum" IDENTIFIER [ ":" integer_type ] "{" enum_body "}" terminator

integer_type        ::= "int64" | "uint64"

enum_body           ::= enum_member { "," enum_member } [ "," ]

enum_member         ::= IDENTIFIER [ "=" expression ]
```

## Statements

```
statement           ::= block
                      | if_stmt
                      | while_stmt
                      | for_stmt
                      | switch_stmt
                      | return_stmt
                      | break_stmt
                      | continue_stmt
                      | import_stmt
                      | pragma_stmt
                      | yield_stmt
                      | expr_stmt

block               ::= "{" { NEWLINE | declaration } "}"

if_stmt             ::= "if" "(" expression ")" statement
                        { "elif" "(" expression ")" statement }
                        [ "else" statement ]

while_stmt          ::= "while" "(" expression ")" statement

for_stmt            ::= "for" "(" for_init ";" [ expression ] ";" [ expression ] ")" statement

for_init            ::= var_decl
                      | expr_stmt
                      | <empty>

switch_stmt         ::= "switch" "(" expression ")" "{" { switch_clause } "}"

switch_clause       ::= case_clause
                      | default_clause

case_clause         ::= "case" case_pattern_list ":" { statement }

case_pattern_list   ::= case_pattern { "," case_pattern }

default_clause      ::= "default" ":" { statement }

case_pattern        ::= expression_pattern
                      | range_pattern
                      | type_pattern
                      | enum_pattern
                      | value_pattern

expression_pattern  ::= comparison_op expression

comparison_op       ::= "==" | "!=" | ">" | ">=" | "<" | "<="

range_pattern       ::= literal ".." literal
                      | literal "..." literal

type_pattern        ::= "is" type_identifier

type_identifier     ::= "nil" | "bool" | "boolean"
                      | "int" | "integer" | "float"
                      | "string"
                      | "array" | "table" | "function"

enum_pattern        ::= IDENTIFIER "." IDENTIFIER

value_pattern       ::= literal

return_stmt         ::= "return" [ expression ] terminator

break_stmt          ::= "break" terminator

continue_stmt       ::= "continue" terminator

import_stmt         ::= "import" STRING [ "as" alias ] terminator

alias               ::= IDENTIFIER { "." IDENTIFIER }
                      | STRING

pragma_stmt         ::= "#" "pragma" IDENTIFIER pragma_value terminator

pragma_value        ::= IDENTIFIER | STRING | "true" | "false"

yield_stmt          ::= "yield" terminator

expr_stmt           ::= expression terminator

terminator          ::= ";" | NEWLINE | EOF
```

## Expressions

Listed from lowest to highest precedence.

```
expression          ::= assignment

assignment          ::= or_expr [ assign_op assignment ]

assign_op           ::= "=" | "+=" | "-=" | "*=" | "/="

or_expr             ::= and_expr { ( "or" | "||" ) and_expr }

and_expr            ::= bitwise_or { ( "and" | "&&" ) bitwise_or }

bitwise_or          ::= bitwise_xor { "|" bitwise_xor }

bitwise_xor         ::= bitwise_and { "^" bitwise_and }

bitwise_and         ::= equality { "&" equality }

equality            ::= comparison { ( "==" | "!=" ) comparison }

comparison          ::= shift { ( ">" | ">=" | "<" | "<=" ) shift }

shift               ::= term { ( "<<" | ">>" ) term }

term                ::= factor { ( "+" | "-" ) factor }

factor              ::= unary { ( "*" | "/" | "%" ) unary }

unary               ::= ( "!" | "-" | "not" | "+" | "~" ) unary
                      | "spawn" postfix "(" [ arg_list ] ")"
                      | "await" unary
                      | "shared" unary
                      | postfix

postfix             ::= primary { call_tail }

call_tail           ::= "(" [ arg_list ] ")"
                      | "[" expression "]"
                      | "." IDENTIFIER
                      | "++" | "--"

arg_list            ::= expression { "," expression }
```

## Primary Expressions

```
primary             ::= literal
                      | IDENTIFIER
                      | table_literal
                      | array_literal
                      | function_expr
                      | "(" expression ")"

function_expr       ::= "func" "(" [ param_list ] ")" [ ":" func_type_name ] block

literal             ::= INTEGER
                      | FLOAT
                      | STRING
                      | CHAR
                      | "true"
                      | "false"
                      | "nil"

table_literal       ::= "{" [ table_entries ] "}"

table_entries       ::= table_entry { "," table_entry } [ "," ]

table_entry         ::= "[" expression "]" "=" expression
                      | IDENTIFIER ":" expression
                      | expression

array_literal       ::= "[" [ array_elements ] "]"

array_elements      ::= expression { "," expression } [ "," ]
```

## Lexical Grammar

```
COMMENT             ::= "//" { <any character except newline> }
                      | "/*" { <any character> } "*/"

INTEGER             ::= DIGIT { DIGIT }
                      | "0x" HEX_DIGIT { HEX_DIGIT }
                      | "0b" BIN_DIGIT { BIN_DIGIT }

FLOAT               ::= DIGIT { DIGIT } "." DIGIT { DIGIT }

STRING              ::= '"' { STRING_CHAR } '"'

CHAR                ::= "'" CHAR_CHAR "'"

IDENTIFIER          ::= ( ALPHA | "_" ) { ALPHA | DIGIT | "_" }

ALPHA               ::= "a".."z" | "A".."Z"
DIGIT               ::= "0".."9"
HEX_DIGIT           ::= DIGIT | "a".."f" | "A".."F"
BIN_DIGIT           ::= "0" | "1"
STRING_CHAR         ::= <any character except '"' and '\'>
                      | "\" ESCAPE_CHAR
CHAR_CHAR           ::= <any character except "'" and '\'>
                      | "\" ESCAPE_CHAR
ESCAPE_CHAR         ::= "n" | "t" | "r" | "\" | '"' | "'" | "0"
```

## Keywords

```
and     await    break    case     continue  default  else   elif
enum    false    for      func     if        import   is
nil     not      or       return   shared    spawn    switch
true    var      while    yield
```

## Operator Precedence (low to high)

| Precedence | Operators                          | Associativity |
|------------|------------------------------------|---------------|
| 1          | `=` `+=` `-=` `*=` `/=`            | right         |
| 2          | `or` `\|\|`                        | left          |
| 3          | `and` `&&`                         | left          |
| 4          | `\|` (bitwise or)                  | left          |
| 5          | `^` (bitwise xor)                  | left          |
| 6          | `&` (bitwise and)                  | left          |
| 7          | `==` `!=`                          | left          |
| 8          | `<` `<=` `>` `>=`                  | left          |
| 9          | `<<` `>>` (shift)                  | left          |
| 10         | `+` `-`                            | left          |
| 11         | `*` `/` `%`                        | left          |
| 12         | `!` `-` `not` `+` `~` (unary)      | right         |
| 13         | `()` `[]` `.` `++` `--` (postfix)  | left          |
