package abzu.runtime.async;

import abzu.AbzuException;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import static java.util.function.Function.identity;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

public class PromiseTest {
  private static ScheduledExecutorService exec;

  private static final int N = 131072;

  @BeforeClass
  public static void setup() {
    exec = Executors.newSingleThreadScheduledExecutor();
  }

  @Test
  public void testMapImmediate() {
    assertEquals(1, Promise.await(new Promise(1).map(i -> i)));
  }

  @Test
  public void testMapDelayed() {
    Promise src = new Promise();
    Promise dst = src.map(i -> i);
    src.fulfil(1);
    assertEquals(1, Promise.await(dst));
  }

  @Test
  public void testFlatMapImmediate() {
    assertEquals(2, Promise.await(new Promise(1).map(whatever -> new Promise(2))));
  }

  @Test
  public void testFlatMapDelayed() {
    Promise srcOne = new Promise();
    Promise srcTwo = new Promise();
    Promise dst = srcOne.map(whatever -> srcTwo);
    srcOne.fulfil(1);
    srcTwo.fulfil(2);
    assertEquals(2, Promise.await(dst));
  }

  @Test
  public void testMapUnwrapImmediate() {
    assertEquals(1, new Promise(1).mapUnwrap(i -> i));
  }

  @Test
  public void testMapUnwrapDelayed() {
    Promise src = new Promise();
    Promise dst = (Promise) src.mapUnwrap(i -> i);
    src.fulfil(1);
    assertEquals(1, Promise.await(dst));
  }

  @Test
  public void testFlatMapUnwrapImmediate() {
    assertEquals(2, new Promise(1).mapUnwrap(whatever -> new Promise(2)));
  }

  @Test
  public void testFlatMapUnwrapDelayed() {
    Promise srcOne = new Promise();
    Promise srcTwo = new Promise();
    Promise dst = (Promise) srcOne.mapUnwrap(whatever -> srcTwo);
    srcOne.fulfil(1);
    srcTwo.fulfil(2);
    assertEquals(2, Promise.await(dst));
  }

  @Test
  public void testMapUnwrapException() {
    Promise promise = new Promise();
    final Object[] holder = {null};
    promise.mapUnwrap(value -> holder[0] = value);
    promise.fulfil(new AbzuException("test", null));
    assertNull(holder[0]);
  }

  @Test
  public void testAwaitImmediate() {
    assertEquals(1, Promise.await(new Promise(1)));
  }

  @Test
  public void testAwaitDelayed() {
    Promise promise = new Promise();
    exec.schedule(() -> promise.fulfil(1), 1, TimeUnit.SECONDS);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testAll() {
    Promise fst = new Promise();
    Promise snd = new Promise();
    Promise promise = Promise.all(new Object[]{ fst, snd, 3 });
    Object[] holder = new Object[1];
    promise.mapUnwrap(v -> { holder[0] = v; return null;});
    assertNull(holder[0]);
    fst.fulfil(1);
    assertNull(holder[0]);
    snd.fulfil(2);
    assertEquals(1, ((Object[]) holder[0])[0]);
    assertEquals(2, ((Object[]) holder[0])[1]);
    assertEquals(3, ((Object[]) holder[0])[2]);
  }

  @Test
  public void testAllException() {
    Promise fst = new Promise();
    Promise snd = new Promise();
    Promise promise = Promise.all(new Object[]{ fst, snd });
    Exception e = new Exception();
    fst.fulfil(e);
    assertEquals(e, promise.mapUnwrap(identity()));
  }

  @Test
  public void testMapChain() {
    Promise original = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(identity());
    }
    original.fulfil(1);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testFlatMapChainImmediate() {
    Promise original = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(Promise::new);
    }
    original.fulfil(1);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testFlatMapChainDelayed() {
    Promise original = new Promise();
    Promise intermediate = new Promise();
    Promise promise = original;
    for (int i = 0; i < N; i++) {
      promise = promise.map(v -> intermediate);
    }
    original.fulfil(1);
    intermediate.fulfil(2);
    assertEquals(2, Promise.await(promise));
  }

  @AfterClass
  public static void teardown() {
    exec.shutdownNow();
  }
}
