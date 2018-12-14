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
    import abzu.ast.ParserVisitor;
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

input : expression EOF ;

expression : left=expression BIN_OP right=expression    #binaryOperationExpression
           | UN_OP expression                           #unaryOperationExpression
           | let                                        #letExpression
           | conditional                                #conditionalExpression
           | value                                      #valueExpression
           | module                                     #moduleExpression
           | apply                                      #functionApplicationExpression
           ;

value : unit
      | booleanLiteral
      | integerLiteral
      | floatLiteral
      | byteLiteral
      | stringLiteral
      | function
      | lambda
      | tuple
      | dict
      | list
      | symbol
      | identifier
      | fqn
      ;

let : KW_LET alias+ KW_IN expression ;
alias : NAME OP_PMATCH expression ;
conditional : KW_IF ifX=expression KW_THEN thenX=expression KW_ELSE elseX=expression ;
apply : (NAME | moduleCall) expression* ;
moduleCall : fqn DOT NAME ;
module : KW_MODULE fqn KW_EXPORTS nonEmptyListOfNames KW_AS function+ ;
nonEmptyListOfNames : NAME (COMMA NAME)* ;

unit : UNIT ;
byteLiteral : INTEGER 'b';
integerLiteral : INTEGER ;
floatLiteral : FLOAT | INTEGER 'f';
stringLiteral : STRING ;
booleanLiteral : KW_TRUE | KW_FALSE ;
function : NAME arg* OP_ASSIGN expression;
arg : NAME ;
tuple : PARENS_L (expression (COMMA expression)*)? PARENS_R ;
dict : key COLON expression (COMMA key COLON expression)* ;
key : STRING ;
list : BRACKET_L expression? (COMMA expression)* BRACKET_R ;
fqn : NAME (SLASH NAME)* ;
symbol : COLON NAME;
identifier : NAME ;
lambda : LAMBDA_START arg* OP_ARROW expression ;

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

BRACKET_L : '[' ;
BRACKET_R : ']' ;
PARENS_L : '(' ;
PARENS_R : ')' ;

COMMA : ',' ;
COLON : ':' ;
DOT : '.' ;
SLASH : OP_DIVIDE ;

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
OP_PMATCH : ':=';
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

UNIT: '()' ;

WS: [ \r\n\t]+ -> skip;
