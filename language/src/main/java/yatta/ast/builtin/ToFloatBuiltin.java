package yatta.ast.builtin;

import com.oracle.truffle.api.dsl.Specialization;
import com.oracle.truffle.api.nodes.NodeInfo;

@NodeInfo(shortName = "float")
public abstract class ToFloatBuiltin extends BuiltinNode {
    @Specialization
    public double byteVal(byte value) {
        return value;
    }

    @Specialization
    public double longVal(long value) {
        return value;
    }

    @Specialization
    public double doubleVal(double value) {
        return value;
    }
}
