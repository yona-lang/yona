package yatta.runtime;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

import static yatta.runtime.Util.*;

public class UtilTest {

  @Test
  public void testInt16ReadWrite() {
    final byte[] data = new byte[2];
    Util.int16Write((short) 0, data, 0);
    assertEquals((short) 0, Util.int16Read(data, 0));
    final short[] vals = new short[]{
     0x1, 0x2, 0x4, 0x8,
     0x1a, 0x2a, 0x4a, 0x8a,
     0x1ab, 0x2ab, 0x4ab, 0x8ab,
     0x1abc, 0x2abc, 0x4abc, (short) 0x8abc
    };
    for (short val : vals) {
      Util.int16Write(val, data, 0);
      assertEquals(val, Util.int16Read(data, 0));
    }
  }

  @Test
  public void testInt32ReadWrite() {
    final byte[] data = new byte[4];
    Util.int32Write(0, data, 0);
    assertEquals(0, Util.int32Read(data, 0));
    final int[] vals = new int[]{
     0x1, 0x2, 0x4, 0x8,
     0x1a, 0x2a, 0x4a, 0x8a,
     0x1ab, 0x2ab, 0x4ab, 0x8ab,
     0x1abc, 0x2abc, 0x4abc, 0x8abc,
     0x1abcd, 0x2abcd, 0x4abcd, 0x8abcd,
     0x1abcde, 0x2abcde, 0x4abcde, 0x8abcde,
     0x1abcdef, 0x2abcdef, 0x4abcdef, 0x8abcdef,
     0x1abcdef0, 0x2abcdef0, 0x4abcdef0, 0x8abcdef0
    };
    for (int val : vals) {
      Util.int32Write(val, data, 0);
      assertEquals(val, Util.int32Read(data, 0));
    }
  }

  @Test
  public void testVarInt63Len() {
    assertEquals(1, Util.varInt63Len(0x0L));
    assertEquals(1, Util.varInt63Len(0x7aL));
    assertEquals(2, Util.varInt63Len(0x8aL));
    assertEquals(2, Util.varInt63Len(0x3abcL));
    assertEquals(3, Util.varInt63Len(0x4abcL));
    assertEquals(3, Util.varInt63Len(0x1abcdeL));
    assertEquals(4, Util.varInt63Len(0x2abcdeL));
    assertEquals(4, Util.varInt63Len(0xfabcdefL));
    assertEquals(5, Util.varInt63Len(0x1abcdef0L));
    assertEquals(5, Util.varInt63Len(0x7abcdef01L));
    assertEquals(6, Util.varInt63Len(0x8abcdef01L));
    assertEquals(6, Util.varInt63Len(0x3abcdef0123L));
    assertEquals(7, Util.varInt63Len(0x4abcdef0123L));
    assertEquals(7, Util.varInt63Len(0x1abcdef012345L));
    assertEquals(8, Util.varInt63Len(0x2abcdef012345L));
    assertEquals(8, Util.varInt63Len(0xfabcdef0123456L));
    assertEquals(9, Util.varInt63Len(0x1abcdef01234567L));
    assertEquals(9, Util.varInt63Len(0x7abcdef012345678L));
  }

  @Test
  public void testVarInt63ReadWrite() {
    final byte[] data = new byte[9];
    Util.varInt63Write(0x7abcdef012345678L, data, 0);
    assertEquals(0x7abcdef012345678L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x1abcdef01234567L, data, 0);
    assertEquals(0x1abcdef01234567L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0xfabcdef0123456L, data, 0);
    assertEquals(0xfabcdef0123456L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x2abcdef012345L, data, 0);
    assertEquals(0x2abcdef012345L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x1abcdef012345L, data, 0);
    assertEquals(0x1abcdef012345L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x4abcdef0123L, data, 0);
    assertEquals(0x4abcdef0123L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x3abcdef0123L, data, 0);
    assertEquals(0x3abcdef0123L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x8abcdef01L, data, 0);
    assertEquals(0x8abcdef01L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x7abcdef01L, data, 0);
    assertEquals(0x7abcdef01L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x1abcdef0, data, 0);
    assertEquals(0x1abcdef0L, Util.varInt63Read(data, 0));
    Util.varInt63Write(0xfabcdefL, data, 0);
    assertEquals(0xfabcdefL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x2abcdeL, data, 0);
    assertEquals(0x2abcdeL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x1abcdeL, data, 0);
    assertEquals(0x1abcdeL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x4abcL, data, 0);
    assertEquals(0x4abcL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x3abcL, data, 0);
    assertEquals(0x3abcL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x8aL, data, 0);
    assertEquals(0x8aL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x7aL, data, 0);
    assertEquals(0x7aL, Util.varInt63Read(data, 0));
    Util.varInt63Write(0x0L, data, 0);
    assertEquals(0x0L, Util.varInt63Read(data, 0));
  }
}
