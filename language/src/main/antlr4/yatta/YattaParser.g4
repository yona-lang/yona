parser grammar YattaParser;

options { tokenVocab=YattaLexer; }

@parser::header
{
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Map;

    import com.oracle.truffle.api.Truffle;
    import com.oracle.truffle.api.frame.FrameDescriptor;
    import com.oracle.truffle.api.source.Source;
    import com.oracle.truffle.api.RootCallTarget;
    import yatta.YattaLanguage;
    import yatta.ast.ExpressionNode;
    import yatta.ast.YattaRootNode;
    import yatta.parser.ParseError;
    import yatta.parser.ParserVisitor;
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
            throwParseError(source, line, charPositionInLine, (Token) offendingSymbol, msg);
        }
    }

    private static void throwParseError(Source source, int line, int charPositionInLine, Token token, String message) {
        int col = charPositionInLine + 1;
        String location = "-- line " + line + " col " + col + ": ";
        int length = token == null ? 1 : Math.max(token.getStopIndex() - token.getStartIndex(), 0);
        throw new ParseError(source, line, col, length, String.format("Error(s) parsing script:%n" + location + message));
    }

    public static RootCallTarget parseYatta(YattaLanguage language, Source source) {
        YattaLexer lexer = new YattaLexer(CharStreams.fromString(source.getCharacters().toString()));
        YattaParser parser = new YattaParser(new CommonTokenStream(lexer));
        lexer.removeErrorListeners();
        parser.removeErrorListeners();
        BailoutErrorListener listener = new BailoutErrorListener(source);
        lexer.addErrorListener(listener);
        parser.addErrorListener(listener);
        parser.source = source;
        ExpressionNode rootExpression = new ParserVisitor(language, source).visit(parser.input());
        YattaRootNode rootNode = new YattaRootNode(language, new FrameDescriptor(), rootExpression, source.createSection(1), "root", Truffle.getRuntime().createMaterializedFrame(new Object[] {}));
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
           | doExpr                                     #doExpression
           | lambda                                     #lambdaExpression
           | importExpr                                 #importExpression
           | tryCatchExpr                               #tryCatchExpression
           | raiseExpr                                  #raiseExpression
           | backtickExpr                               #backtickExpression
           ;


literal : booleanLiteral
        | floatLiteral
        | integerLiteral
        | byteLiteral
        | stringLiteral
        | characterLiteral
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

name : LOWERCASE_NAME ;

let : KW_LET NEWLINE? alias+ KW_IN NEWLINE? expression ;
alias : lambdaAlias | moduleAlias | patternAlias | fqnAlias ;
lambdaAlias : name OP_ASSIGN lambda NEWLINE? ;
moduleAlias : name OP_ASSIGN module NEWLINE? ;
patternAlias : pattern OP_ASSIGN expression NEWLINE? ;
fqnAlias : name OP_ASSIGN fqn NEWLINE? ;
conditional : KW_IF ifX=expression KW_THEN thenX=expression KW_ELSE elseX=expression ;
apply : call expression* ;
call : name | moduleCall | nameCall ;
moduleCall : fqn DOT name ;
nameCall : var=name DOT fun=name;
module : KW_MODULE fqn KW_EXPORTS nonEmptyListOfNames KW_AS NEWLINE function+ ;
nonEmptyListOfNames : NEWLINE? name NEWLINE? (COMMA NEWLINE? name)* NEWLINE? ;

unit : UNIT ;
byteLiteral : INTEGER BYTE_SUFFIX;
floatLiteral : FLOAT | (INTEGER FLOAT_SUFFIX);
integerLiteral : INTEGER ;

stringLiteral: INTERPOLATED_REGULAR_STRING_START interpolatedStringPart* DOUBLE_QUOTE_INSIDE ;
interpolatedStringPart
	: interpolatedStringExpression
	| DOUBLE_CURLY_INSIDE
	| REGULAR_CHAR_INSIDE
	| REGULAR_STRING_INSIDE
	;

interpolatedStringExpression
	: interpolationExpression=expression (COMMA alignment=expression)?
	;


characterLiteral : CHARACTER_LITERAL ;
booleanLiteral : KW_TRUE | KW_FALSE ;
function : name pattern* functionBody NEWLINE?;
functionBody : bodyWithoutGuard | bodyWithGuards+ ;

bodyWithoutGuard : NEWLINE? OP_ASSIGN NEWLINE? expression ;
bodyWithGuards : NEWLINE? VLINE guard=expression OP_ASSIGN NEWLINE? expr=expression ;

tuple : PARENS_L expression (COMMA expression)+ PARENS_R ;
dict : CURLY_L (dictKey OP_ASSIGN dictVal (COMMA dictKey OP_ASSIGN dictVal)*)? CURLY_R ;
dictKey : expression ;
dictVal : expression ;
sequence : emptySequence | oneSequence | twoSequence | otherSequence ;

fqn : (packageName BACKSLASH)? moduleName ;
packageName : LOWERCASE_NAME (BACKSLASH LOWERCASE_NAME)* ;
moduleName : UPPERCASE_NAME ;

symbol : COLON name;
identifier : name ;
lambda : BACKSLASH pattern* OP_ARROW expression ;
underscore: UNDERSCORE ;

emptySequence: BRACKET_L BRACKET_R ;
oneSequence: BRACKET_L expression BRACKET_R ;
twoSequence: BRACKET_L expression COMMA expression BRACKET_R ;
otherSequence: BRACKET_L expression COMMA expression (COMMA expression)+ BRACKET_R ;

caseExpr: KW_CASE expression KW_OF NEWLINE? patternExpression+ NEWLINE? KW_END ;
patternExpression : pattern (patternExpressionWithoutGuard | patternExpressionWithGuard+) NEWLINE ;

doExpr : KW_DO NEWLINE? doOneStep+ NEWLINE? KW_END ;
doOneStep : (alias | expression) NEWLINE ;

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
                     | headTailsHead
                     ;
headTails : (patternWithoutSequence CONS_L)+ tails ;
tailsHead :  tails (CONS_R patternWithoutSequence)+ ;

headTailsHead : leftPattern+ tails rightPattern+ ;
leftPattern : patternWithoutSequence CONS_L ;
rightPattern : CONS_R patternWithoutSequence ;

tails : identifier | emptySequence | underscore ;

dictPattern : CURLY_L (patternValue OP_ASSIGN pattern (COMMA patternValue OP_ASSIGN pattern)*)? CURLY_R ;


importExpr : KW_IMPORT NEWLINE? (importClause NEWLINE?)+ KW_IN NEWLINE? expression ;
importClause : moduleImport | functionsImport ;
moduleImport : fqn (KW_AS name)? ;
functionsImport : functionAlias (COMMA functionAlias)* KW_FROM fqn ;
functionAlias : funName=name (KW_AS funAlias=name)? ;


tryCatchExpr : KW_TRY NEWLINE? expression NEWLINE? catchExpr NEWLINE? KW_END ;
catchExpr : KW_CATCH NEWLINE? catchPatternExpression+ ;

catchPatternExpression : (tripplePattern | underscore) (catchPatternExpressionWithoutGuard | catchPatternExpressionWithGuard+) NEWLINE ;
tripplePattern : PARENS_L pattern COMMA pattern COMMA pattern PARENS_R ;
catchPatternExpressionWithoutGuard : NEWLINE? OP_ARROW NEWLINE? expression ;
catchPatternExpressionWithGuard : NEWLINE? VLINE guard=expression OP_ARROW NEWLINE? expr=expression ;

raiseExpr : KW_RAISE symbol stringLiteral NEWLINE ;


backtickExpr : backtickLeft BACKTICK call BACKTICK right=expression ;
backtickLeft : value | PARENS_L expression PARENS_R ;
