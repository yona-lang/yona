parser grammar YattaParser;

options { tokenVocab=YattaLexer; }

@parser::header
{
    import java.util.*;

    import com.oracle.truffle.api.Truffle;
    import com.oracle.truffle.api.frame.FrameDescriptor;
    import com.oracle.truffle.api.source.Source;
    import com.oracle.truffle.api.RootCallTarget;
    import yatta.YattaLanguage;
    import yatta.ast.ExpressionNode;
    import yatta.ast.FunctionRootNode;
    import yatta.parser.ParseError;
    import yatta.parser.ParserVisitor;
    import yatta.runtime.Context;
}

@parser::members
{
    private Source source;

    private static final class YattaErrorListener extends BaseErrorListener {
        private final Source source;
        YattaErrorListener(Source source) {
            this.source = source;
        }
        @Override
        public void syntaxError(Recognizer<?, ?> recognizer, Object offendingSymbol, int line, int charPositionInLine, String msg, RecognitionException e) {
            throwParseError(source, recognizer, (Token) offendingSymbol, line, charPositionInLine, msg);
        }
    }

    public static final class YattaErrorStrategy extends DefaultErrorStrategy {
        @Override
        public void reportNoViableAlternative(Parser parser, NoViableAltException e) throws RecognitionException {
            Recognizer recognizer = e.getRecognizer();
            StringBuilder msg = new StringBuilder();
            List<String> stack = parser.getRuleInvocationStack();

            Collections.reverse(stack);

            msg.append("can't choose next alternative. Parser stack: [");
            for(String item : stack) {
                msg.append(item);
                msg.append(", ");
            }
            if(stack.size() > 0) {
                msg.delete(msg.length() - 2, msg.length());
            }
            msg.append("]. Valid alternatives are: ");
            msg.append(e.getExpectedTokens().toString(recognizer.getVocabulary()));

            parser.notifyErrorListeners(e.getOffendingToken(), msg.toString(), e);
        }
    }

    private static void throwParseError(Source source, Recognizer recognizer, Token offendingToken, int line, int charPositionInLine, String syntaxErrMsg) {
        StringBuilder msg = new StringBuilder();
        msg.append("Error(s) parsing script:\n");
        msg.append("-- line ");
        msg.append(line);
        msg.append(": ");
        msg.append(charPositionInLine);
        msg.append(" ");
        msg.append(syntaxErrMsg);
        msg.append("\n");
        CommonTokenStream tokens = (CommonTokenStream) recognizer.getInputStream();
        String input = tokens.getTokenSource().getInputStream().toString();
        String[] lines = input.split("\n");
        String errorLine = lines[line - 1];
        msg.append(errorLine);
        msg.append("\n");
        for(int i = 0; i < charPositionInLine; i++) {
            msg.append(" ");
        }
        int length;
        if(offendingToken != null) {
            int start = offendingToken.getStartIndex();
            int stop = offendingToken.getStopIndex();
            if (start >= 0 && stop >= 0) {
                for (int i = start; i <= stop; i++) {
                    msg.append("^");
                }
            }
            length = Math.max(stop - start, 0);
        } else {
            length = 0;
        }
        throw new ParseError(source, line, charPositionInLine + 1, length, msg.toString());
    }

    public static RootCallTarget parseYatta(YattaLanguage language, Context context, Source source) {
        YattaLexer lexer = new YattaLexer(CharStreams.fromString(source.getCharacters().toString()));
        YattaParser parser = new YattaParser(new CommonTokenStream(lexer));
        lexer.removeErrorListeners();
        parser.removeErrorListeners();
        YattaErrorListener listener = new YattaErrorListener(source);
        lexer.addErrorListener(listener);
        parser.addErrorListener(listener);
        parser.setErrorHandler(new YattaErrorStrategy());
        parser.source = source;
        ExpressionNode rootExpression = new ParserVisitor(language, context, source).visit(parser.input());
        FunctionRootNode rootNode = new FunctionRootNode(language, context.globalFrameDescriptor, rootExpression, source.createSection(1), null, "root");
        return Truffle.getRuntime().createCallTarget(rootNode);
    }
}

input : NEWLINE? expression NEWLINE? EOF ;

function : NEWLINE* name pattern* NEWLINE? functionBody ;
functionBody : bodyWithGuards+ | bodyWithoutGuard ;

bodyWithGuards : NEWLINE? VLINE guard=expression OP_ASSIGN NEWLINE? expr=expression NEWLINE ;
bodyWithoutGuard : NEWLINE? OP_ASSIGN NEWLINE? expression NEWLINE ;

