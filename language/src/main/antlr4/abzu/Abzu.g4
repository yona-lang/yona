grammar Abzu;

tokens { INDENT, DEDENT }

@parser::header
{
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Map;

    import com.oracle.truffle.api.Truffle;
    import com.oracle.truffle.api.frame.FrameDescriptor;
    import com.oracle.truffle.api.source.Source;
    import com.oracle.truffle.api.RootCallTarget;
    import abzu.AbzuLanguage;
    import abzu.ast.ExpressionNode;
    import abzu.ast.AbzuRootNode;
    import abzu.parser.AbzuParseError;
    import abzu.parser.ParserVisitor;
}

@parser::members
{
    private Source source;

    private static final class BailoutErrorListener extends BaseErrorListener {
        private final Source source;
        BailoutErrorListener(Source source) {
            this.source = source;
        }
        @Override
        public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol, int line, int charPositionInLine, String msg, RecognitionException e) {
            String location = "-- line " + line + " col " + (charPositionInLine + 1) + ": ";
            throw new AbzuParseError(source, line, charPositionInLine + 1, offendingSymbol == null ? 1 : ((Token) offendingSymbol).getText().length(), "Error(s) parsing script:\n" + location + msg);
        }
    }

    public void SemErr(Token token, String message) {
        int col = token.getCharPositionInLine() + 1;
        String location = "-- line " + token.getLine() + " col " + col + ": ";
        throw new AbzuParseError(source, token.getLine(), col, token.getText().length(), "Error(s) parsing script:\n" + location + message);
    }

    public static RootCallTarget parseAbzu(AbzuLanguage language, Source source) {
        AbzuLexer lexer = new AbzuLexer(CharStreams.fromString(source.getCharacters().toString()));
        AbzuParser parser = new AbzuParser(new CommonTokenStream(lexer));
        lexer.removeErrorListeners();
        parser.removeErrorListeners();
        BailoutErrorListener listener = new BailoutErrorListener(source);
        lexer.addErrorListener(listener);
        parser.addErrorListener(listener);
        parser.source = source;
        ExpressionNode rootExpression = new ParserVisitor(language, source).visit(parser.input());
        AbzuRootNode rootNode = new AbzuRootNode(language, new FrameDescriptor(), rootExpression, source.createSection(1), "root", Truffle.getRuntime().createMaterializedFrame(new Object[] {}));
        return Truffle.getRuntime().createCallTarget(rootNode);
    }
}

input : NEWLINE? expression NEWLINE? EOF ;

expression : PARENS_L expression PARENS_R               #expressionInParents
           | left=expression BIN_OP right=expression    #binaryOperationExpression
           | UN_OP expression                           #unaryOperationExpression
           | let                                        #letExpression
           | conditional                                #conditionalExpression
           | value                                      #valueExpression
           | module                                     #moduleExpression
           | apply                                      #functionApplicationExpression
           | caseExpr                                   #caseExpression
           | lambda                                     #lambdaExpression
           ;

literal : booleanLiteral
        | floatLiteral
        | integerLiteral
        | byteLiteral
        | stringLiteral
        ;

value : unit
      | literal
      | tuple
      | dict
      | sequence
      | symbol
      | identifier
      ;

patternValue : unit
             | literal
             | symbol
             | identifier
             ;

let : KW_LET NEWLINE? alias+ KW_IN NEWLINE? expression ;
alias : lambdaAlias | moduleAlias | patternAlias | fqnAlias ;
lambdaAlias : NAME OP_ASSIGN lambda NEWLINE? ;
moduleAlias : NAME OP_ASSIGN module NEWLINE? ;
patternAlias : pattern OP_ASSIGN expression NEWLINE? ;
fqnAlias : NAME OP_ASSIGN fqn NEWLINE? ;
conditional : KW_IF ifX=expression KW_THEN thenX=expression KW_ELSE elseX=expression ;
apply : (NAME | moduleCall) expression* ;
moduleCall : fqn DOT NAME ;
module : KW_MODULE fqn KW_EXPORTS nonEmptyListOfNames KW_AS NEWLINE function+ ;
nonEmptyListOfNames : NEWLINE? NAME NEWLINE? (COMMA NEWLINE? NAME)* NEWLINE? ;

unit : UNIT ;
byteLiteral : INTEGER 'b';
integerLiteral : INTEGER ;
floatLiteral : FLOAT | INTEGER 'f';
stringLiteral : STRING ;
booleanLiteral : KW_TRUE | KW_FALSE ;
function : NAME pattern* functionBody NEWLINE?;
functionBody : bodyWithoutGuard | bodyWithGuards+ ;

bodyWithoutGuard : NEWLINE? OP_ASSIGN NEWLINE? expression ;
bodyWithGuards : NEWLINE? VLINE guard=expression OP_ASSIGN NEWLINE? expr=expression ;

