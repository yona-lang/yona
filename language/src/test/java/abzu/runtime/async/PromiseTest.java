package abzu.runtime.async;

import abzu.AbzuException;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

public class PromiseTest {
  private static ScheduledExecutorService exec;

  @BeforeClass
  public static void setup() {
    exec = Executors.newSingleThreadScheduledExecutor();
  }

  @Test
  public void testMapNotFulfilled() {
    Promise promise = new Promise();
    Object[] holder = new Object[1];
    promise.map(i -> { holder[0] = i; return null; });
    assertNull(holder[0]);
    promise.fulfil(1);
    assertEquals(1, holder[0]);
  }

  @Test
  public void testMapFulfilled() {
    Promise promise = new Promise();
    promise.fulfil(1);
    assertEquals(1, promise.map(i -> i));
  }

  @Test
  public void testMapPure() {
    Promise promise = new Promise();
    promise.fulfil(1);
    assertEquals(1, Promise.await(promise.mapPure(i -> i)));
  }

  @Test
  public void testMapOtherPromise() {
    Promise one = new Promise();
    Promise two = new Promise();
    one.fulfil(1);
    two.fulfil(2);
    assertEquals(2, one.map(ignore -> two));
  }

  @Test
  public void testMapPureOtherPromise() {
    Promise one = new Promise();
    Promise two = new Promise();
    one.fulfil(1);
    two.fulfil(2);
    assertEquals(2, Promise.await(one.mapPure(ignore -> two)));
  }

  @Test
  public void testAwaitNotFulfilled() {
    Promise promise = new Promise();
    exec.schedule(() -> promise.fulfil(1), 1, TimeUnit.SECONDS);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testAwaitFulfilled() {
    Promise promise = new Promise();
    promise.fulfil(1);
    assertEquals(1, Promise.await(promise));
  }

  @Test
  public void testMapException() {
    Promise promise = new Promise();
    final Object[] holder = {null};
    promise.map(value -> holder[0] = value);
    promise.fulfil(new AbzuException("test", null));
    assertNull(holder[0]);
  }

  @Test
  public void testAll() {
    Promise fst = new Promise();
    Promise snd = new Promise();
    Promise promise = Promise.all(new Object[]{ fst, snd, 3 });
    Object[] holder = new Object[1];
    promise.map(v -> { holder[0] = v; return null;});
    assertNull(holder[0]);
    fst.fulfil(1);
    assertNull(holder[0]);
    snd.fulfil(2);
    assertEquals(1, ((Object[]) holder[0])[0]);
    assertEquals(2, ((Object[]) holder[0])[1]);
    assertEquals(3, ((Object[]) holder[0])[2]);
  }

  @AfterClass
  public static void teardown() {
    exec.shutdownNow();
  }
}
