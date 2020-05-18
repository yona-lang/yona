package yatta;

import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.MethodSource;
import yatta.runtime.*;

import java.util.function.Function;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.*;

public class BinaryOperatorsTest extends CommonTest {
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
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l, 3l)), false, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l, 3l)), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, 2l), new PromiseHolder(Dict.EMPTY), false, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, 2l), new PromiseHolder(Dict.EMPTY.add(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, 2l)), new PromiseHolder(Dict.EMPTY), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, 2l)), new PromiseHolder(Dict.EMPTY.add(1l, 2l)), true, Boolean.class)
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
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l, 3l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l), false, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l, 3l)), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l)), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l, 3l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l)), false, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true), false, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), false, Boolean.class)
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
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l, 3l), Set.set(1l, 2l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l), false, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l, 3l), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l)), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l, 3l)), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l)), false, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), Dict.EMPTY.add(1l, true).add(2l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true), false, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), false, Boolean.class)
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
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, false, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), false, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l, 3l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l, 3l)), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l, 3l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class)
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
        new BinaryArgsHolder(Unit.INSTANCE, Unit.INSTANCE, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 1l, true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), 1l, true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(2l, new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(2l), new PromiseHolder(1l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l, 3l), Set.set(1l, 2l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(1l, 2l), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l, 3l), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l, 3l)), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(1l, 2l)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), Dict.EMPTY.add(1l, true).add(2l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), Dict.EMPTY.add(1l, true).add(2l, true), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true).add(3l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), true, Boolean.class)
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
        new BinaryArgsHolder(new PromiseHolder(1l), 2l, 3l, Long.class),
        new BinaryArgsHolder(1l, new PromiseHolder(2l), 3l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(2l), 3l, Long.class),
        new BinaryArgsHolder(Set.set(1l), 2l, "{1, 2}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l)), 2l, "{1, 2}", String.class),
        new BinaryArgsHolder(Set.set(1l), new PromiseHolder(2l), "{1, 2}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l)), new PromiseHolder(2l), "{1, 2}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true), new Tuple(2l, true), "{1 = true, 2 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true)), new Tuple(2l, true), "{1 = true, 2 = true}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true), new PromiseHolder(new Tuple(2l, true)), "{1 = true, 2 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true)), new PromiseHolder(new Tuple(2l, true)), "{1 = true, 2 = true}", String.class)
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
        new BinaryArgsHolder(new PromiseHolder(3l), 2l, 1l, Long.class),
        new BinaryArgsHolder(3l, new PromiseHolder(2l), 1l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(2l), 1l, Long.class),
        new BinaryArgsHolder(Set.set(1l, 2l), 2l, "{1}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), 2l, "{1}", String.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(2l), "{1}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(2l), "{1}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), 2l, "{1 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), 2l, "{1 = true}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(1l, true).add(2l, true), new PromiseHolder(2l), "{1 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(1l, true).add(2l, true)), new PromiseHolder(2l), "{1 = true}", String.class)
        );
  }

  @ParameterizedTest
  @MethodSource("powerOps")
  void testPowerOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("**")).asDouble();
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> powerOps() {
    return Stream.of(
        new BinaryArgsHolder(3d, 2d, 9d, Double.class),
        new BinaryArgsHolder(new PromiseHolder(3d), 2d, 9d, Double.class),
        new BinaryArgsHolder(3d, new PromiseHolder(2d), 9d, Double.class),
        new BinaryArgsHolder(new PromiseHolder(3d), new PromiseHolder(2d), 9d, Double.class)
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

  @ParameterizedTest
  @CsvSource({
      "2 + 3 * 4, 14",
      "7 * 3 + 24 / 3 - 5, 24"
  })
  public void testOperatorPrecedence(String expression, String expected) {
    assertEquals(expected, context.eval(YattaLanguage.ID, expression).toString());
  }

  @ParameterizedTest
  @MethodSource("leftShiftOps")
  void testLeftShiftOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("<<")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> leftShiftOps() {
    return Stream.of(
        new BinaryArgsHolder(8l, 2l, 8l<<2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), 2l, 8l<<2l, Long.class),
        new BinaryArgsHolder(8l, new PromiseHolder(2l), 8l<<2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), new PromiseHolder(2l), 8l<<2l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("rightShiftOps")
  void testRightShiftOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format(">>")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> rightShiftOps() {
    return Stream.of(
        new BinaryArgsHolder(8l, 2l, 8l>>2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), 2l, 8l>>2l, Long.class),
        new BinaryArgsHolder(8l, new PromiseHolder(2l), 8l>>2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), new PromiseHolder(2l), 8l>>2l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("zerofillRightShiftOps")
  void testZerofillRightShiftOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format(">>>")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> zerofillRightShiftOps() {
    return Stream.of(
        new BinaryArgsHolder(8l, 2l, 8l>>>2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), 2l, 8l>>>2l, Long.class),
        new BinaryArgsHolder(8l, new PromiseHolder(2l), 8l>>>2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(8l), new PromiseHolder(2l), 8l>>>2l, Long.class)
    );
  }

  @ParameterizedTest
  @MethodSource("logicalAndOps")
  void testLogicalAndOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("&&")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> logicalAndOps() {
    return Stream.of(
        new BinaryArgsHolder(true, true, true, Boolean.class),
        new BinaryArgsHolder(true, false, false, Boolean.class),
        new BinaryArgsHolder(false, true, false, Boolean.class),
        new BinaryArgsHolder(false, false, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(true), true, true, Boolean.class),
        new BinaryArgsHolder(true, new PromiseHolder(false), false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(false), new PromiseHolder(true), false, Boolean.class)
    );
  }

  @ParameterizedTest
  @MethodSource("logicalOrOps")
  void testLogicalOrOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("||")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> logicalOrOps() {
    return Stream.of(
        new BinaryArgsHolder(true, true, true, Boolean.class),
        new BinaryArgsHolder(true, false, true, Boolean.class),
        new BinaryArgsHolder(false, true, true, Boolean.class),
        new BinaryArgsHolder(false, false, false, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(true), true, true, Boolean.class),
        new BinaryArgsHolder(true, new PromiseHolder(false), true, Boolean.class),
        new BinaryArgsHolder(new PromiseHolder(false), new PromiseHolder(true), true, Boolean.class)
    );
  }

  @ParameterizedTest
  @MethodSource("bitwiseAndOps")
  void testBitwiseAndOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("&")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> bitwiseAndOps() {
    return Stream.of(
        new BinaryArgsHolder(4l, 2l, 4l & 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), 2l, 4l & 2l, Long.class),
        new BinaryArgsHolder(4l, new PromiseHolder(2l), 4l & 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), new PromiseHolder(2l), 4l & 2l, Long.class),
        new BinaryArgsHolder(Set.set(2l, 4l), Set.set(2l), "{2}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(2l, 4l)), Set.set(2l), "{2}", String.class),
        new BinaryArgsHolder(Set.set(2l, 4l), new PromiseHolder(Set.set(2l)), "{2}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(2l, 4l)), new PromiseHolder(Set.set(2l)), "{2}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), Dict.EMPTY.add(2l, true).add(3l, false), "{3 = false}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), Dict.EMPTY.add(2l, true).add(3l, false), "{3 = false}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{3 = false}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{3 = false}", String.class)
    );
  }

  @ParameterizedTest
  @MethodSource("bitwiseOrOps")
  void testBitwiseOrOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("|")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> bitwiseOrOps() {
    return Stream.of(
        new BinaryArgsHolder(4l, 2l, 4l | 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), 2l, 4l | 2l, Long.class),
        new BinaryArgsHolder(4l, new PromiseHolder(2l), 4l | 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), new PromiseHolder(2l), 4l | 2l, Long.class),
        new BinaryArgsHolder(Set.set(4l), Set.set(2l), "{2, 4}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(4l)), Set.set(2l), "{2, 4}", String.class),
        new BinaryArgsHolder(Set.set(4l), new PromiseHolder(Set.set(2l)), "{2, 4}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(4l)), new PromiseHolder(Set.set(2l)), "{2, 4}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), Dict.EMPTY.add(2l, true).add(3l, false), "{2 = true, 3 = false, 4 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), Dict.EMPTY.add(2l, true).add(3l, false), "{2 = true, 3 = false, 4 = true}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{2 = true, 3 = false, 4 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{2 = true, 3 = false, 4 = true}", String.class)

    );
  }

  @ParameterizedTest
  @MethodSource("bitwiseXorOps")
  void testBitwiseXorOps(BinaryArgsHolder args) {
    Object ret = context.eval(YattaLanguage.ID, args.format("^")).as(args.expectedType);
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> bitwiseXorOps() {
    return Stream.of(
        new BinaryArgsHolder(4l, 2l, 4l ^ 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), 2l, 4l ^ 2l, Long.class),
        new BinaryArgsHolder(4l, new PromiseHolder(2l), 4l ^ 2l, Long.class),
        new BinaryArgsHolder(new PromiseHolder(4l), new PromiseHolder(2l), 4l ^ 2l, Long.class),
        new BinaryArgsHolder(Set.set(1l, 2l), Set.set(2l, 3l), "{1, 3}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), Set.set(2l, 3l), "{1, 3}", String.class),
        new BinaryArgsHolder(Set.set(1l, 2l), new PromiseHolder(Set.set(2l, 3l)), "{1, 3}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Set.set(1l, 2l)), new PromiseHolder(Set.set(2l, 3l)), "{1, 3}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), Dict.EMPTY.add(2l, true).add(3l, false), "{2 = true, 4 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), Dict.EMPTY.add(2l, true).add(3l, false), "{2 = true, 4 = true}", String.class),
        new BinaryArgsHolder(Dict.EMPTY.add(4l, true).add(3l, true), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{2 = true, 4 = true}", String.class),
        new BinaryArgsHolder(new PromiseHolder(Dict.EMPTY.add(4l, true).add(3l, true)), new PromiseHolder(Dict.EMPTY.add(2l, true).add(3l, false)), "{2 = true, 4 = true}", String.class)
    );
  }

  @ParameterizedTest
  @MethodSource("sequenceCatenateOps")
  void testSequenceCatenateOps(BinaryArgsHolder args) {
    Value ret = context.eval(YattaLanguage.ID, args.format("++"));
    args.validator.apply(ret);
  }

  private static boolean sequenceCatenateOpsValidator(Value result) {
    assertEquals(4, result.getArraySize());
    Object[] array = result.as(Object[].class);
    assertEquals(1l, array[0]);
    assertEquals(2l, array[1]);
    assertEquals(3l, array[2]);
    assertEquals(4l, array[3]);
    return true;
  }

  static Stream<BinaryArgsHolder> sequenceCatenateOps() {
    return Stream.of(
        new BinaryArgsHolder(Seq.sequence(1l, 2l), Seq.sequence(3l, 4l), BinaryOperatorsTest::sequenceCatenateOpsValidator),
        new BinaryArgsHolder(new PromiseHolder(Seq.sequence(1l, 2l)), Seq.sequence(3l, 4l), BinaryOperatorsTest::sequenceCatenateOpsValidator),
        new BinaryArgsHolder(Seq.sequence(1l, 2l), new PromiseHolder(Seq.sequence(3l, 4l)), BinaryOperatorsTest::sequenceCatenateOpsValidator),
        new BinaryArgsHolder(new PromiseHolder(Seq.sequence(1l, 2l)), new PromiseHolder(Seq.sequence(3l, 4l)), BinaryOperatorsTest::sequenceCatenateOpsValidator)
    );
  }

  @ParameterizedTest
  @MethodSource("sequenceJoinLeftOps")
  void testSequenceJoinLeftOps(BinaryArgsHolder args) {
    Object[] ret = context.eval(YattaLanguage.ID, args.format("-|")).as(Object[].class);
    assertArrayEquals((Object[]) args.expected, ret);
  }

  static Stream<BinaryArgsHolder> sequenceJoinLeftOps() {
    return Stream.of(
        new BinaryArgsHolder(0l, Seq.sequence(1l, 2l), new Object[] {0l, 1l, 2l}, Object[].class),
        new BinaryArgsHolder(new PromiseHolder(0l), Seq.sequence(1l, 2l), new Object[] {0l, 1l, 2l}, Object[].class),
        new BinaryArgsHolder(0l, new PromiseHolder(Seq.sequence(1l, 2l)), new Object[] {0l, 1l, 2l}, Object[].class),
        new BinaryArgsHolder(new PromiseHolder(0l), new PromiseHolder(Seq.sequence(1l, 2l)), new Object[] {0l, 1l, 2l}, Object[].class)
    );
  }

  @ParameterizedTest
  @MethodSource("sequenceJoinRightOps")
  void testSequenceJoinRightOps(BinaryArgsHolder args) {
    Object[] ret = context.eval(YattaLanguage.ID, args.format("|-")).as(Object[].class);
    assertArrayEquals((Object[]) args.expected, ret);
  }

  static Stream<BinaryArgsHolder> sequenceJoinRightOps() {
    return Stream.of(
        new BinaryArgsHolder(Seq.sequence(1l, 2l), 3l, new Object[] {1l, 2l, 3l}, Object[].class),
        new BinaryArgsHolder(new PromiseHolder(Seq.sequence(1l, 2l)), 3l, new Object[] {1l, 2l, 3l}, Object[].class),
        new BinaryArgsHolder(Seq.sequence(1l, 2l), new PromiseHolder(3l), new Object[] {1l, 2l, 3l}, Object[].class),
        new BinaryArgsHolder(new PromiseHolder(Seq.sequence(1l, 2l)), new PromiseHolder(3l), new Object[] {1l, 2l, 3l}, Object[].class)
    );
  }

  @ParameterizedTest
  @MethodSource("inOps")
  void testInOps(BinaryArgsHolder args) {
    boolean ret = context.eval(YattaLanguage.ID, args.format("in")).asBoolean();
    assertEquals(args.expected, ret);
  }

  static Stream<BinaryArgsHolder> inOps() {
    return Stream.of(
        // Sequence
        new BinaryArgsHolder(3l, Seq.sequence(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), Seq.sequence(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(3l, new PromiseHolder(Seq.sequence(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(Seq.sequence(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(1l, Seq.sequence(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), Seq.sequence(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(Seq.sequence(1l, 2l)), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(Seq.sequence(1l, 2l)), true, boolean.class),

        // Set
        new BinaryArgsHolder(3l, Set.set(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), Set.set(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(3l, new PromiseHolder(Set.set(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(Set.set(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(1l, Set.set(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), Set.set(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(Set.set(1l, 2l)), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(Set.set(1l, 2l)), true, boolean.class),

        // Set
        new BinaryArgsHolder(3l, Dict.EMPTY.add(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), Dict.EMPTY.add(1l, 2l), false, boolean.class),
        new BinaryArgsHolder(3l, new PromiseHolder(Dict.EMPTY.add(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(3l), new PromiseHolder(Dict.EMPTY.add(1l, 2l)), false, boolean.class),
        new BinaryArgsHolder(1l, Dict.EMPTY.add(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), Dict.EMPTY.add(1l, 2l), true, boolean.class),
        new BinaryArgsHolder(1l, new PromiseHolder(Dict.EMPTY.add(1l, 2l)), true, boolean.class),
        new BinaryArgsHolder(new PromiseHolder(1l), new PromiseHolder(Dict.EMPTY.add(1l, 2l)), true, boolean.class)
    );
  }

  private static final class BinaryArgsHolder {
    final Object left, right, expected;
    final Function<Value, Boolean> validator;
    final Class expectedType;

    public BinaryArgsHolder(Object left, Object right, Object expected, Class expectedType) {
      this.left = left;
      this.right = right;
      this.expected = expected;
      this.validator = null;
      this.expectedType = expectedType;
    }

    public BinaryArgsHolder(Object left, Object right, Function<Value, Boolean> validator) {
      this.left = left;
      this.right = right;
      this.expected = null;
      this.validator = validator;
      this.expectedType = null;
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

  @Test
  public void functionsEqualityOne() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    fun1 = \\a b -> a+b\n" +
        "    fun2 = \\a b -> a-b\n" +
        "in fun1 == fun2").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void functionsNotEqualityOne() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    fun1 = \\a b -> a+b\n" +
        "    fun2 = \\a b -> a-b\n" +
        "in fun1 != fun2").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void functionsEqualityTwo() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    fun1 = \\a b -> a+b\n" +
        "    fun2 = \\a b -> a-b\n" +
        "in fun1 == fun1").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void functionsNotEqualityTwo() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    fun1 = \\a b -> a+b\n" +
        "    fun2 = \\a b -> a-b\n" +
        "in fun1 != fun1").asBoolean();
    assertFalse(ret);
  }
}
