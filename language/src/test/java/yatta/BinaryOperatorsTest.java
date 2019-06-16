package yatta;

import org.graalvm.polyglot.Context;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import yatta.runtime.Tuple;
import yatta.runtime.Unit;

import java.util.function.Function;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class BinaryOperatorsTest {
  private Context context;

  @BeforeEach
  public void initEngine() {
    context = Context.create();
  }

  @AfterEach
  public void dispose() {
    context.close();
  }

  @ParameterizedTest
  @MethodSource("equalsOps")
  void testEqualsOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("==")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  @ParameterizedTest
  @MethodSource("equalsOps")
  void testNotEqualsOps(BinaryArgsHolder args) {
    BinaryArgsHolder newArgs = args.mapExpected(exp -> !(boolean) exp);
    Object ret = context.eval(YattaLanguage.ID, newArgs.format("!=")).as(newArgs.expectedType);
    assertEquals(newArgs.expected, ret);
  }

  static Stream<BinaryArgsHolder> equalsOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 2l, false, Boolean.class),
        new BinaryArgsHolder(1l, 1l, true, Boolean.class),
        new BinaryArgsHolder(1d, 2d, false, Boolean.class),
        new BinaryArgsHolder(1d, 1d, true, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 2, false, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 1, true, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", false, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"ad\"", true, Boolean.class),
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, true, Boolean.class),
        new BinaryArgsHolder(new Tuple(1, 2), new Tuple(1, 2, 3), false, Boolean.class),
        new BinaryArgsHolder(new Tuple(1, 2), new Tuple(1, 2), true, Boolean.class),
        new BinaryArgsHolder("[1, 2]", "[1, 2, 3]", false, Boolean.class),
        new BinaryArgsHolder("[1, 2]", "[1, 2, 3]", false, Boolean.class),
        new BinaryArgsHolder(":yes", ":yes", true, Boolean.class),
        new BinaryArgsHolder(":yes", ":no", false, Boolean.class),
        // TODO add dictionary
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class)
    );
  }

  @ParameterizedTest
  @MethodSource("lowerThanOps")
  void testLowerThanOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("<")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  @ParameterizedTest
  @MethodSource("greaterThanOps")
  void testGreaterThanOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format(">")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> lowerThanOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 2l, true, Boolean.class),
        new BinaryArgsHolder(2l, 1l, false, Boolean.class),
        new BinaryArgsHolder(1d, 2d, true, Boolean.class),
        new BinaryArgsHolder(2d, 1d, false, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 2, true, Boolean.class),
        new BinaryArgsHolder((byte) 2, (byte) 1, false, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", true, Boolean.class),
        new BinaryArgsHolder("\"am\"", "\"ad\"", false, Boolean.class),
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), false, Boolean.class)
    );
  }

  static Stream<BinaryArgsHolder> greaterThanOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 2l, false, Boolean.class),
        new BinaryArgsHolder(2l, 1l, true, Boolean.class),
        new BinaryArgsHolder(1d, 2d, false, Boolean.class),
        new BinaryArgsHolder(2d, 1d, true, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 2, false, Boolean.class),
        new BinaryArgsHolder((byte) 2, (byte) 1, true, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", false, Boolean.class),
        new BinaryArgsHolder("\"am\"", "\"ad\"", true, Boolean.class),
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), true, Boolean.class)
    );
  }

  @ParameterizedTest
  @MethodSource("lowerOrEqualsOps")
  void testLowerThanEqualsOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("<=")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  @ParameterizedTest
  @MethodSource("greaterOrEqualsOps")
  void testGreaterThanEqualsOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format(">=")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> lowerOrEqualsOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, 2l, true, Boolean.class),
        new BinaryArgsHolder(2l, 1l, false, Boolean.class),
        new BinaryArgsHolder(1d, 1d, true, Boolean.class),
        new BinaryArgsHolder(1d, 2d, true, Boolean.class),
        new BinaryArgsHolder(2d, 1d, false, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 1, true, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 2, true, Boolean.class),
        new BinaryArgsHolder((byte) 2, (byte) 1, false, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"ad\"", true, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", true, Boolean.class),
        new BinaryArgsHolder("\"am\"", "\"ad\"", false, Boolean.class),
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), false, Boolean.class)
    );
  }

  static Stream<BinaryArgsHolder> greaterOrEqualsOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, 2l, false, Boolean.class),
        new BinaryArgsHolder(2l, 1l, true, Boolean.class),
        new BinaryArgsHolder(1d, 1d, true, Boolean.class),
        new BinaryArgsHolder(1d, 2d, false, Boolean.class),
        new BinaryArgsHolder(2d, 1d, true, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 1, true, Boolean.class),
        new BinaryArgsHolder((byte) 1, (byte) 2, false, Boolean.class),
        new BinaryArgsHolder((byte) 2, (byte) 1, true, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"ad\"", true, Boolean.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", false, Boolean.class),
        new BinaryArgsHolder("\"am\"", "\"ad\"", true, Boolean.class),
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), true, Boolean.class)
    );
  }

  @ParameterizedTest
  @MethodSource("plusOps")
  void testPlusOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("+")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> plusOps() {
    return Stream.of(
        new BinaryArgsHolder(1l, 2l, 3l, Long.class),
        new BinaryArgsHolder(1d, 2d, 3d, Double.class),
        new BinaryArgsHolder("\"ad\"", "\"am\"", "adam", String.class),
        // TODO add sequence and dictionary
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, 3l, Long.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), 3l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), 3l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("minusOps")
  void testMinusOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("-")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> minusOps() {
    return Stream.of(
        new BinaryArgsHolder(3l, 2l, 1l, Long.class),
        new BinaryArgsHolder(3d, 2d, 1d, Double.class),
        // TODO add dictionary
        new BinaryArgsHolder(new PromiseHolder(3l), 2l, 1l, Long.class),
        new BinaryArgsHolder(3l, new PromiseHolder(2l), 1l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(2l), 1l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("multiplyOps")
  void testMultiplyOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("*")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> multiplyOps() {
    return Stream.of(
        new BinaryArgsHolder(3l, 2l, 6l, Long.class),
        new BinaryArgsHolder(3d, 2d, 6d, Double.class),
        new BinaryArgsHolder(new PromiseHolder(3l), 2l, 6l, Long.class),
        new BinaryArgsHolder(3l, new PromiseHolder(2l), 6l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(2l), 6l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("divideOps")
  void testDivideOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("/")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> divideOps() {
    return Stream.of(
        new BinaryArgsHolder(6l, 2l, 3l, Long.class),
        new BinaryArgsHolder(6d, 2d, 3d, Double.class),
        new BinaryArgsHolder(new PromiseHolder(6l), 2l, 3l, Long.class),
        new BinaryArgsHolder(6l, new PromiseHolder(2l), 3l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(6l), new PromiseHolder(2l), 3l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("moduloOps")
  void testModuloOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("%")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> moduloOps() {
    return Stream.of(
        new BinaryArgsHolder(5l, 2l, 1l, Long.class),
        new BinaryArgsHolder(5d, 2d, 1d, Double.class),
        new BinaryArgsHolder(new PromiseHolder(5l), 2l, 1l, Long.class),
        new BinaryArgsHolder(5l, new PromiseHolder(2l), 1l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(5l), new PromiseHolder(2l), 1l, Long.class)
    );
  }

  private static final class BinaryArgsHolder {
    final Object left, right, expected;
    final Class expectedType;

    public BinaryArgsHolder(Object left, Object right, Object expected, Class expectedType) {
      this.left = left;
      this.right = right;
      this.expected = expected;
      this.expectedType = expectedType;
    }

    private static String format(Object obj) {
      if (obj instanceof PromiseHolder) {
        return String.format("async \\->%s", obj);
      } else {
        return String.format("%s", obj);
      }
    }

    public String format(String op) {
      return String.format("(%s) %s (%s)", format(left), op, format(right));
    }

    public BinaryArgsHolder mapExpected(Function<Object, Object> callback) {
      return new BinaryArgsHolder(left, right, callback.apply(expected), expectedType);
    }
  }

  private static final class PromiseHolder {
    Object val;

    public PromiseHolder(Object val) {
      this.val = val;
    }

    @Override
    public String toString() {
      return String.format("%s", val);
    }
  }
}
