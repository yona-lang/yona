package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "int")
public abstract class ToIntegerBuiltin extends BuiltinNode {
    @Specialization
    public long byteVal(byte value) {
        return value;
    }

    @Specialization
    public long longVal(long value) {
        return value;
    }

    @Specialization
    public long doubleVal(double value) {
        return (long) value;
    }
}