expression : PARENS_L expression PARENS_R                                                                #expressionInParents
           | op=(OP_LOGIC_NOT | OP_BIN_NOT) expression                                                   #negationExpression
           | left=expression BACKTICK call BACKTICK right=expression                                     #backtickExpression
           | left=expression op=(OP_POWER | OP_MULTIPLY | OP_DIVIDE | OP_MODULO) right=expression        #multiplicativeExpression
           | left=expression op=(OP_PLUS | OP_MINUS) right=expression                                    #additiveExpression
           | left=expression op=(OP_LEFTSHIFT | OP_RIGHTSHIFT | OP_ZEROFILL_RIGHTSHIFT) right=expression #binaryShiftExpression
           | left=expression op=(OP_GTE | OP_LTE| OP_GT | OP_LT | OP_EQ | OP_NEQ) right=expression       #comparativeExpression
           | <assoc=right> left=expression OP_CONS_L right=expression                                    #consLeftExpression
           | left=expression OP_CONS_R right=expression                                                  #consRightExpression
           | <assoc=right> left=expression OP_JOIN right=expression                                      #joinExpression
           | left=expression OP_BIN_AND right=expression                                                 #bitwiseAndExpression
           | left=expression OP_BIN_XOR right=expression                                                 #bitwiseXorExpression
           | left=expression VLINE right=expression                                                      #bitwiseOrExpression
           | left=expression OP_LOGIC_AND right=expression                                               #logicalAndExpression
           | left=expression OP_LOGIC_OR right=expression                                                #logicalOrExpression
           | left=expression KW_IN right=expression                                                      #inExpression
           | let                                                                                         #letExpression
           | conditional                                                                                 #conditionalExpression
           | value                                                                                       #valueExpression
           | apply                                                                                       #functionApplicationExpression
           | caseExpr                                                                                    #caseExpression
           | doExpr                                                                                      #doExpression
           | importExpr                                                                                  #importExpression
           | tryCatchExpr                                                                                #tryCatchExpression
           | raiseExpr                                                                                   #raiseExpression
           | generatorExpr                                                                               #generatorExpression
           | <assoc=right> left=expression NEWLINE? OP_PIPE_L right=expression                           #pipeLeftExpression
           | left=expression NEWLINE? OP_PIPE_R right=expression                                         #pipeRightExpression
           | fieldAccessExpr                                                                             #fieldAccessExpression
           | fieldUpdateExpr                                                                             #fieldUpdateExpression
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
      | set
      | symbol
      | identifier
      | fqn
      | lambda
      | module
      | recordInstance
      ;

patternValue : unit
             | literal
             | symbol
             | identifier
             ;

name : LOWERCASE_NAME ;

let : KW_LET NEWLINE? alias+ KW_IN NEWLINE? expression ;
alias : lambdaAlias | moduleAlias | valueAlias | patternAlias | fqnAlias ;
lambdaAlias : name OP_ASSIGN lambda NEWLINE? ;
moduleAlias : name OP_ASSIGN module NEWLINE? ;
valueAlias : identifier OP_ASSIGN expression NEWLINE? ;
patternAlias : pattern OP_ASSIGN expression NEWLINE? ;
fqnAlias : name OP_ASSIGN fqn NEWLINE? ;
conditional : KW_IF ifX=expression NEWLINE? KW_THEN NEWLINE? thenX=expression NEWLINE? KW_ELSE NEWLINE? elseX=expression ;
apply : call funArg* ;
funArg : PARENS_L expression PARENS_R | value;
call : name | moduleCall | nameCall ;
moduleCall : fqn DCOLON name ;
nameCall : var=name DCOLON fun=name;
module : NEWLINE* KW_MODULE fqn KW_EXPORTS nonEmptyListOfNames KW_AS NEWLINE record* function+ NEWLINE? KW_END ;
nonEmptyListOfNames : NEWLINE? name NEWLINE? (COMMA NEWLINE? name)* NEWLINE? ;

unit : UNIT ;
byteLiteral : BYTE;
floatLiteral : FLOAT | FLOAT_INTEGER;
integerLiteral : INTEGER ;

stringLiteral: INTERPOLATED_REGULAR_STRING_START interpolatedStringPart* DOUBLE_QUOTE_INSIDE ;
interpolatedStringPart
	: interpolatedStringExpression
	| DOUBLE_CURLY_INSIDE
	| DOUBLE_CURLY_CLOSE_INSIDE
	| REGULAR_CHAR_INSIDE
	| REGULAR_STRING_INSIDE
	;

interpolatedStringExpression
	: interpolationExpression=expression (COMMA alignment=expression)?
	;


characterLiteral : CHARACTER_LITERAL ;
booleanLiteral : KW_TRUE | KW_FALSE ;

tuple : PARENS_L expression NEWLINE? (COMMA NEWLINE? expression)+ PARENS_R ;
dict : CURLY_L (dictKey OP_ASSIGN dictVal (COMMA dictKey OP_ASSIGN dictVal)*)? CURLY_R ;
dictKey : expression ;
dictVal : expression ;
sequence : emptySequence | otherSequence ;
set : CURLY_L expression (COMMA expression)* CURLY_R ;

fqn : (packageName BACKSLASH)? moduleName ;
packageName : LOWERCASE_NAME (BACKSLASH LOWERCASE_NAME)* ;
moduleName : UPPERCASE_NAME ;

symbol : COLON name;
identifier : name ;
lambda : BACKSLASH pattern* OP_RIGHT_ARROW NEWLINE? expression ;
underscore: UNDERSCORE ;

emptySequence: BRACKET_L BRACKET_R ;
otherSequence: BRACKET_L expression (COMMA expression)* BRACKET_R ;