tuple : PARENS_L expression (COMMA expression)+ PARENS_R ;
dict : CURLY_L (dictKey OP_ASSIGN dictVal (COMMA dictKey OP_ASSIGN dictVal)*)? CURLY_R ;
dictKey : expression ;
dictVal : expression ;
sequence : emptySequence | oneSequence | twoSequence | otherSequence ;
fqn : NAME (SLASH NAME)* ;
symbol : COLON NAME;
identifier : NAME ;
lambda : LAMBDA_START pattern* OP_ARROW expression ;
underscore: UNDERSCORE ;

emptySequence: BRACKET_L BRACKET_R ;
oneSequence: BRACKET_L expression BRACKET_R ;
twoSequence: BRACKET_L expression COMMA expression BRACKET_R ;
otherSequence: BRACKET_L expression COMMA expression (COMMA expression)+ BRACKET_R ;

caseExpr: KW_CASE expression KW_OF NEWLINE? patternExpression+ ;
patternExpression : pattern (patternExpressionWithoutGuard | patternExpressionWithGuard+) NEWLINE ;

patternExpressionWithoutGuard : NEWLINE? OP_ARROW NEWLINE? expression ;
patternExpressionWithGuard : NEWLINE? VLINE guard=expression OP_ARROW NEWLINE? expr=expression ;

pattern : underscore
        | patternValue
        | tuplePattern
        | sequencePattern
        | dictPattern
        ;

patternWithoutSequence: underscore
                      | patternValue
                      | tuplePattern
                      | dictPattern
                      ;

tuplePattern : PARENS_L pattern (COMMA pattern)+ PARENS_R ;
sequencePattern : identifier AT PARENS_L innerSequencePattern PARENS_R
                | innerSequencePattern
                ;
innerSequencePattern : BRACKET_L (pattern (COMMA pattern)*)? BRACKET_R
                     | headTails
                     | tailsHead
                     ;
headTails : (patternWithoutSequence CONS_L)+ tails ;
tailsHead :  tails (CONS_R patternWithoutSequence)+ ;
tails : identifier | emptySequence | underscore ;

dictPattern : CURLY_L (patternValue OP_ASSIGN pattern (COMMA patternValue OP_ASSIGN pattern)*)? CURLY_R ;

UNIT: '()' ;
UNDERSCORE : '_' ;
AT : '@' ;

// Keywords
KW_LET : 'let' ;
KW_IN : 'in' ;
KW_IF : 'if' ;
KW_THEN : 'then' ;
KW_ELSE : 'else' ;
KW_TRUE : 'true' ;
KW_FALSE : 'false' ;
KW_MODULE : 'module' ;
KW_EXPORTS : 'exports' ;
KW_AS : 'as' ;
KW_CASE : 'case' ;
KW_OF : 'of' ;

BRACKET_L : '[' ;
BRACKET_R : ']' ;
PARENS_L : '(' ;
PARENS_R : ')' ;
CURLY_L : '{' ;
CURLY_R : '}' ;

COMMA : ',' ;
COLON : ':' ;

CONS_L : '<:' ;
CONS_R : ':>' ;

DOT : '.' ;
SLASH : OP_DIVIDE ;
VLINE : '|';

LAMBDA_START : '\\' ;

// Data
STRING: '"' ('\\"'|.)*? '"' ;
NAME : [a-zA-Z_]+ ;
INTEGER : '-'?[0-9]+ ;
FLOAT : ('0' .. '9') + ('.' ('0' .. '9') +)? ;

// Operators
BIN_OP : OP_COMPARISON | OP_ARITHMETIC | OP_LIST;
UN_OP: OP_NOT;

OP_ASSIGN : '=';
OP_EQ : '==' ;
OP_NEQ : '!=' ;
OP_LT : '<' ;
OP_LTE : '<=' ;
OP_GT : '>' ;
OP_GTE : '>=';
OP_NOT : '!' ;
OP_ARROW : '->' ;

OP_COMPARISON : OP_EQ | OP_NEQ | OP_LT | OP_LTE | OP_GT | OP_GTE ;

OP_PLUS : '+' ;
OP_MINUS : '-';
OP_MULTIPLY : '*';
OP_DIVIDE : '/';
OP_MODULO : '%';

OP_ARITHMETIC : OP_PLUS | OP_MINUS | OP_MULTIPLY | OP_DIVIDE | OP_MODULO ;

OP_CONS : '::';
OP_JOIN : '++';

OP_LIST :  OP_CONS | OP_JOIN ;

NEWLINE: ('\r'? '\n')+ ;

fragment COMMENT : NEWLINE? '#' ~[\r\n\f]* ;
fragment SPACES : [ \t]+ ;

WS: (COMMENT | SPACES) -> skip ;
