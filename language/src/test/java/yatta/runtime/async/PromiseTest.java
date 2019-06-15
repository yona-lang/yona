package yatta.runtime.async;

import yatta.YattaException;
import com.oracle.truffle.api.nodes.Node;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;

import static org.junit.jupiter.api.Assertions.*;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import static java.util.function.Function.identity;

public class PromiseTest {
  private static ScheduledExecutorService exec;

  private static final int N = 131072;

  private Node node = null;

  @BeforeAll
  public static void setup() {
    exec = Executors.newSingleThreadScheduledExecutor();
  }

  @Test
  public void testMapImmediate() {
    assertEquals(1, Promise.await(new Promise(1).map(i -> i, node)));
  }

  @Test
  public void testMapDelayed() {
    Promise src = new Promise();
    Promise dst = src.map(i -> i, node);
    src.fulfil(1, node);
    assertEquals(1, Promise.await(dst));
  }

  @Test
  public void testFlatMapImmediate() {
    assertEquals(2, Promise.await(new Promise(1).map(whatever -> new Promise(2), node)));
  }

  @Test
  public void testFlatMapDelayed() {
    Promise srcOne = new Promise();
    Promise srcTwo = new Promise();
    Promise dst = srcOne.map(whatever -> srcTwo, node);
    srcOne.fulfil(1, node);
    srcTwo.fulfil(2, node);
    assertEquals(2, Promise.await(dst));
  }

  @Test
  public void testMapUnwrapImmediate() {
    assertEquals(1, new Promise(1).unwrap());
  }

  @Test
  public void testMapUnwrapException() {
    Promise promise = new Promise();
    Exception e = new YattaException("test", null);
    promise.fulfil(e, node);
    assertEquals(e, promise.value);
  }

  @Test
  public void testAwaitImmediate() {
    assertEquals(1, Promise.await(new Promise(1)));
  }

  @Test
  public void testAwaitDelayed() {
    Promise promise = new Promise();
    exec.schedule(() -> promise.fulfil(1, node), 1, TimeUnit.SECONDS);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testAll() {
    Promise fst = new Promise();
    Promise snd = new Promise();
    Promise promise = Promise.all(new Object[]{ fst, snd, 3 }, node);
    Object[] value = (Object[]) promise.unwrap();
    assertNull(value);
    fst.fulfil(1, node);
    value = (Object[]) promise.unwrap();
    assertNull(value);
    snd.fulfil(2, node);
    value = (Object[]) promise.unwrap();
    assertEquals(1, value[0]);
    assertEquals(2, value[1]);
    assertEquals(3, value[2]);
  }

  @Test
  public void testAllException() {
    Promise fst = new Promise();
    Promise snd = new Promise();
    Promise promise = Promise.all(new Object[]{ fst, snd }, node);
    Exception e = new Exception();
    fst.fulfil(e, node);
    assertEquals(e, promise.value);
  }

  @Test
  public void testMapChain() {
    Promise original = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(identity(), node);
    }
    original.fulfil(1, node);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testFlatMapChainImmediate() {
    Promise original = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(Promise::new, node);
    }
    original.fulfil(1, node);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testFlatMapChainDelayed() {
    Promise original = new Promise();
    Promise intermediate = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(v -> intermediate, node);
    }
    original.fulfil(1, node);
    intermediate.fulfil(2, node);
    assertEquals(2, Promise.await(promise));
  }

  @AfterAll
  public static void teardown() {
    exec.shutdownNow();
  }
}
