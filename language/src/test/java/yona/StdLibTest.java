package yona;

import org.graalvm.polyglot.Context;
import org.graalvm.polyglot.PolyglotException;
import org.graalvm.polyglot.Value;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestInstance;

import java.io.ByteArrayInputStream;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

@TestInstance(TestInstance.Lifecycle.PER_CLASS)
public class StdLibTest extends CommonTest {
  @Test
  public void sequenceLenTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::len [1, 2, 3]").asLong();
  }

  @Test
  public void sequenceFoldLeftTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::foldl [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldRightTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::foldr [1, 2, 3] (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceFoldLeftWithinLetTest() {
    long ret = context.eval(YonaLanguage.ID, "let xx = 5 in Seq::foldl [1, 2, 3] (\\acc val -> acc + val + xx) 0").asLong();
    assertEquals(21L, ret);
  }

  @Test
  public void sequenceReduceLeftFilterTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceRightFilterTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducer [-2,-1,0,1,2] <| Transducers::filter \\val -> val < 0 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDropNTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::drop 2 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftTakeNTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [-2,-1,0,1,2] <| Transducers::take 2 (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void sequenceReduceLeftDedupeTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [1, 1, 2, 3, 3, 4] <| Transducers::dedupe (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftDistinctTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [1, 2, 3, 4, 1, 2, 3, 4] <| Transducers::distinct (0, \\acc val -> acc + val, \\acc -> acc * 2)").asLong();
    assertEquals(20L, ret);
  }

  @Test
  public void sequenceReduceLeftChunkTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [6, 1, 5, 2, 4, 3] <| Transducers::chunk 2 (1, \\acc val -> acc * Seq::len val, identity)").asLong();
    assertEquals(8L, ret);
  }

  @Test
  public void sequenceReduceLeftScanTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [1, 2, 3] <| Transducers::scan (0, \\ acc val -> acc + Seq::len val, identity)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void sequenceReduceLeftCatTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::reducel [{1, 2}, {3, 4}] <| Transducers::cat (Set::reduce) (0, \\acc val -> acc + val, identity)").asLong();
    assertEquals(10L, ret);
  }

  @Test
  public void dictFoldTest() {
    long ret = context.eval(YonaLanguage.ID, "Dict::fold {'a' = 1, 'b' = 2, 'c' = 3} (\\acc _ -> acc + 1) 0").asLong();
    assertEquals(3L, ret);
  }

  @Test
  public void dictReduceMapTest() {
    long ret = context.eval(YonaLanguage.ID, "Dict::reduce {'a' = 1, 'b' = 2, 'c' = 3} <| Transducers::map \\val -> val (0, \\state val -> state + 1, \\state -> state * 2)").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setFoldTest() {
    long ret = context.eval(YonaLanguage.ID, "Set::fold {1, 2, 3} (\\acc val -> acc + val) 0").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void setReduceFilterTest() {
    long ret = context.eval(YonaLanguage.ID, "Set::reduce {-2,-1,0,1,2} <| Transducers::filter \\val -> val < 0 (0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(-6L, ret);
  }

  @Test
  public void setReduceMapTest() {
    long ret = context.eval(YonaLanguage.ID, "Set::reduce {1, 2, 3} <| Transducers::map \\val -> val + 1 (0, \\state val -> state + val, \\state -> state * 2)").asLong();
    assertEquals(18L, ret);
  }

  @Test
  @SuppressWarnings("unchecked")
  public void systemCommandTest() {
    Value tuple = context.eval(YonaLanguage.ID, "System::run [\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("ahoj", ((List<String>) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  @SuppressWarnings("unchecked")
  public void systemAsyncCommandTest() {
    Value tuple = context.eval(YonaLanguage.ID, "System::run [async \\->\"echo\", \"ahoj\"]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("ahoj", ((List<String>) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  @SuppressWarnings("unchecked")
  public void systemPipelineCommandTest() {
    Value tuple = context.eval(YonaLanguage.ID, "System::pipeline [\n" +
        "[\"echo\", \"hello\"],\n" +
        "[\"rev\"]" +
        "]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("olleh", ((List<String>) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  @SuppressWarnings("unchecked")
  public void systemPipelineAsyncCommandTest() {
    Value tuple = context.eval(YonaLanguage.ID, "System::pipeline [\n" +
        "[\"echo\", \"hello\"],\n" +
        "[async \\->\"rev\"]" +
        "]");
    assertTrue(tuple.hasArrayElements());

    Object[] array = tuple.as(Object[].class);
    assertEquals(0L, array[0]);
    assertEquals("olleh", ((List<String>) array[1]).get(0));
    assertTrue(((String) array[2]).isEmpty()); // empty Seq will always evaluate isString to 0 and then polyglot will return empty string (even though technically it should be an empty Seq of Seqs(strings, lines))
  }

  @Test
  public void simpleEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "eval :yona \"1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void asyncEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "eval :yona \"async \\\\-> 1\"").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void raiseEvalTest() {
    assertThrows(PolyglotException.class, () -> {
      try {
        context.eval(YonaLanguage.ID, "eval :yona \"raise :test \\\"test msg\\\"\"");
      } catch (PolyglotException ex) {
        assertEquals("YonaError <test>: test msg", ex.getMessage());
        throw ex;
      }
    });
  }

  @Test
  public void javaTypeEvalTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [\"50\"]\n" +
        "in Java::instanceof instance type").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void javaAsyncTypeEvalTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [async \\-> \"50\"]\n" +
        "in Java::instanceof instance type").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void javaCatchTest() {
    String ret = context.eval(YonaLanguage.ID, "try\n" +
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
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    instance = Java::new type [\"-2\"]\n" +
        "in instance::longValue").asLong();
    assertEquals(-2L, ret);
  }

  @Test
  public void javaCallArgMethodTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    type = Java::type \"java.math.BigInteger\"\n" +
        "    big_two = Java::new type [\"2\"]\n" +
        "    big_three = Java::new type [\"3\"]\n" +
        "    result = big_two::multiply big_three\n" +
        "in result::longValue").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void javaCallStaticMethodTest() {
    long ret = context.eval(YonaLanguage.ID, "(java\\util\\Collections::singletonList 5)::get (java\\Types::to_int 0)").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void javaCallCurriedMethodTest() {
    long ret = context.eval(YonaLanguage.ID, "do\n" +
        "    list = Java::new (Java::type \"java.util.ArrayList\") []\n" +
        "    list::add 5\n" +
        "    set_second = list::set <| java\\Types::to_int 0\n" +
        "    set_second 6\n" +
        "    list::get <| java\\Types::to_int 0\n" +
        "end").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void javaCallStaticWithAsyncMethodTest() {
    long ret = context.eval(YonaLanguage.ID, "do\n" +
        "    list = Java::new (Java::type \"java.util.ArrayList\") []\n" +
        "    list::add (async \\ -> 5)\n" +
        "    list::size\n" +
        "end").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void javaCallDictArgTest() {
    long ret = context.eval(YonaLanguage.ID, "yona\\TestUtil::mapSize {:one = 1, :two = 2}").asLong();
    assertEquals(2L, ret);
  }

  @Test
  public void javaCallSeqArgTest() {
    long ret = context.eval(YonaLanguage.ID, "yona\\TestUtil::arraySize [:one, :two]").asLong();
    assertEquals(2L, ret);
  }

  @Test
  public void javaCallTupleArgTest() {
    long ret = context.eval(YonaLanguage.ID, "yona\\TestUtil::arraySize (:one, :two)").asLong();
    assertEquals(2L, ret);
  }

  @Test
  public void simpleJsonParseEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "JSON::parse \"5\"").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void simpleAsyncJsonParseEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "JSON::parse <| async \\-> \"5\"").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void arrayJsonParseEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "JSON::parse \"[1, 2]\"").getArraySize();
    assertEquals(2L, ret);
  }

  @Test
  public void dictJsonParseEvalTest() {
    long ret = context.eval(YonaLanguage.ID, "Dict::len <| JSON::parse \"{{\\\"1\\\": 2, \\\"3\\\": 4}}\"").asLong();
    assertEquals(2L, ret);
  }

  @Test
  public void stringJsonParseEvalTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::parse \"\\\"test\\\"\"").asString();
    assertEquals("test", ret);
  }

  @Test
  public void boolJsonFalseParseEvalTest() {
    boolean ret = context.eval(YonaLanguage.ID, "JSON::parse \"false\"").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void boolJsonTrueParseEvalTest() {
    boolean ret = context.eval(YonaLanguage.ID, "JSON::parse \"true\"").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void unitJsonParseEvalTest() {
    boolean ret = context.eval(YonaLanguage.ID, "JSON::parse \"null\"").isNull();
    assertTrue(ret);
  }

  @Test
  public void unitJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate ()").asString();
    assertEquals("null", ret);
  }

  @Test
  public void integerJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate 1").asString();
    assertEquals("1", ret);
  }

  @Test
  public void tupleBooleanJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate (true, false)").asString();
    assertEquals("[true, false]", ret);
  }

  @Test
  public void seqIntegerOneJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate [1]").asString();
    assertEquals("[1]", ret);
  }

  @Test
  public void seqIntegerTwoJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate [1, 2]").asString();
    assertEquals("[1, 2]", ret);
  }

  @Test
  public void seqIntegerThreeJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate [1, 2, 3]").asString();
    assertEquals("[1, 2, 3]", ret);
  }

  @Test
  public void dictTwoJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate {:one = 1, :two = 2}").asString();
    assertEquals("{\"one\": 1, \"two\": 2}", ret);
  }

  @Test
  public void setTwoJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate {:one, :two}").asString();
    assertEquals("[\"one\", \"two\"]", ret);
  }

  @Test
  public void charJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate 'x'").asString();
    assertEquals("\"x\"", ret);
  }

  @Test
  public void stringJsonGenerateTest() {
    String ret = context.eval(YonaLanguage.ID, "JSON::generate \"x\"").asString();
    assertEquals("\"x\"", ret);
  }

  @Test
  public void timeoutPromiseTest() {
    long ret = context.eval(YonaLanguage.ID, "timeout (:millis, 500) (let _ = sleep (:millis, 100) in 1)").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void timeoutValueTest() {
    long ret = context.eval(YonaLanguage.ID, "timeout (:millis, 500) 1").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void timeoutAsyncTimeoutTest() {
    long ret = context.eval(YonaLanguage.ID, "timeout (let _ = sleep (:millis, 1000) in (:millis, 500)) 1").asLong();
    assertEquals(1L, ret);
  }

  @Test
  public void httpClientTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (status, headers, body) = http\\Client::get session \"https://httpbin.org/get\" {}\n" +
        "in\n" +
        "    status").asLong();
    assertEquals(200L, ret);
  }

  @Test
  public void httpClientAsyncTest() {
    long ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = async \\-> http\\Client::session {}\n" +
        "    (status, headers, body) = http\\Client::get session (async \\->\"https://httpbin.org/get\") (async \\->{})\n" +
        "in\n" +
        "    status").asLong();
    assertEquals(200L, ret);
  }

  @Test
  public void httpClientAuthTest() {
    String ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session {:authenticator = (:password, \"test\", \"test\")}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/basic-auth/test/test\" {}\n" +
        "    {\"user\" = user, \"authenticated\" = true} = JSON::parse body\n" +
        "in\n" +
        "    user").asString();
    assertEquals("test", ret);
  }

  @Test
  public void httpClientAuthAsyncTest() {
    String ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session <| async \\-> {async \\-> :authenticator = async \\-> (async \\-> :password, \"test\", \"test\")}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/basic-auth/test/test\" {}\n" +
        "    {\"user\" = user, \"authenticated\" = true} = JSON::parse body\n" +
        "in\n" +
        "    user").asString();
    assertEquals("test", ret);
  }

  @Test
  public void httpClientHeadersTest() {
    String ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/headers\" {:accept = \"application/json\"}\n" +
        "    {\"headers\" = response_headers} = JSON::parse body\n" +
        "in\n" +
        "    Dict::lookup \"Accept\" response_headers").asString();
    assertEquals("application/json", ret);
  }

  @Test
  public void httpClientHeadersBinaryBodyTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session {:body_encoding = :binary}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/image\" {:accept = \"image/jpeg\"}\n" +
        "in\n" +
        "    Seq::is_string body").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void httpClientHeadersAsyncTest() {
    String ret = context.eval(YonaLanguage.ID, "let\n" +
        "    session = http\\Client::session {}\n" +
        "    (200, headers, body) = http\\Client::get session \"https://httpbin.org/headers\" <| async \\-> {async \\-> :accept = async \\-> \"application/json\"}\n" +
        "    {\"headers\" = response_headers} = JSON::parse body\n" +
        "in\n" +
        "    Dict::lookup \"Accept\" response_headers").asString();
    assertEquals("application/json", ret);
  }

  @Test
  public void readTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).in(new ByteArrayInputStream("x".getBytes())).build();
    customContext.enter();
    int ret = customContext.eval(YonaLanguage.ID, "IO::read").asInt();
    assertEquals('x', ret);
    customContext.leave();
  }

  @Test
  public void readlnTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).in(new ByteArrayInputStream("hello\n".getBytes())).build();
    customContext.enter();
    String ret = customContext.eval(YonaLanguage.ID, "IO::readln").asString();
    assertEquals("hello", ret);
    customContext.leave();
  }

  @Test
  public void pidTest() {
    boolean ret = context.eval(YonaLanguage.ID, "System::pid > 0").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void getEnvTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).environment("TEST_ENV", "TEST").build();
    customContext.enter();
    String ret = customContext.eval(YonaLanguage.ID, "System::get_env \"TEST_ENV\"").asString();
    assertEquals("TEST", ret);
    customContext.leave();
  }

  @Test
  public void getArgsTest() {
    Context customContext = Context.newBuilder().allowAllAccess(true).arguments("yona", new String[] {"-h", "-test"}).build();
    customContext.enter();
    boolean ret = customContext.eval(YonaLanguage.ID, "let [\"-h\", \"-test\"] = System::args in true").asBoolean();
    assertTrue(ret);
    customContext.leave();
  }

  @Test
  public void fileListTest() {
    long ret = context.eval(YonaLanguage.ID, "File::list_dir \".\" |> Seq::len").asLong();
    assertTrue(ret > 0);
  }

  @Test
  public void byteToIntTest() {
    long ret = context.eval(YonaLanguage.ID, "5b |> int").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void intToIntTest() {
    long ret = context.eval(YonaLanguage.ID, "5 |> int").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void floatToIntTest() {
    long ret = context.eval(YonaLanguage.ID, "5.0 |> int").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void stringToIntTest() {
    long ret = context.eval(YonaLanguage.ID, "\"5\" |> int").asLong();
    assertEquals(5L, ret);
  }

  @Test
  public void byteToFloatTest() {
    double ret = context.eval(YonaLanguage.ID, "5b |> float").asDouble();
    assertEquals(5d, ret);
  }

  @Test
  public void intToFloatTest() {
    double ret = context.eval(YonaLanguage.ID, "5 |> float").asDouble();
    assertEquals(5d, ret);
  }

  @Test
  public void floatToFloatTest() {
    double ret = context.eval(YonaLanguage.ID, "5.0 |> float").asDouble();
    assertEquals(5d, ret);
  }

  @Test
  public void stringToFloatTest() {
    double ret = context.eval(YonaLanguage.ID, "\"5\" |> float").asDouble();
    assertEquals(5d, ret);
  }

  @Test
  public void seqLookupTest() {
    long ret = context.eval(YonaLanguage.ID, "Seq::lookup 1 [5, 6, 7] ").asLong();
    assertEquals(6L, ret);
  }

  @Test
  public void seqZipTest() {
    boolean ret = context.eval(YonaLanguage.ID, "[(1, 4), (2, 5), (3, 6)] == Seq::zip [1, 2, 3] [4, 5, 6]").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void dictFromSeqTest() {
    long ret = context.eval(YonaLanguage.ID, "Dict::from_seq [(1, 4), (2, 5), (3, 6)] |> Dict::lookup 1").asLong();
    assertEquals(4, ret);
  }

  @Test
  public void seqToStrTest() {
    String ret = context.eval(YonaLanguage.ID, "[\"adam\", \"fedor\"] |> str").asString();
    assertEquals("[adam, fedor]", ret);
  }

  @Test
  public void regexpExecSuccessOneTest() {
    Value ret = context.eval(YonaLanguage.ID, "Regexp::compile \"(a|(b))c\" {:ignore_case} |> Regexp::exec \"xacy\"");
    assertEquals(2, ret.getArraySize());
    assertEquals("ac", ret.getArrayElement(0).asString());
    assertEquals("a", ret.getArrayElement(1).asString());
  }

  @Test
  public void regexpExecSuccessTwoTest() {
    Value ret = context.eval(YonaLanguage.ID, "Regexp::compile \"we\" {:ignore_case, :global} |> Regexp::exec \"We will, we will\"");
    assertEquals(2, ret.getArraySize());
    assertEquals("We", ret.getArrayElement(0).asString());
    assertEquals("we", ret.getArrayElement(1).asString());
  }

  @Test
  public void regexpExecFailTest() {
    Value ret = context.eval(YonaLanguage.ID, "Regexp::compile \"(a|(b))c\" {:ignore_case} |> Regexp::exec \"xxx\"");
    assertEquals(0, ret.getArraySize());
  }

  @Test
  public void regexpReplaceOneTest() {
    Value ret = context.eval(YonaLanguage.ID, "Regexp::compile \"we\" {:ignore_case, :global} |> Regexp::replace \"You We will, we will\" \"she\"");
    assertEquals("You she will, she will", ret.asString());
  }

  @Test
  public void regexpReplaceTwoTest() {
    Value ret = context.eval(YonaLanguage.ID, "Regexp::compile \"HTML\" {:ignore_case, :global} |> Regexp::replace \"I love HTML\" \"$& and JavaScript\"");
    assertEquals("I love HTML and JavaScript", ret.asString());
  }

  @Test
  public void reflectionModulesTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let mods = Reflect::modules in (\"File\" in mods)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void reflectionFunctionsTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let funs = Reflect::functions \"File\" in (\"path\" in funs)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void reflectionFunctionsInvalidModTest() {
    long ret = context.eval(YonaLanguage.ID, "let funs = Reflect::functions \"Invalid\" in (Dict::len funs)").asLong();
    assertEquals(0L, ret);
  }

  @Test
  public void reflectionFunctionsTwoTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let funs = Reflect::functions \"http\\\\Client\" in (\"post\" in funs)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void reflectionFunctionsThreeTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let\n" +
        "mod = module Test exports test as\n" +
        "test = 1\n" +
        "end\n" +
        "funs = Reflect::functions mod\n" +
        "in (\"test\" in funs)").asBoolean();
    assertTrue(ret);
  }

  @Test
  public void reflectionFunctionsFourTest() {
    boolean ret = context.eval(YonaLanguage.ID, "let\n" +
        "mod = module Test exports test as\n" +
        "test = 1\n" +
        "priv = 2\n" +
        "end\n" +
        "funs = Reflect::functions mod\n" +
        "in (\"priv\" in funs)").asBoolean();
    assertFalse(ret);
  }

  @Test
  public void ordTest() {
    byte ret = context.eval(YonaLanguage.ID, "ord 'x'").asByte();
    assertEquals((byte) 120, ret);
  }

  @Test
  public void trimTest() {
    String ret = context.eval(YonaLanguage.ID, "Seq::trim \" ahoj \n\t\r\"").asString();
    assertEquals("ahoj", ret);
  }
}
