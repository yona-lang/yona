package yatta;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayInputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class StdLibTest extends CommonTest {
  @Test
  public void sequenceLenTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::len [1, 2, 3]").asLong();
  }

  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::foldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::foldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval(YattaLanguage.ID, "let xx = 5 in Seq::foldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21L, ret);
  }

  @Test
  public void sequenceReduceLeftFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceRightFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducer [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDropNTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::drop 2 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftTakeNTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::take 2 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDedupeTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 1, 2, 3, 3, 4] <| Transducers::dedupe (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftDistinctTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 2, 3, 4, 1, 2, 3, 4] <| Transducers::distinct (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftChunkTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [6, 1, 5, 2, 4, 3] <| Transducers::chunk 2 (1, \\acc val -> acc * Seq::len val, identity)").asLong();
    assertEquals(8L, ret);
  }

  @Test
  public void sequenceReduceLeftScanTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [1, 2, 3] <| Transducers::scan (0, \\ acc val -> acc + Seq::len val, identity)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftCatTest() {
    long ret = context.eval(YattaLanguage.ID, "Seq::reducel [{1, 2}, {3, 4}] <| Transducers::cat (Set::reduce) (0, \\acc val -> acc + val, identity)").asLong();
    assertEquals(10L, ret);
  }

  @Test
  public void dictFoldTest() {
    long ret = context.eval(YattaLanguage.ID, "Dict::fold {'a' = 1, 'b' = 2, 'c' = 3} (\\acc _ -> acc + 1) 0").asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void dictReduceMapTest() {
    long ret = context.eval(YattaLanguage.ID, "Dict::reduce {'a' = 1, 'b' = 2, 'c' = 3} <| Transducers::map \\val -> val (0, \\state val -> state + 1, \\state -> state * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setFoldTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::fold {1, 2, 3} (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setReduceFilterTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::reduce {-2,-1,0,1,2} <| Transducers::filter \\val -> val < 0 (0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void setReduceMapTest() {
    long ret = context.eval(YattaLanguage.ID, "Set::reduce {1, 2, 3} <| Transducers::map \\val -> val + 1 (0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(18L, ret);
  }

  @Test
  public void systemCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "System::run [\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("ahoj", ((List) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void systemAsyncCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "System::run [async \\->\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("ahoj", ((List) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void systemPipelineCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "System::pipeline [\n" +
        "[\"echo\", \"hello\"],\n" +
        "[\"rev\"]" +
        "]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("olleh", ((List) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void systemPipelineAsyncCommandTest() {
    Value tuple = context.eval(YattaLanguage.ID, "System::pipeline [\n" +
        "[\"echo\", \"hello\"],\n" +
        "[async \\->\"rev\"]" +
        "]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("olleh", ((List) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void simpleEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "eval :yatta \"1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void asyncEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "eval :yatta \"async \\\\-> 1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void raiseEvalTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YattaLanguage.ID, "eval :yatta \"raise :test \\\"test msg\\\"\"");
      } catch (PolyglotException ex) {
        assertEquals("YattaError <test>: test msg", ex.getMessage());
        throw ex;
      }
    });
  }

  @Test
  public void javaTypeEvalTest() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [\"50\"]\n" +
        "in Java::instanceof instance type").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void javaAsyncTypeEvalTest() {
    boolean ret = context.eval(YattaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [async \\-> \"50\"]\n" +
        "in Java::instanceof instance type").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void javaCatchTest() {
    String ret = context.eval(YattaLanguage.ID, "try\n" +
        "let\n" +
        "    type = Java::type \"java.lang.ArithmeticException\"\n" +
        "    error = Java::new type [\"testing error\"]\n" +
        "in Java::throw error\n" +
        "catch\n" +
        "    (:java, error_msg, _) -> error_msg\n" +
        "end").asString();
    assertEquals("java.lang.ArithmeticException: testing error", ret);
  }

  @Test
  public void javaCallNoArgMethodTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [\"-2\"]\n" +
        "in instance::longValue").asLong();
    assertEquals(-2l, ret);
  }

  @Test
  public void javaCallArgMethodTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    big_two = Java::new type [\"2\"]\n" +
        "    big_three = Java::new type [\"3\"]\n" +
        "    result = big_two::multiply big_three\n" +
        "in result::longValue").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void javaCallStaticMethodTest() {
    long ret = context.eval(YattaLanguage.ID, "(java\\util\\Collections::singletonList 5)::get (java\\Types::to_int 0)").asLong();
    assertEquals(5l, ret);
  }

  @Test
  public void javaCallCurriedMethodTest() {
    long ret = context.eval(YattaLanguage.ID, "do\n" +
        "    list = Java::new (Java::type \"java.util.ArrayList\") []\n" +
        "    list::add 5\n" +
        "    set_second = list::set <| java\\Types::to_int 0\n" +
        "    set_second 6\n" +
        "    list::get <| java\\Types::to_int 0\n" +
        "end").asLong();
    assertEquals(6l, ret);
  }

  @Test
  public void javaCallStaticWithAsyncMethodTest() {
    long ret = context.eval(YattaLanguage.ID, "do\n" +
        "    list = Java::new (Java::type \"java.util.ArrayList\") []\n" +
        "    list::add (async \\ -> 5)\n" +
        "    list::size\n" +
        "end").asLong();
    assertEquals(1l, ret);
  }

  @Test
  public void simpleJsonParseEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "JSON::parse \"5\"").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void simpleAsyncJsonParseEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "JSON::parse <| async \\-> \"5\"").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void arrayJsonParseEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "JSON::parse \"[1, 2]\"").getArraySize();
    assertEquals(2L, ret);
  }

  @Test
  public void dictJsonParseEvalTest() {
    long ret = context.eval(YattaLanguage.ID, "Dict::len <| JSON::parse \"{{\\\"1\\\": 2, \\\"3\\\": 4}}\"").asLong();
    assertEquals(2L, ret);
  }

  @Test
  public void stringJsonParseEvalTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::parse \"\\\"test\\\"\"").asString();
    assertEquals("test", ret);
  }

  @Test
  public void boolJsonFalseParseEvalTest() {
    boolean ret = context.eval(YattaLanguage.ID, "JSON::parse \"false\"").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void boolJsonTrueParseEvalTest() {
    boolean ret = context.eval(YattaLanguage.ID, "JSON::parse \"true\"").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void unitJsonParseEvalTest() {
    boolean ret = context.eval(YattaLanguage.ID, "JSON::parse \"null\"").isNull();
    assertTrue(ret);
  }

  @Test
  public void unitJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate ()").asString();
    assertEquals("null", ret);
  }

  @Test
  public void integerJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate 1").asString();
    assertEquals("1", ret);
  }

  @Test
  public void tupleBooleanJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate (true, false)").asString();
    assertEquals("[true, false]", ret);
  }

  @Test
  public void seqIntegerOneJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate [1]").asString();
    assertEquals("[1]", ret);
  }

  @Test
  public void seqIntegerTwoJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate [1, 2]").asString();
    assertEquals("[1, 2]", ret);
  }

  @Test
  public void seqIntegerThreeJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate [1, 2, 3]").asString();
    assertEquals("[1, 2, 3]", ret);
  }

  @Test
  public void dictTwoJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate {:one = 1, :two = 2}").asString();
    assertEquals("{\"one\": 1, \"two\": 2}", ret);
  }

  @Test
  public void setTwoJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate {:one, :two}").asString();
    assertEquals("[\"one\", \"two\"]", ret);
  }

  @Test
  public void charJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate 'x'").asString();
    assertEquals("\"x\"", ret);
  }

  @Test
  public void stringJsonGenerateTest() {
    String ret = context.eval(YattaLanguage.ID, "JSON::generate \"x\"").asString();
    assertEquals("\"x\"", ret);
  }

  @Test
  public void timeoutPromiseTest() {
    long ret = context.eval(YattaLanguage.ID, "timeout (:millis, 500) (let _ = sleep (:millis, 100) in 1)").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void timeoutValueTest() {
    long ret = context.eval(YattaLanguage.ID, "timeout (:millis, 500) 1").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void timeoutAsyncTimeoutTest() {
    long ret = context.eval(YattaLanguage.ID, "timeout (let _ = sleep (:millis, 1000) in (:millis, 500)) 1").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void httpClientTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (status, headers, body) = http\\Client::get session \"https://httpbin.org/get\" {}\n" +
        "in\n" +
        "    status").asLong();
    assertEquals(200L, ret);
  }

  @Test
  public void httpClientAsyncTest() {
    long ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = async \\-> http\\Client::session {}\n" +
        "    (status, headers, body) = http\\Client::get session (async \\->\"https://httpbin.org/get\") (async \\->{})\n" +
        "in\n" +
        "    status").asLong();
    assertEquals(200L, ret);
  }

  @Test
  public void httpClientAuthTest() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = http\\Client::session {:authenticator = (:password, \"test\", \"test\")}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/basic-auth/test/test\" {}\n" +
        "    {\"user\" = user, \"authenticated\" = true} = JSON::parse body\n" +
        "in\n" +
        "    user").asString();
    assertEquals("test", ret);
  }

  @Test
  public void httpClientAuthAsyncTest() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = http\\Client::session <| async \\-> {async \\-> :authenticator = async \\-> (async \\-> :password, \"test\", \"test\")}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/basic-auth/test/test\" {}\n" +
        "    {\"user\" = user, \"authenticated\" = true} = JSON::parse body\n" +
        "in\n" +
        "    user").asString();
    assertEquals("test", ret);
  }

  @Test
  public void httpClientHeadersTest() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/headers\" {:accept = \"application/json\"}\n" +
        "    {\"headers\" = response_headers} = JSON::parse body\n" +
        "in\n" +
        "    Dict::lookup response_headers \"Accept\"").asString();
    assertEquals("application/json", ret);
  }

  @Test
  public void httpClientHeadersAsyncTest() {
    String ret = context.eval(YattaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/headers\" <| async \\-> {async \\-> :accept = async \\-> \"application/json\"}\n" +
        "    {\"headers\" = response_headers} = JSON::parse body\n" +
        "in\n" +
        "    Dict::lookup response_headers \"Accept\"").asString();
    assertEquals("application/json", ret);
  }

  @Test
  public void readTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).in(new ByteArrayInputStream("x".getBytes())).build();
    customContext.enter();
    int ret = customContext.eval(YattaLanguage.ID, "read").asInt();
    assertEquals('x', ret);
    customContext.leave();
  }

  @Test
  public void readlnTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).in(new ByteArrayInputStream("hello\n".getBytes())).build();
    customContext.enter();
    String ret = customContext.eval(YattaLanguage.ID, "readln").asString();
    assertEquals("hello", ret);
    customContext.leave();
  }
}