caseExpr: KW_CASE expression KW_OF NEWLINE? patternExpression+ NEWLINE? KW_END ;
patternExpression : pattern (patternExpressionWithoutGuard | patternExpressionWithGuard+) NEWLINE ;

doExpr : KW_DO NEWLINE? doOneStep+ NEWLINE? KW_END ;
doOneStep : (alias | expression) NEWLINE ;

patternExpressionWithoutGuard : NEWLINE? OP_RIGHT_ARROW NEWLINE? expression ;
patternExpressionWithGuard : NEWLINE? VLINE guard=expression OP_RIGHT_ARROW NEWLINE? expr=expression ;

pattern : PARENS_L pattern PARENS_R
        | underscore
        | patternValue
        | dataStructurePattern
        | asDataStructurePattern
        ;

dataStructurePattern : tuplePattern
                     | sequencePattern
                     | dictPattern
                     | recordPattern
                     ;

asDataStructurePattern : identifier AT (PARENS_L dataStructurePattern PARENS_R | dataStructurePattern) ;

patternWithoutSequence: underscore
                      | patternValue
                      | tuplePattern
                      | dictPattern
                      ;

tuplePattern : PARENS_L pattern (COMMA pattern)+ PARENS_R ;
sequencePattern : BRACKET_L (pattern (COMMA pattern)*)? BRACKET_R
                | headTails
                | tailsHead
                | headTailsHead
                ;
headTails : (patternWithoutSequence OP_CONS_L)+ tails ;
tailsHead :  tails (OP_CONS_R patternWithoutSequence)+ ;

headTailsHead : leftPattern+ tails rightPattern+ ;
leftPattern : patternWithoutSequence OP_CONS_L ;
rightPattern : OP_CONS_R patternWithoutSequence ;

tails : identifier | sequence | underscore | stringLiteral ;

dictPattern : CURLY_L (patternValue OP_ASSIGN pattern (COMMA patternValue OP_ASSIGN pattern)*)? CURLY_R ;

recordPattern : recordType (PARENS_L (name OP_ASSIGN pattern) (COMMA name OP_ASSIGN pattern)* PARENS_R)? ;

importExpr : KW_IMPORT NEWLINE? (importClause NEWLINE?)+ KW_IN NEWLINE? expression ;
importClause : moduleImport | functionsImport ;
moduleImport : fqn (KW_AS name)? ;
functionsImport : functionAlias (COMMA functionAlias)* KW_FROM fqn ;
functionAlias : funName=name (KW_AS funAlias=name)? ;


tryCatchExpr : KW_TRY NEWLINE? expression NEWLINE? catchExpr NEWLINE? KW_END ;
catchExpr : KW_CATCH NEWLINE? catchPatternExpression+ ;

catchPatternExpression : (tripplePattern | underscore) (catchPatternExpressionWithoutGuard | catchPatternExpressionWithGuard+) NEWLINE ;
tripplePattern : PARENS_L pattern COMMA pattern COMMA pattern PARENS_R ;
catchPatternExpressionWithoutGuard : NEWLINE? OP_RIGHT_ARROW NEWLINE? expression ;
catchPatternExpressionWithGuard : NEWLINE? VLINE guard=expression OP_RIGHT_ARROW NEWLINE? expr=expression ;

raiseExpr : KW_RAISE symbol stringLiteral ;

generatorExpr : sequenceGeneratorExpr | setGeneratorExpr | dictGeneratorExpr ;
sequenceGeneratorExpr : BRACKET_L reducer=expression VLINE collectionExtractor OP_LEFT_ARROW stepExpression=expression NEWLINE? (KW_IF condition=expression)? BRACKET_R ;
setGeneratorExpr : CURLY_L reducer=expression VLINE collectionExtractor OP_LEFT_ARROW stepExpression=expression NEWLINE? (KW_IF condition=expression)? CURLY_R ;
dictGeneratorExpr : CURLY_L dictGeneratorReducer VLINE collectionExtractor OP_LEFT_ARROW stepExpression=expression NEWLINE? (KW_IF condition=expression)? CURLY_R ;

dictGeneratorReducer : dictKey OP_ASSIGN dictVal ;

collectionExtractor : valueCollectionExtractor | keyValueCollectionExtractor ;
valueCollectionExtractor : identifierOrUnderscore ;
keyValueCollectionExtractor : key=identifierOrUnderscore OP_ASSIGN val=identifierOrUnderscore ;
identifierOrUnderscore : identifier | underscore ;

record : KW_RECORD UPPERCASE_NAME OP_ASSIGN PARENS_L identifier (NEWLINE? COMMA NEWLINE? identifier)* PARENS_R NEWLINE;

recordInstance : recordType PARENS_L (name OP_ASSIGN expression) (COMMA name OP_ASSIGN expression)* PARENS_R ;
recordType : UPPERCASE_NAME ;

fieldAccessExpr : identifier DOT name ;
fieldUpdateExpr : identifier PARENS_L (name OP_ASSIGN expression) (COMMA name OP_ASSIGN expression)* PARENS_R ;
