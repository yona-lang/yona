grammar Abzu;

tokens { INDENT, DEDENT }

@lexer::members {
    // A queue where extra tokens are pushed on (see the NEWLINE lexer rule).
    private java.util.LinkedList<Token> tokens = new java.util.LinkedList<>();
    // The stack that keeps track of the indentation level.
    private java.util.Stack<Integer> indents = new java.util.Stack<>();
    // The amount of opened braces, brackets and parenthesis.
    private int opened = 0;
    // The most recently produced token.
    private Token lastToken = null;
    @Override
    public void emit(Token t) {
    super.setToken(t);
    tokens.offer(t);
    }

    @Override
    public Token nextToken() {
    // Check if the end-of-file is ahead and there are still some DEDENTS expected.
    if (_input.LA(1) == EOF && !this.indents.isEmpty()) {
      // Remove any trailing EOF tokens from our buffer.
      for (int i = tokens.size() - 1; i >= 0; i--) {
        if (tokens.get(i).getType() == EOF) {
          tokens.remove(i);
        }
      }

      // First emit an extra line break that serves as the end of the statement.
      this.emit(commonToken(AbzuParser.NEWLINE, "\n"));

      // Now emit as much DEDENT tokens as needed.
      while (!indents.isEmpty()) {
        this.emit(createDedent());
        indents.pop();
      }

      // Put the EOF back on the token stream.
      this.emit(commonToken(AbzuParser.EOF, "<EOF>"));
    }

    Token next = super.nextToken();

    if (next.getChannel() == Token.DEFAULT_CHANNEL) {
      // Keep track of the last token on the default channel.
      this.lastToken = next;
    }

    return tokens.isEmpty() ? next : tokens.poll();
    }

    private Token createDedent() {
    CommonToken dedent = commonToken(AbzuParser.DEDENT, "");
    dedent.setLine(this.lastToken.getLine());
    return dedent;
    }

    private CommonToken commonToken(int type, String text) {
    int stop = this.getCharIndex() - 1;
    int start = text.isEmpty() ? stop : stop - text.length() + 1;
    return new CommonToken(this._tokenFactorySourcePair, type, DEFAULT_TOKEN_CHANNEL, start, stop);
    }

    // Calculates the indentation of the provided spaces, taking the
    // following rules into account:
    //
    // "Tabs are replaced (from left to right) by one to eight spaces
    //  such that the total number of characters up to and including
    //  the replacement is a multiple of eight [...]"
    //
    //  -- https://docs.python.org/3.1/reference/lexical_analysis.html#indentation
    static int getIndentationCount(String spaces) {
    int count = 0;
    for (char ch : spaces.toCharArray()) {
      switch (ch) {
        case '\t':
          count += 8 - (count % 8);
          break;
        default:
          // A normal space char.
          count++;
      }
    }

    return count;
    }

    boolean atStartOfInput() {
    return super.getCharPositionInLine() == 0 && super.getLine() == 1;
    }
}

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
    import abzu.ast.AbzuExpressionNode;
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
        AbzuExpressionNode rootExpression = new ParserVisitor().visit(parser.input());
        AbzuRootNode rootNode = new AbzuRootNode(language, new FrameDescriptor(), rootExpression, source.createSection(1), "root");
        return Truffle.getRuntime().createCallTarget(rootNode);
    }
}

input : NEWLINE* expression NEWLINE* EOF ;

expression : left=expression BIN_OP right=expression    #binaryOperationExpression
           | UN_OP expression                           #unaryOperationExpression
           | let                                        #letExpression
           | conditional                                #conditionalExpression
           | apply                                      #functionApplicationExpression
           | value                                      #valueExpression
           | module                                     #moduleExpression
           ;

value : unit
      | booleanLiteral
      | integerLiteral
      | floatLiteral
      | byteLiteral
      | stringLiteral
      | function
      | tuple
      | dict
      | list
      | symbol
      ;

