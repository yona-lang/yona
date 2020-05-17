package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.Seq;
import yatta.runtime.async.Promise;
import yatta.runtime.strings.StringUtil;

@NodeInfo(shortName = "str")
public abstract class ToStringBuiltin extends BuiltinNode {
    @Specialization
    public Seq byteStr(byte value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public Seq booleanStr(boolean value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public Seq longStr(long value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public Seq doubleStr(double value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public Promise promiseStr(Promise value) {
        return value.map(StringUtil::yattaValueAsYattaString, this);
    }

    @Fallback
    public Seq anyStr(Object value) {
        return StringUtil.yattaValueAsYattaString(value);
    }
}
