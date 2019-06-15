package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class InnerSequenceTest {

  @Test
  public void testPush() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.push(new Object[]{ new byte[]{ 1 }, i }, 1);
      for (int j = 0; j < i; j++) {
        InnerSequence.Split split = new InnerSequence.Split(false, false);
        assertEquals(0, seq.split(seq.measure() - j - 1, split));
        assertEquals(j, split.point[1]);
      }
    }
  }

  @Test
  public void testInject() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.inject(new Object[]{ new byte[]{ 1 }, i }, 1);
      for (int j = 0; j < i; j++) {
        InnerSequence.Split split = new InnerSequence.Split(false, false);
        assertEquals(0, seq.split(j, split));
        assertEquals(j, split.point[1]);
      }
    }
  }

  @Test
  public void testFirst() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.push(new Object[]{ new byte[]{ 1 }, i }, 1);
      assertEquals(i, seq.first()[1]);
    }
  }

  @Test
  public void testLast() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.inject(new Object[]{ new byte[]{ 1 }, i }, 1);
      assertEquals(i, seq.last()[1]);
    }
  }

  @Test
  public void testPop() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.inject(new Object[]{ new byte[]{ 1 }, i }, 1);
    }
    for (int i = 1; i < 100; i++) {
      seq = seq.pop();
      assertEquals(i, seq.first()[1]);
    }
  }

  @Test
  public void testEject() {
    InnerSequence seq = InnerSequence.Shallow.EMPTY;
    for (int i = 0; i < 100; i++) {
      seq = seq.push(new Object[]{ new byte[]{ 1 }, i }, 1);
    }
    for (int i = 1; i < 100; i++) {
      seq = seq.eject();
      assertEquals(i, seq.last()[1]);
    }
  }
}
