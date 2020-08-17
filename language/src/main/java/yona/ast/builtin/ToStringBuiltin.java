package yona.ast.builtin;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yona.runtime.Seq;
import yona.runtime.async.Promise;
import yona.runtime.strings.StringUtil;

@NodeInfo(shortName = "str")
public abstract class ToStringBuiltin extends BuiltinNode {
    @Specialization
    public Seq byteStr(byte value) {
        return StringUtil.yonaValueAsYonaString(value);
    }

    @Specialization
    public Seq booleanStr(boolean value) {
        return StringUtil.yonaValueAsYonaString(value);
    }

    @Specialization
    public Seq longStr(long value) {
        return StringUtil.yonaValueAsYonaString(value);
    }

    @Specialization
    public Seq doubleStr(double value) {
        return StringUtil.yonaValueAsYonaString(value);
    }

    @Specialization
    public Promise promiseStr(Promise value) {
        return value.map(StringUtil::yonaValueAsYonaString, this);
    }

    @Fallback
    public Seq anyStr(Object value) {
        return StringUtil.yonaValueAsYonaString(value);
    }
}