let : KW_LET alias+ KW_IN expression ;
alias : NAME OP_ASSIGN expression ;
conditional : KW_IF ifX=expression KW_THEN thenX=expression KW_ELSE elseX=expression ;
apply : NAME expression* ;
module : KW_MODULE fqn KW_EXPORTS nonEmptyListOfNames KW_AS function+ ;
nonEmptyListOfNames : NAME (COMMA expression)* ;

unit : UNIT ;
byteLiteral : INTEGER 'b';
integerLiteral : INTEGER ;
floatLiteral : FLOAT | INTEGER 'f';
stringLiteral : STRING ;
booleanLiteral : KW_TRUE | KW_FALSE ;
function : NAME arg* OP_ASSIGN NEWLINE INDENT expression DEDENT ;
arg : NAME ;
tuple : PARENS_L (expression (COMMA expression)*)? PARENS_R ;
dict : key COLON expression (COMMA key COLON expression)* ;
key : STRING ;
list : BRACKET_L expression? (COMMA expression)* BRACKET_R ;
fqn : NAME (DOT NAME)* ; // TODO add uppercase/lowercase rules here
symbol : COLON NAME;

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

// Data
STRING: '"' ('\\"'|.)*? '"' ;
NAME : [a-zA-Z_]+ ;
INTEGER : '-'?[0-9]+ ;
FLOAT : ('0' .. '9') + ('.' ('0' .. '9') +)? ;

// Operators
BIN_OP : OP_COMPARISON | OP_ARITHETIC | OP_LIST;
UN_OP: OP_NOT;

OP_ASSIGN : '=';
OP_EQ : '==' ;
OP_NEQ : '!=' ;
OP_LT : '<' ;
OP_LTE : '<=' ;
OP_GT : '>' ;
OP_GTE : '>=';
OP_NOT : '!' ;

OP_COMPARISON : OP_EQ | OP_NEQ | OP_LT | OP_LTE | OP_GT | OP_GTE | OP_NOT ;

OP_PLUS : '+' ;
OP_MINUS : '-';
OP_MULTIPLY : '*';
OP_DIVIDE : '/';
OP_MODULO : '%';

OP_ARITHETIC : OP_PLUS | OP_MINUS | OP_MULTIPLY | OP_DIVIDE | OP_MODULO ;

OP_CONS : '::';
OP_JOIN : '++';

OP_LIST :  OP_CONS | OP_JOIN ;

UNIT: '()' ;

fragment SPACES
 : [ \t]+
 ;

fragment COMMENT
 : '#' ~[\r\n\f]*
 ;

fragment LINE_JOINING
 : '\\' SPACES? ( '\r'? '\n' | '\r' | '\f')
;
SKIP_
 : ( SPACES | COMMENT | LINE_JOINING ) -> skip
;

NEWLINE
 : ( {atStartOfInput()}?   SPACES
   | ( '\r'? '\n' | '\r' | '\f' ) SPACES?
   )
   {
     String newLine = getText().replaceAll("[^\r\n\f]+", "");
     String spaces = getText().replaceAll("[\r\n\f]+", "");
     int next = _input.LA(1);
     if (opened > 0 || next == '\r' || next == '\n' || next == '\f' || next == '#') {
       // If we're inside a list or on a blank line, ignore all indents,
       // dedents and line breaks.
       skip();
     }
     else {
       emit(commonToken(NEWLINE, newLine));
       int indent = getIndentationCount(spaces);
       int previous = indents.isEmpty() ? 0 : indents.peek();
       if (indent == previous) {
         // skip indents of the same size as the present indent-size
         skip();
       }
       else if (indent > previous) {
         indents.push(indent);
         emit(commonToken(AbzuParser.INDENT, spaces));
       }
       else {
         // Possibly emit more than 1 DEDENT token.
         while(!indents.isEmpty() && indents.peek() > indent) {
           this.emit(createDedent());
           indents.pop();
         }
       }
     }
   }
;
