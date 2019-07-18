package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Fallback;
import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;
import yatta.runtime.async.Promise;
import yatta.runtime.strings.StringUtil;

@NodeInfo(shortName = "str")
public abstract class ToStringBuiltin extends BuiltinNode {
    @Specialization
    public String byteStr(byte value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public String booleanStr(boolean value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public String longStr(long value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public String doubleStr(double value) {
        return StringUtil.yattaValueAsYattaString(value);
    }

    @Specialization
    public Promise promiseStr(Promise value) {
        return value.map(val -> {
            return StringUtil.yattaValueAsYattaString(val);
        }, this);
    }

    @Fallback
    public String anyStr(Object value) {
        return StringUtil.yattaValueAsYattaString(value);
    }
}
