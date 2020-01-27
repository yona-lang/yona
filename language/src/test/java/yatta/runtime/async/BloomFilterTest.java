package yatta.runtime.async;

import org.junit.jupiter.api.Test;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

import static org.junit.Assert.assertTrue;

public class BloomFilterTest {
  static final int N = 1 << 24;

  @Test
  public void testNoFalseNegatives() {
    final int length = BloomFilter.lengthFor(N, 0.01);
    final long[] data = new long[length];
    final int hashes = BloomFilter.hashesFor(N, length);
    final VarHandle handle = MethodHandles.arrayElementVarHandle(long[].class);
    for (long i = 0; i < N; i++) {
      BloomFilter.add(data, handle, hashes, -i);
    }
    for (long i = 0; i < N; i++) {
      assertTrue(BloomFilter.mightContain(data, handle, hashes, -i));
    }
  }
}
