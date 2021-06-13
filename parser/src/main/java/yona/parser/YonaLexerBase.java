package yona.parser;

import org.antlr.v4.runtime.CharStream;
import org.antlr.v4.runtime.CommonToken;
import org.antlr.v4.runtime.Lexer;

import java.util.Stack;

abstract class YonaLexerBase extends Lexer {
    public YonaLexerBase(CharStream input) {
        super(input);
    }

    private int interpolatedStringLevel;
    private final Stack<Integer> curlyLevels = new Stack<>();

    public void interpolationOpened() {
        interpolatedStringLevel += 1;
    }

    public void interpolationClosed() {
        interpolatedStringLevel -= 1;
        assert interpolatedStringLevel >= 0;
    }

    public void openCurly() {
        if (interpolatedStringLevel > 0) {
            curlyLevels.push(curlyLevels.pop() + 1);
        }
    }

    public void closeCurly() {
        if (interpolatedStringLevel > 0)
        {
            curlyLevels.push(curlyLevels.pop() - 1);
            if (curlyLevels.peek() == 0)
            {
                curlyLevels.pop();
                popMode();
                setType(YonaLexer.CLOSE_INTERP);
            }
        }
    }

    public void interpolatedCurlyOpened() {
        curlyLevels.push(1);
    }

    public void interpolatedDoubleCurlyOpened() {
        this.emit(curlyToken("{"));
    }

    public void interpolatedDoubleCurlyClosed() {
        this.emit(curlyToken("}"));
    }

    private CommonToken curlyToken(final String text) {
        int stop = this.getCharIndex() - 1;
        int start = text.isEmpty() ? stop : stop - text.length();
        return new CommonToken(this._tokenFactorySourcePair, YonaLexer.REGULAR_STRING_INSIDE, DEFAULT_TOKEN_CHANNEL, start, stop);
    }
}
